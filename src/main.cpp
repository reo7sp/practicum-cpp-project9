#include <chrono>
#include <exception>
#include <memory>
#include <print>
#include <thread>
#include <utility>

#include <SFML/Graphics.hpp>

#include <exec/repeat_until.hpp>
#include <exec/static_thread_pool.hpp>
#include <exec/variant_sender.hpp>
#include <stdexec/execution.hpp>

#include "mandelbrot_sender.hpp"
#include "sfml_display_sender.hpp"
#include "sfml_events_handler.hpp"
#include "types_sfml.hpp"

using namespace std::chrono_literals;

class AppReceiver {
public:
    using receiver_concept = stdexec::receiver_t;

    AppReceiver(stdexec::run_loop& loop, std::exception_ptr& error) : loop_(&loop), error_(&error) {
    }

    void set_value() noexcept {
        loop_->finish();
    }

    void set_error(std::exception_ptr error) noexcept {
        *error_ = std::move(error);
        loop_->finish();
    }

    void set_stopped() noexcept {
        loop_->finish();
    }

    stdexec::env<> get_env() const noexcept {
        return {};
    }

private:
    stdexec::run_loop* loop_;
    std::exception_ptr* error_;
};

class WaitForFPS {
public:
    static constexpr float TARGET_FPS = 60.0f;
    static constexpr float FRAME_TIME_MS = 1000.0f / TARGET_FPS;

    explicit WaitForFPS(FrameClock& frame_clock, unsigned int target_fps)
        : frame_clock_(frame_clock), frame_time_(1s / target_fps) {
    }

    void operator()() {
        auto cur_frame_duration = frame_clock_.GetFrameTime();

        if (cur_frame_duration < frame_time_) {
            std::this_thread::sleep_for(frame_time_ - cur_frame_duration);
        }

        frame_clock_.Reset();
    }

private:
    FrameClock& frame_clock_;
    const std::chrono::milliseconds frame_time_ = 1ms;
};

class MandelbrotApp {
public:
    MandelbrotApp() : compute_pool_{std::max(1u, std::thread::hardware_concurrency())} {
        std::println("hardware_concurrency: {}\n", std::thread::hardware_concurrency());
    }

    void Run() {
        auto initialize_pipe =
            stdexec::just() | stdexec::then([this]() {
                state_ = std::make_unique<SfmlState>(
                    RenderSettings{.width = 800, .height = 600, .max_iterations = 100, .escape_radius = 2.0}
                );
            });
        stdexec::sync_wait(std::move(initialize_pipe));

        auto compute_sched = compute_pool_.get_scheduler();
        stdexec::run_loop sfml_loop;
        auto sfml_sched = sfml_loop.get_scheduler();  // на macOS иначе не запускается из-за ограничения на main thread

        auto process_frame_pipe =
            SfmlEventHandler(state_->window, state_->render_settings, state_->app_state) |
            stdexec::let_value([this, compute_sched, sfml_sched] {
                auto skip_frame_pipe =
                    stdexec::just() | stdexec::then([this] { WaitForFPS{state_->frame_clock, 60}(); });

                auto render_frame_pipe =
                    stdexec::just(&state_->fb) | stdexec::continues_on(compute_sched) |
                    mandelbrot::MakeComputeSender(state_->render_settings, state_->app_state.viewport) |
                    stdexec::continues_on(sfml_sched) | render::MakeSfmlDisplaySender(*state_) |
                    stdexec::then([this](FrameBuffer*) {
                        state_->app_state.need_rerender = false;
                        WaitForFPS{state_->frame_clock, 60}();
                    });

                using process_frame_sender_t =
                    exec::variant_sender<decltype(skip_frame_pipe), decltype(render_frame_pipe)>;

                return state_->app_state.need_rerender ? process_frame_sender_t{std::move(render_frame_pipe)}
                                                       : process_frame_sender_t{std::move(skip_frame_pipe)};
            });

        auto repeated_pipe = std::move(process_frame_pipe) |
                             stdexec::then([this] { return state_->app_state.should_exit; }) | exec::repeat_until();

        std::exception_ptr error;
        AppReceiver receiver(sfml_loop, error);
        auto op = stdexec::connect(std::move(repeated_pipe), std::move(receiver));
        stdexec::start(op);
        sfml_loop.run();
        if (error) {
            std::rethrow_exception(error);
        }
    }

private:
    std::unique_ptr<SfmlState> state_;
    exec::static_thread_pool compute_pool_;
};

int main() {
    std::println("=== Mandelbrot Fractal Renderer ===\n");
    std::println("Controls:");
    std::println("  Left Mouse Button  - Zoom In");
    std::println("  Right Mouse Button - Zoom Out");
    std::println("  X                  - Toggle Auto Zoom (infinite zoom to 'Seahorse Valley' point)");
    std::println("  C                  - Reset to Initial View");
    std::println("  Close Window       - Exit\n");

    try {
        MandelbrotApp app;
        app.Run();
    } catch (const std::exception& e) {
        std::println("Error: {}", e.what());
        return 1;
    }
    return 0;
}
