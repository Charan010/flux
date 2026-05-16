#pragma once

#include <thread>

#include "progress_bar.h"

class ProgressRenderer {

  private:
    ProgressBar& progress;

    std::thread render_thread;

    int interval_ms;

  public:
    explicit ProgressRenderer(ProgressBar& progress, int interval = 100);

    ~ProgressRenderer();
};