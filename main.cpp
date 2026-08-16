#include <algorithm>
#include <exception>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <unistd.h>

#include "core/snapshot_store.h"

namespace fs = std::filesystem;

namespace {

constexpr const char *kDefaultStore = "store";


struct Style {
	const char *dim, *bold, *red, *yellow, *green, *reset;
};

Style style(){
	static const bool tty = ::isatty(1);
	if (!tty)
		return {"", "", "", "", "", ""};

	return {"\033[2m", "\033[1m", "\033[31m", "\033[33m", "\033[32m", "\033[0m"};
}

void banner(const std::string &store) {
	const Style s = style();
	std::cout << s.bold << "relic" << s.reset
	          << s.dim << "  " << fs::absolute(store).string() << s.reset << "\n"
	          << s.dim << "  :help for commands" << s.reset << "\n\n";
}

void help(){

	const Style s = style();
	auto row = [&](const char *cmd, const char *desc){
		std::cout << "  " << std::left << std::setw(27) << cmd
		          << s.dim << desc << s.reset << "\n";
	};

	std::cout << "\n";
	row("backup <path> <name>",      "snapshot a file or directory");
	row("restore <name> <path>",     "materialize a snapshot");
	row("list",                      "show stored snapshots");
	row("remove <name>",             "delete a snapshot (run gc to reclaim)");
	std::cout << "\n";
	row("gc [--dry-run]",            "reclaim space from deleted snapshots");
	row("fsck [--rebuild] [--deep]", "check the store; rebuild index from packs");
	row("checkpoint",                "force a full index checkpoint");
	std::cout << "\n";
	std::cout << "\n";
	row(":help",                     "this");
	row(":quit",                     "exit");
	std::cout << "\n";

}

std::vector<std::string> split(const std::string &line) {
	std::istringstream iss(line);
	return {std::istream_iterator<std::string>(iss), std::istream_iterator<std::string>()};
}

bool has_flag(const std::vector<std::string> &args, const std::string &flag) {
	return std::find(args.begin(), args.end(), flag) != args.end();
}

std::string human(uint64_t bytes) {
	static const char *unit[] = {"B", "KB", "MB", "GB", "TB"};
	double v = static_cast<double>(bytes);
	int u = 0;
	while (v >= 1024.0 && u < 4) { v /= 1024.0; ++u; }

	std::ostringstream out;
	out << std::fixed << std::setprecision(u == 0 ? 0 : 1) << v << " " << unit[u];
	return out.str();
}

void list_snapshots(const std::string &store) {

	const Style s = style();
	const fs::path dir = fs::path(store) / "manifests";

	std::vector<fs::directory_entry> entries;
	if (fs::exists(dir))
		for (const auto &e : fs::directory_iterator(dir))
			if (e.is_regular_file())
				entries.push_back(e);

	if (entries.empty()) {
		std::cout << s.dim << "  no snapshots\n" << s.reset;
		return;
	}

	std::sort(entries.begin(), entries.end(),
	          [](const auto &a, const auto &b) { return a.path() < b.path(); });

	for (const auto &e : entries)
		std::cout << "  " << std::left << std::setw(28) << e.path().filename().string()
		          << s.dim << std::right << std::setw(10)
		          << human(fs::file_size(e.path())) << s.reset << "\n";
}

void run_fsck(SnapshotStore &engine, bool rebuild, bool deep) {

	const Style s = style();
	const auto r = engine.fsck(rebuild, deep);

	if (rebuild)
		std::cout << "  rebuilt from " << r.rebuild.packs_scanned << " packs, "
		          << r.rebuild.records << " records\n";

	if (r.rebuild.packs_truncated)
		std::cout << s.yellow << "  " << r.rebuild.packs_truncated
		          << " pack(s) truncated, " << human(r.rebuild.trailing_bytes)
		          << " unusable\n" << s.reset;

	std::cout << "  " << r.snapshots << " snapshots, "
	          << r.live_chunks << " chunk references\n";

	if (r.dangling == 0) {
		std::cout << s.green << "  ok" << s.reset << "\n";
		return;
	}

	std::cout << s.red << "  " << r.dangling
	          << " dangling reference(s); these snapshots cannot be restored:\n" << s.reset;

	for (const auto &name : r.broken_snapshots)
		std::cout << s.red << "    " << name << s.reset << "\n";
}

/* Anything that reads or writes the index is unsafe while it is degraded. */
bool blocked_while_degraded(const std::string &cmd) {
	return cmd != "fsck" && cmd != "list" && cmd != "remove"
	    && cmd != ":help" && cmd != "help";
}

void dispatch(SnapshotStore &engine, const std::string &store,
		const std::string &cmd, const std::vector<std::string> &args) {

	const Style s = style();

	if (engine.degraded() && blocked_while_degraded(cmd)) {
		std::cout << s.red << "  index unavailable: " << engine.degraded_reason()
		          << s.reset << "\n"
		          << s.dim << "  run 'fsck --rebuild' to reconstruct it from packs\n"
		          << s.reset;
		return;
	}

	if (cmd == "backup") {
		if (args.size() < 3)
			throw std::runtime_error("usage: backup <path> <name>");
		engine.backup(args[1], args[2]);
		std::cout << s.green << "  " << args[2] << s.reset << "\n";

	}
	else if (cmd == "restore") {
		if (args.size() < 3)
			throw std::runtime_error("usage: restore <name> <path>");
		engine.restore(args[1], args[2]);
		std::cout << s.green << "  " << args[2] << s.reset << "\n";

	} else if (cmd == "list") {
		list_snapshots(store);

	} else if (cmd == "remove") {
		if (args.size() < 2)
			throw std::runtime_error("usage: remove <name>");
		engine.remove_snapshot(args[1]);
		std::cout << "  removed " << args[1]
		          << s.dim << "   run gc to reclaim" << s.reset << "\n";

	} else if (cmd == "gc") {
		engine.gc(has_flag(args, "--dry-run"));

	} else if (cmd == "fsck") {
		run_fsck(engine, has_flag(args, "--rebuild"), has_flag(args, "--deep"));

	} else if (cmd == "checkpoint") {
		engine.checkpoint();
		std::cout << "  checkpointed\n";

	}else if (cmd == "help" || cmd == ":help")
		help();

	else
		std::cout << s.dim << "  unknown: " << cmd << "   :help" << s.reset << "\n";
}

}

int main(int argc, char **argv) {

	const std::string store = (argc > 1) ? argv[1] : kDefaultStore;
	const Style s = style();

	try {
		SnapshotStore engine(store);
		banner(store);

		if (engine.degraded())
			std::cout << s.red << "  index unavailable: " << engine.degraded_reason()
			          << s.reset << "\n"
			          << s.dim << "  run 'fsck --rebuild' to reconstruct it from packs\n\n"
			          << s.reset;

		std::string line;
		while (std::cout << s.dim << "> " << s.reset << std::flush,
		       std::getline(std::cin, line)) {

			const auto args = split(line);
			if (args.empty())
				continue;

			const std::string &cmd = args[0];
			if (cmd == "exit" || cmd == "quit" || cmd == ":quit" || cmd == ":q")
				break;

			try {
				dispatch(engine, store, cmd, args);
			} catch (const std::exception &e) {
				std::cout << s.red << "  " << e.what() << s.reset << "\n";
			}
		}

	} catch (const std::exception &e) {
		std::cerr << s.red << "relic: " << e.what() << s.reset << "\n";
		return 1;
	}

	return 0;
}
