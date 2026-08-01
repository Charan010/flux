#include "gc.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>

#include "hashing/digest.h"
#include "json.hpp"

using json = nlohmann::json;


GcScanner::GcScanner(const Index &index): index_(index) {}


std::unordered_set<Digest, DigestHash> GcScanner::collect_live_objects(const std::filesystem::path &manifest_dir) const{

	std::unordered_set<Digest, DigestHash> live;

	if(!std::filesystem::exists(manifest_dir))
		return live;

	for(const auto &entry: std::filesystem::recursive_directory_iterator(manifest_dir)){

		if(!entry.is_regular_file() || entry.path().extension() == ".tmp")
			continue;

		const std::string where = entry.path().string();

		std::ifstream in(entry.path());
		if(!in)
			throw std::runtime_error("gc: cannot read manifest" + where);

		json manifest;

		try{ in >> manifest; }
		catch(const json::exception &e){
			throw std::runtime_error("gc: malformed manifest " + where + " :" + e.what());
		}

  		const auto files = manifest.find("files");
        if (files == manifest.end() || !files->is_array())
            throw std::runtime_error("gc: manifest missing 'files' array: " + where);

        for (const auto &file : *files) {

            const auto chunks = file.find("chunks");
            if (chunks == file.end() || !chunks->is_array())
                throw std::runtime_error("gc: manifest entry missing 'chunks' array: " + where);

            for (const auto &digest : *chunks) {

                if (!digest.is_string())
                    throw std::runtime_error("gc: non-string digest in manifest: " + where);

                /* Manifests store hex for readability; the index is keyed on raw
                   bytes. from_hex throws on anything that is not 64 hex chars,
                   so a corrupt manifest fails loudly instead of inserting a key
                   that silently matches nothing. */
                live.insert(from_hex(digest.get<std::string>()));
            }
        }
    }

    return live;
}


/* Walks through all live objects and keeps track of stats like how many live bytes exist in each packfile.
	Garbage collection gets triggered for a packfile when dead bytes percentage reaches x threshold.
*/
GcReport GcScanner::analyze(const std::unordered_set<Digest, DigestHash> &live_objects) const{

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

	/* A digest a manifest points at but the index has never heard of. This is the
	   inverse direction of the sweep and it is cheap, so check it: it is the only
	   signal that a snapshot has already become unrestorable. */
	for (const auto &digest : live_objects)
		if (index_.find(digest) == nullptr)
			report.dangling.push_back(digest);

	return report;
}


/* Returns list of packfiles which has > x% of dead bytes so that packfiles can be compacted.*/
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