#include "gc.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>

GcScanner::GcScanner(const Index &index) : index_(index) {}

std::unordered_set<std::string>GcScanner::collect_live_objects(const std::filesystem::path &manifest_dir) const {
    
	std::unordered_set<std::string> live;

    if (!std::filesystem::exists(manifest_dir))
        return live;

	
	/* Iterates across all manifest files and stores all the objects referenced into a set. Mark and sweep garbage collection
	   allows to accurately count what objects are being chunks even mid crash.
	*/
    for (const auto &entry : std::filesystem::directory_iterator(manifest_dir)) {
        if (!entry.is_regular_file())
            continue;

        std::ifstream manifest(entry.path());
        if (!manifest)
            throw std::runtime_error("gc: cannot read manifest: " + entry.path().string());

        std::string digest;
        while (std::getline(manifest, digest))
            if (!digest.empty())
                live.insert(digest); 
    }

    return live;
}

GcReport GcScanner::analyze(const std::unordered_set<std::string> &live_objects) const{

	GcReport report;

	for(const auto &[digest, loc]: index_){

		report.total_objects++;
		PackStats &pack = report.per_pack[loc.pack_id];

		bool object_found = (live_objects.find(digest) != live_objects.end()) ? true : false;

		if(object_found){

			report.total_live_objects++;
			pack.live_objects++;
			report.total_live_bytes += loc.compressed_size;
			pack.live_bytes += loc.compressed_size;

		}
		else{

			report.total_garbage_objects++;
			pack.garbage_objects++;
			report.total_garbage_bytes += loc.compressed_size;
			pack.garbage_bytes += loc.compressed_size;
		}
	}

	return report;
}

std::vector<uint32_t> GcScanner::select_packs_for_compaction(const GcReport &report, double min_garbage_ratio) const{

	std::vector<std::pair<uint32_t, double>> candidates;

    for (const auto &[pack_id, stats] : report.per_pack) {
        
		const uint64_t total = stats.live_bytes + stats.garbage_bytes;
        if (total == 0)
            continue;

        const double ratio = static_cast<double>(stats.garbage_bytes) / static_cast<double>(total);
        if (ratio >= min_garbage_ratio)
            candidates.emplace_back(pack_id, ratio);
    }

    std::sort(candidates.begin(), candidates.end(),
	[](const auto &a, const auto &b) { return a.second > b.second; });

    std::vector<uint32_t> packs;
    packs.reserve(candidates.size());
    for (const auto &[pack_id, _] : candidates)
        packs.push_back(pack_id);

    return packs;

}

