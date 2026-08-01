#include "bench.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <iomanip>

#include <unistd.h>
#include "core/snapshot_store.h"

namespace fs = std::filesystem;
namespace {

	uint64_t directory_size(const fs::path &dir){

		uint64_t total = 0;

		if(!fs::exists(dir))
			return total;

		for(const auto &e : fs::recursive_directory_iterator(dir))
			if(e.is_regular_file()) total += e.file_size();
		
		return total;
	}

	inline double convert_to_mb(uint64_t bytes){
		return static_cast<double>(bytes) / (1024.0 * 1024.0);
	}

	inline double seconds_since(std::chrono::steady_clock::time_point start) {
    	return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	}

	Digest hash_file(const fs::path &path){

		std::ifstream in(path, std::ios::binary);
		std::vector<uint8_t> buf(std::istreambuf_iterator<char>(in), {});

		Chunk c {std::move(buf) };
		return blake3_hash(c);

	}


	/* Given two directories, it checks whether the directories contain the same files and byte-level data using BLAKE3 hash. */
	bool trees_identical(const fs::path &a_root, const fs::path &b_root){

		for(const auto &e : fs::recursive_directory_iterator(a_root)){

			if(!e.is_regular_file()) continue;

			const fs::path relative = fs::relative(e.path(), a_root);
			const fs::path got = b_root / relative;

			if(!fs::exists(got) || hash_file(e.path()) != hash_file(got))
				return false;

		}

		return true;
	}
}

void run_bench(SnapshotStore &engine, const std::string store, const std::string &corpus_path){

	const fs::path corpus = corpus_path;

	if(!fs::exists(corpus))
		throw std::runtime_error("benchmark: corpus path doesnt exist");

	const fs::path out = fs::temp_directory_path() / ("relic_bench_out_" + std::to_string(getpid()));
    fs::remove_all(out);

	const std::string snap = "bench";
    fs::remove(fs::path(store) / "manifests" / snap);


	const uint64_t input_bytes  = directory_size(corpus);
    const uint64_t store_before = directory_size(fs::path(store) / "packs");

    std::cout << "  corpus       " << corpus.string() << "\n";
    std::cout << "  input        " << convert_to_mb(input_bytes) << " MB\n";
    std::cout << "  backing up...\n";

    auto t0 = std::chrono::steady_clock::now();
    engine.backup(corpus.string(), snap);
    const double backup_s = seconds_since(t0);

    const uint64_t stored = directory_size(fs::path(store) / "packs") - store_before;

    std::cout << "  restoring...\n";
    auto t1 = std::chrono::steady_clock::now();
    engine.restore(snap, out.string());
    const double restore_s = seconds_since(t1);

    std::cout << "  verifying...\n";
    const bool ok = trees_identical(corpus, out);

	const double ratio        = input_bytes ? static_cast<double>(stored) / static_cast<double>(input_bytes) : 0.0;
	const double backup_mbps  = backup_s  > 0 ? convert_to_mb(input_bytes) / backup_s  : 0.0;
	const double restore_mbps = restore_s > 0 ? convert_to_mb(input_bytes) / restore_s : 0.0;
	const char  *verdict      = ok ? "PASS (byte-identical)" : "FAIL (restore differs)";

	std::cout << std::fixed << std::setprecision(2)
          << "\n"
          << "  input        " << convert_to_mb(input_bytes) << " MB\n"
          << "  stored       " << convert_to_mb(stored)      << " MB\n"
          << "  ratio        " << ratio        << "x  (lower is better)\n"
          << "  backup       " << backup_s     << " s  (" << backup_mbps  << " MB/s)\n"
          << "  restore      " << restore_s    << " s  (" << restore_mbps << " MB/s)\n"
          << "  verify       " << verdict      << "\n\n";   
}

