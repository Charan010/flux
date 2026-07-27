#pragma once

#include <cstddef>
#include <string>

/*Self-test: generates a small corpus with known redundancy (a duplicated file with log kind of data)
 TO-DO: replace this with actual dataset with proper results. */
void run_bench(const std::string &store, std::size_t size_mb);