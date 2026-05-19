
#include "compression_job.h"

#include <algorithm>
#include <fstream>
#include <latch>
#include <memory>

#include "chunk.h"
#include "codec_engine.h"
#include "huffman_engine.h"
#include "mmap_file.h"


CompressionJob::CompressionJob(
    Threadpool& pool,
    SharedWriter& shared_writer,
    CompressionMode mode,
    const std::string& input,
    const std::string& output,
    size_t chunk_size
)
    : pool(pool),
      shared_writer(shared_writer),
      mode(mode),
      input_file(input),
      output_file(output),
      chunk_size(chunk_size) {}



void CompressionJob::start() {

    MappedFile input(input_file);

    if (input.size() == 0)
        throw std::runtime_error("input file is empty");

    std::shared_ptr<CodecEngine> engine;
    switch (mode) {
        case CompressionMode::Huffman:
            engine = std::make_shared<HuffmanEngine>();
            break;

        default:
            throw std::runtime_error("unsupported compression mode");
    }


    engine->prepare_encoder(input.data(),input.size());

    const size_t total_chunks = (input.size() + chunk_size - 1)/ chunk_size;

    auto ordered_queue = std::make_shared<OrderedQueue>(total_chunks);

    auto writer = std::make_shared<BitWriter>(output_file);

    auto writer_job = std::make_shared<SharedWriter::WriterJob>();
    writer_job->id = 0
    ;

    writer_job->queue =
        ordered_queue;

    writer_job->writer =
        writer;

    ordered_queue->attach_writer(&shared_writer, writer_job->id);

    shared_writer.register_job(writer_job);
    std::latch completion_latch(total_chunks);

    for (size_t i = 0; i < total_chunks; ++i) {

        const size_t offset = i * chunk_size;

        const size_t len = std::min(chunk_size, input.size() - offset);
        pool.submit([
            engine, ordered_queue, data = input.data(), offset, len, i, &completion_latch ]{

            Chunk chunk;
            chunk.id = i;

            engine->encode_chunk(data + offset, len, chunk);
            ordered_queue->push(std::move(chunk));
            completion_latch.count_down();


        });
    }

    completion_latch.wait();
    ordered_queue->close();
}