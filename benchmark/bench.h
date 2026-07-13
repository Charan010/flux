#pragma once

#include "threadpool.h"
#include <string>
#include <iomanip>
#include <iostream>

namespace term {
inline constexpr const char *reset = "\033[0m";
inline constexpr const char *bold = "\033[1m";
inline constexpr const char *dim = "\033[2m";

inline constexpr const char *red = "\033[31m";
inline constexpr const char *green = "\033[32m";
inline constexpr const char *yellow = "\033[33m";
inline constexpr const char *blue = "\033[34m";
inline constexpr const char *cyan = "\033[36m";
inline constexpr const char *gray = "\033[90m";
} 

static void clear_screen() { std::cout << "\033[2J\033[H" << std::flush; }

static std::string human_size(uint64_t bytes) {

  constexpr double KB = 1024.0;
  constexpr double MB = KB * 1024.0;
  constexpr double GB = MB * 1024.0;

  std::ostringstream out;

  out << std::fixed << std::setprecision(2);

  if (bytes >= GB)
    out << (bytes / GB) << " GB";
  else if (bytes >= MB)
    out << (bytes / MB) << " MB";
  else if (bytes >= KB)
    out << (bytes / KB) << " KB";
  else
    out << bytes << " B";

  return out.str();
}



void run_bench_mode(const std::string &input_path, Threadpool &pool);
