/* -------------------------------------------------------------------------- */
/*                             HTPP and WS server                             */
/* -------------------------------------------------------------------------- */

#include "net/server.hpp"

#include <civetweb.h>

#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace net {
namespace {

// How many ports to try after the requested one before giving up.
constexpr int kPortAttempts = 20;

// Websocket path we promote. Anything else is served as a static file.
constexpr const char* kWebsocketPath = "/ws";

// A websocket connection holds its worker for its whole lifetime, so a single
// thread would make the server unable to server anything else, not even
// index.html. Measured: with one thread, an open socket blocks every other
// request.
constexpr const char* kWorkerThreads = "4";

}  // namespace

struct Server::Impl {
	uint16_t port = 0;
	std::string docRoot;
	mg_context* context = nullptr;

	ConnectHandler onConnect;
	TextHandler onText;

	// civetweb hands over frames, not messages, so a fragmented message is
	// rebuilt here before reaching the protocol layer.
	struct Client {
		mg_connection* connection = nullptr;
		std::string pending;
	};

	std::mutex mutex;
	std::unordered_map<ClientId, Client> clients;
	ClientId nextId = 1;

	// Per connection cookie civetweb carries for us.
	struct Cookie {
		Impl* impl;
		ClientId id;
	};

	// Registers a fresh client and returns the id handed to the callbacks.
	ClientId add(mg_connection* connection) {
		const std::lock_guard<std::mutex> lock(mutex);
		const ClientId id = nextId++;
		clients[id] = Client{connection, {}};
		return id;
	}

	// Forgets a client that just disconnected.
	void remove(ClientId id) {
		const std::lock_guard<std::mutex> lock(mutex);
		clients.erase(id);
	}

	// Connection behind `id`, or nullptr when it is gone.
	mg_connection* connectionof(ClientId id) {
		const std::lock_guard<std::mutex> lock(mutex);
		const auto found = clients.find(id);
		return found == clients.end() ? nullptr : found->second.connection;
	}

	// Ever connected client, snapshotted so a handler may disconnect one.
	std::vector<ClientId> everyClient() {
		const std::lock_guard<std::mutex> lock(mutex);
		std::vector<ClientId> ids;
		ids.reserve(clients.size());
		for (const auto& entry : clients) {
			ids.push_back(entry.first);
		}
		return ids;
	}

	// Accepts every handshake; the real filtering is the loopback bind.
	static int onWsConnect(const mg_connection*, void*) { return 0; }

	// Handshake is done: register the client and tell the protocol layer.
	static void onWsReady(mg_connection* connection, void* user) {
		auto* impl = static_cast<Impl*>(user);
		const ClientId id = impl->add(connection);
		mg_set_user_connection_data(connection, new Cookie{impl, id});
		if (impl->onConnect) {
			impl->onConnect(id);
		}
	}

	// One frame arrived. Rebuild the message, then hand it over on FIN.
	static int onWsData(mg_connection* connection, int bits, char* data, size_t length,
						void* user) {
		auto* impl = static_cast<Impl*>(user);
		auto* cookie = static_cast<Cookie*>(mg_get_user_connection_data(connection));
		if (cookie == nullptr) {
			return 1;
		}

		const int opcode = bits & 0x0F;
		if (opcode == MG_WEBSOCKET_OPCODE_CONNECTION_CLOSE) {
			return 0;
		}
		if (opcode != MG_WEBSOCKET_OPCODE_TEXT && opcode != MG_WEBSOCKET_OPCODE_CONTINUATION) {
			return 1;
		}

		std::string message;
		{
			const std::lock_guard<std::mutex> lock(impl->mutex);
			const auto found = impl->clients.find(cookie->id);
			if (found == impl->clients.end()) {
				return 1;
			}
			found->second.pending.append(data, length);
			if ((bits & 0x80) == 0) {
				return 1;
			}
			message.swap(found->second.pending);
		}

		if (impl->onText) {
			impl->onText(cookie->id, message);
		}
		return 1;
	}

	// The client is gone: drop it and free its cookie.
	static void onWsClose(const mg_connection* connection, void* user) {
		auto* impl = static_cast<Impl*>(user);
		auto* mutableConnection = const_cast<mg_connection*>(connection);
		auto* cookie = static_cast<Cookie*>(mg_get_user_connection_data(mutableConnection));
		if (cookie == nullptr) {
			return;
		}
		impl->remove(cookie->id);
		mg_set_user_connection_data(mutableConnection, nullptr);
		delete cookie;
	}

	// Swallows civetweb's own logging, which would otherwise spam stderr while
	// we probe for a free port.
	static int onLogMessage(const mg_connection*, const char*) { return 1; }
};

Server::Server(uint16_t port, std::string docRoot) : impl_(std::make_unique<Impl>()) {
	impl_->port = port;
	impl_->docRoot = std::move(docRoot);
}

Server::~Server() {
	stop();
}

// Binds on the loopback, trying the requested port then the next ones.
bool Server::start() {
	if (impl_->context != nullptr) {
		return true;
	}
	mg_callbacks callbacks;
	std::memset(&callbacks, 0, sizeof(callbacks));
	callbacks.log_message = Impl::onLogMessage;

	for (int attempt = 0; attempt < kPortAttempts; ++attempt) {
		const uint16_t candidate = static_cast<uint16_t>(impl_->port + attempt);
		const std::string listening = "127.0.0.1" + std::to_string(candidate);
		const char* options[] = {
			"document_root", impl_->docRoot.c_str(), "listening_ports",			 listening.c_str(),
			"num_threads",	 kWorkerThreads,		 "enable_directory_listing", "no",
			nullptr};
		mg_context* context = mg_start(&callbacks, impl_.get(), options);
		if (context != nullptr) {
			impl_->context = context;
			impl_->port = candidate;
			mg_set_websocket_handler(context, kWebsocketPath, Impl::onWsConnect, Impl::onWsReady,
									 Impl::onWsData, Impl::onWsClose, impl_.get());
			return true;
		}
	}
	return false;
}

// Stops the server and waits for its workers.
void Server::stop() {
	if (impl_ == nullptr || impl_->context == nullptr) {
		return;
	}
	mg_stop(impl_->context);
	impl_->context = nullptr;
	const std::lock_guard<std::mutex> lock(impl_->mutex);
	impl_->clients.clear();
}

bool Server::isRunning() const {
	return impl_->context != nullptr;
}

uint16_t Server::port() const {
	return impl_->port;
}

void Server::onConnect(ConnectHandler handler) {
	impl_->onConnect = std::move(handler);
}

void Server::onText(TextHandler handler) {
	impl_->onText = std::move(handler);
}

// Sends one text frame, silently dropping it if the client already left.
void Server::sendText(ClientId client, const std::string& text) {
	mg_connection* connection = impl_->connectionof(client);
	if (connection == nullptr) {
		return;
	}
	mg_websocket_write(connection, MG_WEBSOCKET_OPCODE_TEXT, text.data(), text.size());
}

// Sends one text frame to every connected client.
void Server::broadcast(const std::string& text) {
	for (const ClientId id : impl_->everyClient()) {
		sendText(id, text);
	}
}

}  // namespace net
