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

// A websocket connection holds its worker for its whole lifetime, so the pool
// bounds how many tabs can be open at once: past it, nothing is served, not
// even index.html. Measured with 4: a fourth tab hung. Idle threads cost only
// address space.
constexpr const char* kWorkerThreads = "32";

// Idle time after which civetweb pings a websocket client. A client that
// leaves 6 pings in a row unanswered (MG_MAX_UNANSWERED_PING, civetweb's
// default of 5, plus one) is closed: about a minute and a half.
constexpr const char* kPingIntervalMs = "15000";

// Largest message a client may send, fragments included. A command is a few
// hundred bytes; past this, the connection is closed instead of buffering.
constexpr size_t kMaxMessageBytes = size_t{1} << 20;

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
	// Out of memory, the client stays unregistered and its frames are ignored:
	// nothing may be thrown back into civetweb's C code.
	static void onWsReady(mg_connection* connection, void* user) {
		auto* impl = static_cast<Impl*>(user);
		try {
			const ClientId id = impl->add(connection);
			mg_set_user_connection_data(connection, new Cookie{impl, id});
			if (impl->onConnect) {
				impl->onConnect(id);
			}
		} catch (...) {
			return;
		}
	}

	// One frame arrived. Rebuild the message, then hand it over on FIN. A
	// message over kMaxMessageBytes, or any failure, closes the connection
	// (returning 0) rather than letting an exception reach civetweb.
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

		try {
			std::string message;
			{
				const std::lock_guard<std::mutex> lock(impl->mutex);
				const auto found = impl->clients.find(cookie->id);
				if (found == impl->clients.end()) {
					return 1;
				}
				std::string& pending = found->second.pending;
				if (pending.size() + length > kMaxMessageBytes) {
					pending.clear();
					pending.shrink_to_fit();
					return 0;
				}
				pending.append(data, length);
				if ((bits & 0x80) == 0) {
					return 1;
				}
				message.swap(pending);
			}

			if (impl->onText) {
				impl->onText(cookie->id, message);
			}
			return 1;
		} catch (...) {
			return 0;
		}
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
		const std::string listening = "127.0.0.1:" + std::to_string(candidate);
		const char* options[] = {"document_root",
								 impl_->docRoot.c_str(),
								 "listening_ports",
								 listening.c_str(),
								 "num_threads",
								 kWorkerThreads,
								 "enable_directory_listing",
								 "no",
								 "static_file_max_age",
								 "0",
								 "enable_websocket_ping_pong",
								 "yes",
								 "websocket_timeout_ms",
								 kPingIntervalMs,
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

// Sends one text frame, silently dropping it if the client already left. The
// lock is held during the write: the close handler takes it too, so civetweb
// cannot release the connection under a writer from another thread.
void Server::sendText(ClientId client, const std::string& text) {
	const std::lock_guard<std::mutex> lock(impl_->mutex);
	const auto found = impl_->clients.find(client);
	if (found == impl_->clients.end()) {
		return;
	}
	mg_websocket_write(found->second.connection, MG_WEBSOCKET_OPCODE_TEXT, text.data(),
					   text.size());
}

// Sends one text frame to every connected client.
void Server::broadcast(const std::string& text) {
	for (const ClientId id : impl_->everyClient()) {
		sendText(id, text);
	}
}

}  // namespace net
