#include "dedup_bench.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <random>
#include <vector>

#include <unistd.h>

#include "core/dedup_engine.h"
#include "bench.h"

namespace fs = std::filesystem;

namespace {

	uint64_t packs_size(const std::string &store){

		const fs::path packs = fs::path(store) / "packs";
		uint64_t total = 0;

		if(!fs::exists(packs))
			return 0;

		for(const auto &e : fs::recursive_directory_iterator(packs))
			if(e.is_regular_file()) total += e.file_size();

		return total;

	}

	inline double convert_to_kb(uint64_t bytes){
		return static_cast<double>(bytes)/ 1024.0;
	}

	/* copy the corpus so that files could be safely mutated to test deduplication. */
	fs::path stage_corpus(const fs::path &corpus, const fs::path &work){

		const fs::path staged = work / "data";
    	fs::remove_all(work);
    	fs::create_directories(staged);
    	fs::copy(corpus, staged, fs::copy_options::recursive);
    	return staged;

	}

	/* Takes a random file from the corpus and writes random bytes of size @param bytes*/
	uint64_t modify_one_file(const fs::path &root, size_t bytes){

		std::mt19937 rng(12345);

		for(const auto &e : fs::recursive_directory_iterator(root)){

			if(!e.is_regular_file()) continue;

			std::ofstream out(e.path(), std::ios::binary | std::ios::app);
			std::vector<uint8_t> buf(bytes);
            for (auto &b : buf) b = static_cast<uint8_t>(rng());

            out.write(reinterpret_cast<const char *>(buf.data()), static_cast<std::streamsize>(buf.size()));
            return bytes;
		}

		return 0;
	}

	void row(const char *label, uint64_t growth_bytes, const char *note) {
    	std::cout << "  " << std::left << std::setw(26) << label
              << std::right << std::setw(12) << growth_bytes << " B"
              << "   " << note << "\n";
	}

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

	std::string pct(double v) {
    	std::ostringstream o;
    	o << std::fixed << std::setprecision(1) << v;
    	return o.str();
	}

}


void run_dedup_bench(DedupEngine &engine, const std::string &store, const std::string &corpus_path){

	const fs::path corpus = corpus_path;
	
	if(!fs::exists(corpus))
		throw std::runtime_error("dedup-bench: corpus not found");

	const fs::path work = fs::temp_directory_path() / ("relic_dedup_" + std::to_string(getpid()));
    const fs::path data = stage_corpus(corpus, work);

	 auto clear_snaps = [&] {
        const fs::path m = fs::path(store) / "manifests";
        for (const char *n : {"d1", "d2", "d3", "d4"})
            fs::remove(m / n);
    };

    clear_snaps();

	const uint64_t corpus_bytes = directory_size(data);

	std::cout << "\n";
    std::cout << "Deduplication benchmark\n";
    std::cout << "corpus            " << corpus.string() << "\n";
    std::cout << "logical size      " << convert_to_mb(corpus_bytes) << " MB\n";
    std::cout << "chunking          content-defined (rolling hash)\n";
    std::cout << "compression       zstd level 3\n";
    std::cout << "\n";

	  std::cout << std::left
              << std::setw(32) << "action"
              << std::right
              << std::setw(14) << "logical (MB)"
              << std::setw(14) << "stored (MB)"
              << std::setw(12) << "ratio"
              << std::setw(12) << "saved %"
              << "\n";
    std::cout << std::string(94, '-') << "\n";


	 auto measure = [&](const std::string &action, const std::string &snap,
                       const fs::path &src, uint64_t logical) {
        const uint64_t before = packs_size(store);
        engine.backup(src.string(), snap);
        const uint64_t grew = packs_size(store) - before;

        const double ratio = grew ? static_cast<double>(logical) / grew : 0.0;
        const double saved = logical ? 100.0 * (1.0 - static_cast<double>(grew) / logical) : 0.0;

        std::ostringstream r; r << std::fixed << std::setprecision(1) << ratio << "x";
        std::cout << std::left  << std::setw(32) << action
                  << std::right << std::setw(14) << convert_to_mb(logical)
                  << std::setw(14) << convert_to_mb(grew)
                  << std::setw(12) << r.str()
                  << std::setw(12) << pct(saved)
                  << "\n";
        return grew;
    };

    measure("first backup", "d1", data, corpus_bytes);

    measure("re-backup, unchanged", "d2", data, corpus_bytes);

    const uint64_t changed = modify_one_file(data, 64 * 1024);
    const uint64_t g3 = measure("re-backup, 1 file +64KB", "d3", data, corpus_bytes + changed);

    const fs::path dup = work / "dup";
    fs::remove_all(dup);
    fs::create_directories(dup);
    fs::copy(corpus, dup / "a", fs::copy_options::recursive);
    fs::copy(corpus, dup / "b", fs::copy_options::recursive);
    measure("backup 2 identical copies", "d4", dup, corpus_bytes * 2);

    std::cout << std::string(94, '-') << "\n";

    const uint64_t logical_total  = corpus_bytes * 5 + changed; 
    const uint64_t physical_total = packs_size(store);
    const double   factor = physical_total ? static_cast<double>(logical_total) / physical_total : 0.0;
    const double   saved  = logical_total ? 100.0 * (1.0 - static_cast<double>(physical_total) / logical_total) : 0.0;


    std::ostringstream f; f << std::fixed << std::setprecision(1) << factor << "x";
    std::cout << std::left  << std::setw(32) << "total (4 snapshots)"
              << std::right << std::setw(14) << convert_to_mb(logical_total)
              << std::setw(14) << convert_to_mb(physical_total)
              << std::setw(12) << f.str()
              << std::setw(12) << pct(saved)
              << "\n\n";

	clear_snaps();
	fs::remove_all(work);

}