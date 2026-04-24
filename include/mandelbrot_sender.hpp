#pragma once

#include "mandelbrot_fractal_utils.hpp"
#include "types_sfml.hpp"

#include <print>
#include <stdexec/execution.hpp>

using namespace std::chrono_literals;

namespace mandelbrot {

static auto MakeComputeSender(RenderSettings settings, ViewPort viewport) {
    static AvrTimeCounter time_counter;

    return stdexec::then([settings, viewport](FrameBuffer* fb) {
               time_counter.Start();
               return fb;
           }) |
           stdexec::bulk(
               stdexec::par, settings.height,
               [settings, viewport](std::uint32_t y, FrameBuffer* fb) noexcept {
                   for (std::uint32_t x = 0; x < fb->width; ++x) {
                       const mandelbrot::Complex point =
                           Pixel2DToComplex(x, y, viewport, settings.width, settings.height);
                       const std::uint32_t iterations =
                           CalculateIterationsForPoint(point, settings.max_iterations, settings.escape_radius);
                       const mandelbrot::RgbColor color = IterationsToColor(iterations, settings.max_iterations);

                       const size_t pixel_idx = (static_cast<size_t>(y) * fb->width + x) * 4u;
                       fb->rgba[pixel_idx] = color.r;
                       fb->rgba[pixel_idx + 1] = color.g;
                       fb->rgba[pixel_idx + 2] = color.b;
                       fb->rgba[pixel_idx + 3] = 255;
                   }
               }
           ) |
           stdexec::then([](FrameBuffer* fb) {
               time_counter.End();

               if (time_counter.Count() % 10 == 0) {
                   std::println(
                       "\nAverage compute time: {} ms over {} frames", time_counter.GetAvr(), time_counter.Count()
                   );
               }

               return fb;
           });
}

}  // namespace mandelbrot
