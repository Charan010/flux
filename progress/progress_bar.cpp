#include "progress_bar.h"

#include <iomanip>
#include <iostream>

ProgressBar::ProgressBar(size_t chunks, size_t bytes)
    : total_chunks(chunks), total_bytes(bytes), start_time(std::chrono::steady_clock::now()) {}

void ProgressBar::update(size_t bytes_processed) {
    completed_chunks++;
    processed_bytes += bytes_processed;
}

bool ProgressBar::done() const {
    return completed_chunks.load(std::memory_order_acquire) >= total_chunks;
}

RenderStats ProgressBar::compute_stats() const {

    using namespace std::chrono;
    auto now = steady_clock::now();

    double elapsed = duration<double>(now - start_time).count();

    size_t completed = completed_chunks.load(std::memory_order_relaxed);
    size_t processed = processed_bytes.load(std::memory_order_relaxed);

    float progress = static_cast<float>(completed) / total_chunks;

    double mb_processed = processed / (1024.0 * 1024.0);

    double throughput = elapsed > 0 ? mb_processed / elapsed : 0;

    double total_mb = total_bytes / (1024.0 * 1024.0);

    double eta = throughput > 0 ? (total_mb - mb_processed) / throughput : 0;

    return {.progress = progress,
            .throughput = throughput,
            .eta = eta,
            .completed = completed,
            .total = total_chunks,
            .bar_pos = static_cast<int>(progress * 40)};
}

void ProgressBar::render_bar(int width, int pos) const {

    std::cout << "\033[1m\033[34m[";

    for (int i = 0; i < width; ++i) {

        if (i < pos)
            std::cout << "\033[32m█";

        else
            std::cout << "\033[90m░";
    }

    std::cout << "\033[34m]\033[0m ";
}

void ProgressBar::render_percentage(float progress) const {
    std::cout << "\033[1m" << std::setw(3) << static_cast<int>(progress * 100.0f) << "%\033[0m ";
}

void ProgressBar::render_throughput(double throughput) const {
    std::cout << "\033[36m| " << std::fixed << std::setprecision(1) << throughput
              << " MB/s \033[0m";
}

void ProgressBar::render_eta(double eta) const {

    std::cout << "\033[35m| ETA " << std::fixed << std::setprecision(1) << eta << "s \033[0m";
}

void ProgressBar::render_chunks(size_t completed, size_t total) const {

    std::cout << "\033[33m(" << completed << "/" << total << ")\033[0m";
}

void ProgressBar::render() {

    constexpr int BAR_WIDTH = 40;

    auto stats = compute_stats();

    std::cout << "\r";

    render_bar(BAR_WIDTH, stats.bar_pos);

    render_percentage(stats.progress);

    render_throughput(stats.throughput);

    render_eta(stats.eta);

    render_chunks(stats.completed, stats.total);
    std::cout << std::flush;
}