/* -------------------------------------------------------------------------- */
/*                            Gomoku - entry point                            */
/* -------------------------------------------------------------------------- */

//  The binary is SELF CONTAINED: it serves the interface from ui/dist and
//  talks WebSocket to it. Playing needs no node and no other server.
//
//    ./Gomoku                    serves and opens a browser
//    ./Gomoku --port 8080        picks another port, or the next free one
//    ./Gomoku --no-browser       does not open a browser
//    ./Gomoku --ui <path>        serves another directory than ui/dist
//    ./Gomoku --parent-watchdog  stops as soon as stdin closes, for the shell
//
//  See docs/INTEGRATION.md steps 0 and 6, and docs/ELECTRON.md.

#include <fcntl.h>
#include <signal.h>
#include <sys/prctl.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <thread>

#include "net/server.hpp"
#include "server/session.hpp"

namespace {

volatile sig_atomic_t gRunning = 1;

// Asks the main loop to leave, the only thing a handler may safely do.
void onSignal(int) {
	gRunning = 0;
}

// Directory holding the executable, so ui/dist is found whatever the cwd.
std::string executableDirectory() {
	char buffer[4096];
	const ssize_t length = ::readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
	if (length <= 0) {
		return ".";
	}
	buffer[length] = '\0';
	const std::string path(buffer);
	const std::size_t slash = path.rfind('/');
	return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

// Opens `url` in the default browser, ignoring any failure: the URL is printed
// anyway, so a missing xdg-open is not worth reporting.
void openBrowser(const std::string& url) {
	const pid_t child = ::fork();
	if (child != 0) {
		return;
	}
	const int devnull = ::open("/dev/null", O_WRONLY);
	if (devnull >= 0) {
		::dup2(devnull, STDOUT_FILENO);
		::dup2(devnull, STDERR_FILENO);
	}
	::execlp("xdg-open", "xdg-open", url.c_str(), nullptr);
	::_exit(0);
}

// Dies with the parent process. PR_SET_PDEATHSIG loses the race when the
// parent dies before the call, which getppid catches right after.
void followParentDeath() {
	::prctl(PR_SET_PDEATHSIG, SIGTERM);
	if (::getppid() == 1) {
		gRunning = 0;
	}
}

// Watches stdin and stops once it closes, which happens when the shell that
// spawned us dies. Only started behind --parent-watchdog: launched from a
// terminal with stdin on /dev/null, read returns 0 at once and we would stop
// the very second we started.
void watchParentPipe() {
	char byte = 0;
	while (::read(STDIN_FILENO, &byte, 1) > 0) {
	}
	gRunning = 0;
}

struct Options {
	uint16_t port = 8642;
	bool browser = true;
	bool parentWatchdog = false;
	std::string docRoot;
};

// Reads the command line, printing usage and returning false on anything odd.
bool parseOptions(int argc, char** argv, Options& options) {
	for (int i = 1; i < argc; ++i) {
		const std::string argument = argv[i];
		if (argument == "--port" && i + 1 < argc) {
			options.port = static_cast<uint16_t>(std::atoi(argv[++i]));
		} else if (argument == "--no-browser") {
			options.browser = false;
		} else if (argument == "--parent-watchdog") {
			options.parentWatchdog = true;
		} else if (argument == "--ui" && i + 1 < argc) {
			options.docRoot = argv[++i];
		} else if (argument == "-h" || argument == "--help") {
			std::printf("usage: %s [--port N] [--no-browser] [--ui <path>] [--parent-watchdog]\n",
						argv[0]);
			return false;
		} else {
			std::fprintf(stderr, "unknown argument: %s (try --help)\n", argument.c_str());
			return false;
		}
	}
	return true;
}

}  // namespace

// Starts the server, announces its port, and blocks until asked to stop.
int main(int argc, char** argv) {
	Options options;
	if (!parseOptions(argc, argv, options)) {
		return 1;
	}
	if (options.docRoot.empty()) {
		options.docRoot = executableDirectory() + "/ui/dist";
	}

	// The subject forbids any unexpected exit: SIGPIPE is neutralised, children
	// are reaped, and every exception is caught right here.
	::signal(SIGPIPE, SIG_IGN);
	::signal(SIGCHLD, SIG_IGN);
	::signal(SIGINT, onSignal);
	::signal(SIGTERM, onSignal);

	if (options.parentWatchdog) {
		followParentDeath();
		std::thread(watchParentPipe).detach();
	}

	try {
		net::Server server(options.port, options.docRoot);
		gomoku::server::Session session(server);
		session.install();

		if (!server.start()) {
			std::fprintf(stderr, "no free port from %u\n", options.port);
			return 1;
		}

		// Machine readable and flushed: the Electron shell waits for this line
		// to know where to point its window. Without the flush it would hang on
		// a perfectly running engine (docs/ELCTRON.md).
		std::printf("GOMOKU_READY port=%u\n", server.port());
		std::fflush(stdout);

		const std::string url = "http://127.0.0.1:" + std::to_string(server.port()) + "/";
		std::fprintf(stderr, "serving %s from %s\n", url.c_str(), options.docRoot.c_str());
		if (options.browser) {
			openBrowser(url);
		}

		while (gRunning != 0) {
			const struct timespec pause{0, 50000000};
			::nanosleep(&pause, nullptr);
		}
		server.stop();
		return 0;
	} catch (const std::exception& error) {
		std::fprintf(stderr, "fatal: %s\n", error.what());
		return 1;
	} catch (...) {
		std::fprintf(stderr, "fatal: unknown error\n");
		return 1;
	}
}
