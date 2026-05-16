#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>

struct RenderStats {

    float progress;
    double throughput, eta;
    size_t completed, total;
    int bar_pos;
};

class ProgressBar {

  private:
    std::atomic<size_t> completed_chunks{0};
    std::atomic<size_t> processed_bytes{0};
    size_t total_chunks;
    size_t total_bytes;
    std::chrono::steady_clock::time_point start_time;

  private:
    RenderStats compute_stats() const;
    void render_bar(int width, int pos) const;
    void render_percentage(float progress) const;
    void render_throughput(double throughput) const;
    void render_eta(double eta) const;
    void render_chunks(size_t completed, size_t total) const;

  public:
    ProgressBar(size_t chunks, size_t bytes);
    void update(size_t bytes_processed);
    bool done() const;
    void render();
};