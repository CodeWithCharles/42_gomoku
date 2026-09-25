/* -------------------------------------------------------------------------- */
/*                        Module 4 - HTTP and WS server                       */
/* -------------------------------------------------------------------------- */

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace net {

// Handle on a connected websocket client, valid until it disconnects.
using ClientId = int;
inline constexpr ClientId kNoClient = -1;

class Server {
  public:
	using ConnectHandler = std::function<void(ClientId)>;
	using TextHandler = std::function<void(ClientId, const std::string&)>;

	Server(uint16_t port, std::string docRoot);
	~Server();

	Server(const Server&) = delete;
	Server& operator=(const Server&) = delete;

	// Binds on 127.0.0.1 ONLY, never on every interface: the game has no
	// business on the school network. Tries `port`, then the next ones.
	// Returns false when none of them is free.
	bool start();

	// Stops the server and waits for its workers. Abt a sec
	void stop();

	bool isRunning() const;

	// Port actually bound, meaningful only once start() succeeded.
	uint16_t port() const;

	// A client finished the websocket handshake: time to push the full state.
	void onConnect(ConnectHandler handler);

	// A complete text message arrived, fragmented frames already reassembled.
	void onText(TextHandler handler);

	void sendText(ClientId client, const std::string& text);
	void broadcast(const std::string& text);

  private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

}  // namespace net
