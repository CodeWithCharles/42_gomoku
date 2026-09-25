/* -------------------------------------------------------------------------- */
/*            Module 4 - Glue between the engine and the interface            */
/* -------------------------------------------------------------------------- */

//  Session owns THE game and turns the protocol of docs/PROTOCOL.md into calls
//  to Game and Engine. The reference is ui/src/protocol.ts: the interface and
//  tools/mock-engine.mjs are written against it, so when this file and that one
//  disagree, this one is wrong.
//
//  No Gomoku rule here. If one itches to be written in this file, it belongs in
//  rules.hpp.
//
//  THREADING: civetweb calls the handlers from several worker threads, so every
//  command is serialised on one mutex. A search holds it for its whole budget,
//  which is exactly the intent: commands queue instead of racing on the game.

#pragma once

#include <cstdint>
#include <mutex>
#include <string>

#include "gomoku/eval.hpp"
#include "gomoku/game.hpp"
#include "gomoku/search.hpp"
#include "net/server.hpp"

namespace gomoku::server {

// Who plays which colour.
struct Seats {
	bool blackIsAi = false;
	bool whiteIsAi = true;

	bool isAi(Player p) const;
};

class Session {
  public:
	explicit Session(net::Server& server);

	// Wires the connection and message handlers onto the server.
	void install();

  private:
	void onConnect(net::ClientId client);
	void onText(net::ClientId client, const std::string& text);

	void newGame(net::ClientId client, const std::string& body);
	void play(net::ClientId client, const std::string& body);
	void suggest(net::ClientId client);
	void undo(net::ClientId client);
	void setLimits(net::ClientId client, const std::string& body);
	void setWeights(net::ClientId client, const std::string& body);

	// Lets the AI play while it is its turn, so AI versus AI works too.
	void runAiTurns(net::ClientId client);
	SearchResult think(net::ClientId client);

	void pushState(net::ClientId client, const char* event);
	void pushError(net::ClientId client, const std::string& message, const char* code);
	std::string buildState(const char* event) const;

	net::Server& server_;
	std::mutex mutex_;
	Game game_;
	Engine engine_;
	SearchLimits limits_;
	Seats seats_;
	SearchStats lastStats_;
};

}  // namespace gomoku::server
