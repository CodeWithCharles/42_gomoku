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
//  THREADING: civetweb handlers never touch the game. They push a job onto a
//  queue and return at once, so the next frame of the same socket is read
//  while the engine thinks. One worker thread owns the game and runs the jobs
//  in order. `stop` and `pause` are the exceptions: they act right away, from
//  the handler, through atomics (the engine's stop flag, halted_, paused_).

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

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
	~Session();

	Session(const Session&) = delete;
	Session& operator=(const Session&) = delete;

	// Wires the connection and message handlers onto the server, then starts
	// the worker thread.
	void install();

  private:
	enum class JobKind : uint8_t { Connect, Command, Suggest, AiTurn };

	struct Job {
		JobKind kind;
		net::ClientId client;
		std::string text;
	};

	void enqueue(Job job);
	void workerLoop();
	void run(const Job& job);
	void dispatch(net::ClientId client, const std::string& text);
	void interruptAi();
	void pauseAi();

	void onConnect(net::ClientId client);
	void onText(net::ClientId client, const std::string& text);

	void newGame(net::ClientId client, const std::string& body);
	void play(net::ClientId client, const std::string& body);
	void suggest(net::ClientId client);
	void undo(net::ClientId client);
	void chooseColour(net::ClientId client, const std::string& body);
	void setLimits(net::ClientId client, const std::string& body);
	void setWeights(net::ClientId client, const std::string& body);
	void resume(net::ClientId client);
	void step(net::ClientId client);

	// Queues one AI move when it is the AI's turn; each AI move queues the
	// next one, so AI versus AI works too.
	void scheduleAiTurn(net::ClientId client);
	void aiTurn(net::ClientId client);
	void aiChoose(net::ClientId client);
	SearchResult think(net::ClientId client, Game& game);
	Game freePlayCopy() const;
	void syncSeats();
	void alignStats();

	void pushState(net::ClientId client, const char* event);
	void pushError(net::ClientId client, const std::string& message, const char* code);
	std::string buildState(const char* event) const;

	net::Server& server_;
	Game game_;
	Engine engine_;
	SearchLimits limits_;
	Seats seats_;
	SearchStats lastStats_;
	TimeBank bank_;
	bool aiPending_ = false;
	bool seatsSwapped_ = false;
	bool stepPending_ = false;
	std::atomic<bool> halted_{false};
	std::atomic<bool> paused_{false};
	std::vector<std::optional<SearchStats>> moveStats_;
	std::vector<std::optional<SearchStats>> choiceStats_;

	std::mutex queueMutex_;
	std::condition_variable queueReady_;
	std::deque<Job> jobs_;
	bool closing_ = false;
	std::thread worker_;
};

}  // namespace gomoku::server
