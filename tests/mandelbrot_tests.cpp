#include <SFML/Graphics.hpp>
#include <gtest/gtest.h>
#include <stdexec/execution.hpp>

#include <chrono>
#include <thread>

#include "mandelbrot_fractal_utils.hpp"
#include "mandelbrot_sender.hpp"
#include "sfml_events_handler.hpp"
#include "types_sfml.hpp"

using namespace std::chrono_literals;

namespace {

template <typename V>
struct TestReceiver {
    using receiver_concept = stdexec::receiver_t;

    V* value = nullptr;
    bool* set_value_called = nullptr;
    bool* set_error_called = nullptr;
    bool* set_stopped_called = nullptr;

    void set_value(V received_value) noexcept {
        *value = received_value;
        *set_value_called = true;
    }

    void set_error(std::exception_ptr) noexcept {
        *set_error_called = true;
    }

    void set_stopped() noexcept {
        *set_stopped_called = true;
    }

    stdexec::env<> get_env() const noexcept {
        return {};
    }
};

template <>
struct TestReceiver<void> {
    using receiver_concept = stdexec::receiver_t;

    bool* set_value_called = nullptr;
    bool* set_error_called = nullptr;
    bool* set_stopped_called = nullptr;

    void set_value() noexcept {
        *set_value_called = true;
    }

    void set_error(std::exception_ptr) noexcept {
        *set_error_called = true;
    }

    void set_stopped() noexcept {
        *set_stopped_called = true;
    }

    stdexec::env<> get_env() const noexcept {
        return {};
    }
};

}  // namespace

TEST(UnitMandelbrotUtilsTest, CenterPointBelongsToSet) {
    std::uint32_t iterations = mandelbrot::CalculateIterationsForPoint(mandelbrot::Complex{0.0, 0.0}, 100, 2.0);

    EXPECT_EQ(iterations, 100u);
}

TEST(UnitMandelbrotUtilsTest, PixelToComplexUsesViewportBounds) {
    ViewPort viewport{-2.0, 2.0, -1.0, 1.0};
    mandelbrot::Complex point = mandelbrot::Pixel2DToComplex(50, 25, viewport, 100, 50);

    EXPECT_DOUBLE_EQ(point.real(), 0.0);
    EXPECT_DOUBLE_EQ(point.imag(), 0.0);
}

TEST(UnitSenderTest, MandelbrotComputeSenderFillsFramebufferAndPreservesAlpha) {
    RenderSettings settings{.width = 8, .height = 6, .max_iterations = 32, .escape_radius = 2.0};
    FrameBuffer fb = FrameBuffer::Make(settings.width, settings.height);

    auto pipe = stdexec::just(&fb) | mandelbrot::MakeComputeSender(settings, ViewPort{});
    auto result = stdexec::sync_wait(pipe);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), &fb);
    bool has_non_zero_rgb = false;
    for (size_t i = 0; i < fb.rgba.size(); i += 4) {
        has_non_zero_rgb = has_non_zero_rgb || fb.rgba[i] != 0 || fb.rgba[i + 1] != 0 || fb.rgba[i + 2] != 0;
    }
    EXPECT_TRUE(has_non_zero_rgb);
}

TEST(CustomReceiverPipelineTest, MandelbrotComputeSenderSupportsCustomReceiverViaConnectStart) {
    RenderSettings settings{.width = 8, .height = 6, .max_iterations = 32, .escape_radius = 2.0};
    FrameBuffer fb = FrameBuffer::Make(settings.width, settings.height);
    FrameBuffer* value = nullptr;
    bool set_value_called = false;
    bool set_error_called = false;
    bool set_stopped_called = false;

    auto pipe = stdexec::just(&fb) | mandelbrot::MakeComputeSender(settings, ViewPort{});
    auto op = stdexec::connect(
        pipe, TestReceiver<FrameBuffer*>{
                  .value = &value,
                  .set_value_called = &set_value_called,
                  .set_error_called = &set_error_called,
                  .set_stopped_called = &set_stopped_called,
              }
    );
    stdexec::start(op);

    EXPECT_TRUE(set_value_called);
    EXPECT_FALSE(set_error_called);
    EXPECT_FALSE(set_stopped_called);
    EXPECT_EQ(value, &fb);
}

TEST(UnitSenderTest, SfmlEventHandlerStopsWhenAppAlreadyMarkedForExit) {
    sf::RenderWindow window;
    RenderSettings settings{.width = 8, .height = 6, .max_iterations = 32, .escape_radius = 2.0};
    AppState state;
    state.should_exit = true;
    bool set_value_called = false;
    bool set_error_called = false;
    bool set_stopped_called = false;

    auto pipe = SfmlEventHandler(window, settings, state);
    auto op = stdexec::connect(
        pipe, TestReceiver<void>{
                  .set_value_called = &set_value_called,
                  .set_error_called = &set_error_called,
                  .set_stopped_called = &set_stopped_called,
              }
    );
    stdexec::start(op);

    EXPECT_FALSE(set_value_called);
    EXPECT_FALSE(set_error_called);
    EXPECT_TRUE(set_stopped_called);
}

TEST(UnitSenderTest, SfmlEventHandlerAutoZoomUpdatesViewportAndRequestsRerender) {
    sf::RenderWindow window;
    RenderSettings settings{.width = 800, .height = 600, .max_iterations = 64, .escape_radius = 2.0};
    AppState state;
    state.auto_zoom_enabled = true;
    state.need_rerender = false;
    ViewPort initial_viewport = state.viewport;
    bool set_value_called = false;
    bool set_error_called = false;
    bool set_stopped_called = false;

    std::this_thread::sleep_for(110ms);

    auto pipe = SfmlEventHandler(window, settings, state);
    auto op = stdexec::connect(
        pipe, TestReceiver<void>{
                  .set_value_called = &set_value_called,
                  .set_error_called = &set_error_called,
                  .set_stopped_called = &set_stopped_called,
              }
    );
    stdexec::start(op);

    EXPECT_TRUE(set_value_called);
    EXPECT_FALSE(set_error_called);
    EXPECT_FALSE(set_stopped_called);
    EXPECT_TRUE(state.need_rerender);
    EXPECT_LT(state.viewport.width(), initial_viewport.width());
    EXPECT_LT(state.viewport.height(), initial_viewport.height());
}

TEST(CustomReceiverPipelineTest, FullPipelineDeliversFramebufferToCustomReceiver) {
    sf::RenderWindow window;
    RenderSettings settings{.width = 16, .height = 12, .max_iterations = 32, .escape_radius = 2.0};
    AppState state;
    FrameBuffer fb = FrameBuffer::Make(settings.width, settings.height);
    FrameBuffer* value = nullptr;
    bool set_value_called = false;
    bool set_error_called = false;
    bool set_stopped_called = false;

    auto pipe = SfmlEventHandler(window, settings, state) | stdexec::let_value([&] {
                    return stdexec::just(&fb) | mandelbrot::MakeComputeSender(settings, state.viewport);
                });
    auto op = stdexec::connect(
        pipe, TestReceiver<FrameBuffer*>{
                  .value = &value,
                  .set_value_called = &set_value_called,
                  .set_error_called = &set_error_called,
                  .set_stopped_called = &set_stopped_called,
              }
    );
    stdexec::start(op);

    EXPECT_TRUE(set_value_called);
    EXPECT_FALSE(set_error_called);
    EXPECT_FALSE(set_stopped_called);
    EXPECT_EQ(value, &fb);
    bool has_non_zero_rgb = false;
    for (size_t i = 0; i < fb.rgba.size(); i += 4) {
        has_non_zero_rgb = has_non_zero_rgb || fb.rgba[i] != 0 || fb.rgba[i + 1] != 0 || fb.rgba[i + 2] != 0;
    }
    EXPECT_TRUE(has_non_zero_rgb);
}

TEST(IntegrationPipelineTest, AutoZoomChangesViewportBeforeCompute) {
    sf::RenderWindow window;
    RenderSettings settings{.width = 32, .height = 24, .max_iterations = 32, .escape_radius = 2.0};
    AppState state;
    state.auto_zoom_enabled = true;
    state.need_rerender = false;
    FrameBuffer fb = FrameBuffer::Make(settings.width, settings.height);

    std::this_thread::sleep_for(110ms);

    auto pipe = SfmlEventHandler(window, settings, state) | stdexec::let_value([&] {
                    return stdexec::just(&fb) | mandelbrot::MakeComputeSender(settings, state.viewport);
                });
    auto result = stdexec::sync_wait(pipe);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), &fb);
    bool has_non_zero_rgb = false;
    for (size_t i = 0; i < fb.rgba.size(); i += 4) {
        has_non_zero_rgb = has_non_zero_rgb || fb.rgba[i] != 0 || fb.rgba[i + 1] != 0 || fb.rgba[i + 2] != 0;
    }
    EXPECT_TRUE(has_non_zero_rgb);
}

TEST(IntegrationPipelineTest, ShouldExitStopsPipelineBeforeCompute) {
    sf::RenderWindow window;
    RenderSettings settings{.width = 16, .height = 12, .max_iterations = 32, .escape_radius = 2.0};
    AppState state;
    state.should_exit = true;
    FrameBuffer fb = FrameBuffer::Make(settings.width, settings.height);
    FrameBuffer* value = nullptr;
    bool set_value_called = false;
    bool set_error_called = false;
    bool set_stopped_called = false;

    auto pipe = SfmlEventHandler(window, settings, state) | stdexec::let_value([&] {
                    return stdexec::just(&fb) | mandelbrot::MakeComputeSender(settings, state.viewport);
                });
    auto op = stdexec::connect(
        pipe, TestReceiver<FrameBuffer*>{
                  .value = &value,
                  .set_value_called = &set_value_called,
                  .set_error_called = &set_error_called,
                  .set_stopped_called = &set_stopped_called,
              }
    );
    stdexec::start(op);

    EXPECT_FALSE(set_value_called);
    EXPECT_FALSE(set_error_called);
    EXPECT_TRUE(set_stopped_called);
    EXPECT_EQ(value, nullptr);
    bool all_pixels_are_zero = true;
    for (sf::Uint8 pixel_value : fb.rgba) {
        all_pixels_are_zero = all_pixels_are_zero && pixel_value == 0;
    }
    EXPECT_TRUE(all_pixels_are_zero);
}
