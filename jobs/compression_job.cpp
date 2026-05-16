#include "compression_job.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <fstream>

#include "chunk.h"
#include "codec_engine.h"
#include "huffman_engine.h"
#include "mmap_file.h"


CompressionJob::CompressionJob(Threadpool &pool, CompressionMode mode,
     const std::string &input,
     const std::string &output, size_t chunk_size):
    
     pool(pool), mode(mode), input_file(input), output_file(output), chunk_size(chunk_size){

    chunk_buffer = std::make_unique<ChunkBuffer>();
}


void CompressionJob::start(){

    MappedFile input(input_file);

    if(input.size() == 0)
        throw std::runtime_error("input file is empty.");

    std::unique_ptr<CodecEngine> engine;

    switch(mode){

        case CompressionMode::Huffman:
            engine = std::make_unique<HuffmanEngine>();
            break;

        default:
            throw std::runtime_error("unsupported compression mode");
    }


    engine -> prepare_encoder(input.data(), input.size());

    const size_t total_chunks = (input.size() + chunk_size - 1)/ chunk_size;

    for(size_t i = 0 ; i < total_chunks; ++i){

        const size_t offset = i * chunk_size;

        const size_t len = std::min(chunk_size, input.size()- offset);
         pool.submit([this, engine_ptr = engine.get(), data = input.data(), offset, len, i]() {
                Chunk chunk;
                chunk.id = i;

                engine_ptr->encode_chunk(data + offset, len, chunk);
                (*chunk_buffer).submit_chunk(chunk);
            });

    }
}