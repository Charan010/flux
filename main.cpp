#include <iostream>

#include "bench.h"
#include "cli.h"
#include "daemon.h"
#include "jobs/jobs_common.h"
#include "shared_writer.h"
#include "threadpool.h"

int main(int argc, char *argv[]) {

  Threadpool pool(Config::threadpool_size);
  SharedWriter writer;

  constexpr size_t chunk_size = Config::chunk_size;

  if (argc > 1 && std::string(argv[1]) == "--cli") {
    run_cli(pool, writer, chunk_size);

  } else if (argc > 2 && std::string(argv[1]) == "--b") {
    run_bench_mode(argv[2], pool);

  } else {
    run_daemon(pool, writer, chunk_size);
  }

  return 0;
}
