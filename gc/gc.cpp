#include "gc.h"

GcScanner::GcScanner(const Index &index):
	index_(index) {}


GcReport GcScanner::dry_run(const std::unordered_set<std::string>& referenced_objects) const{
    GcReport report;

    for (const auto& [digest, location] : index_){
        report.total_objects++;

        auto& pack_stats = report.per_pack[location.pack_id];

        if ((referenced_objects.find(digest) != referenced_objects.end())){
            report.total_live_objects++;
            pack_stats.live_objects++;
            pack_stats.live_bytes += location.compressed_size;

            report.total_live_bytes += location.compressed_size;
        }

        else{

            report.total_garbage_objects++;

            pack_stats.garbage_objects++;
            pack_stats.garbage_bytes += location.compressed_size;

            report.total_garbage_bytes += location.compressed_size;
        }
    }

    return report;
}