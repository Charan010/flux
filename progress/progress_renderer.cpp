#include "progress_renderer.h"

#include <chrono>
#include <iostream>

ProgressRenderer::ProgressRenderer(ProgressBar& progress_bar, int interval):
    progress(progress_bar), interval_ms(interval){


    render_thread = std::thread([this]() {
        while(!progress.done()) {
            progress.render();
            std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
        }

        progress.render();
        std::cout << "\n";
    });
}


ProgressRenderer::~ProgressRenderer() {

    if(render_thread.joinable())
        render_thread.join();
}