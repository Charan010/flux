#include "coordinator.h"

#include "jobs/compression_job.h"
#include "jobs/decompression_job.h"

Coordinator::Coordinator(size_t threads) : pool(threads) {}

Coordinator::~Coordinator() { pool.shutdown(); }

void Coordinator::compress(CompressionMode mode, const std::string& input,
        const std::string& output,
        size_t chunk_size){

            
    CompressionJob job(pool, writer, mode, input, output, chunk_size);
    job.start();
    
}


/*

void Coordinator::decompress(CompressionMode mode, const std::string& input,
                             const std::string& output, size_t chunk_size) {

    DecompressionJob job(pool, mode, input, output, chunk_size);

    job.start();
}
*/
