#pragma once
#include <atomic>
#include <memory>

#include "app_module.hpp"
#include "c3/core.hpp"
#include "c3/pet.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
namespace c3::device
{
struct UiSnapshot
{
    View view = View::home;
    uint8_t selected = 0;
    Settings settings{};
    bool animating = false;
    uint32_t events = 0, stopwatch_ms = 0, countdown_seconds = 300;
    bool stopwatch_running = false, countdown_running = false;
    uint8_t game = 255, game_state = 0, game_lives = 0, piano_note = 0;
    uint32_t game_score = 0, game_steps = 0, piano_sequence = 0, ui_stack_free = 0;
    uint8_t tetris_rotation = 0, tetris_piece = 0;
    uint8_t pet_style = 0, pet_action = 0;
    uint32_t pet_sequence = 0, pet_bytes = 0;
};
enum class PetOperation : uint8_t { open, action, close, install };
struct PetRequest
{
    PetOperation operation{};
    uint8_t value = 0;
    uint32_t id = 0;
    PetPack* pack = nullptr;  // ownership passes to UI on a successful enqueue
};
struct PetResult { uint32_t id = 0; bool ok = false; };
struct Runtime
{
    Settings initial_settings{};
    QueueHandle_t keys = nullptr, settings = nullptr;
    QueueHandle_t pet_requests = nullptr, pet_results = nullptr;
    std::unique_ptr<PetPack> pet_pack;
    std::atomic<uint8_t> pet_style{0};
    std::atomic<uint32_t> key_events{0}, key_drops{0}, input_late{0};
    std::atomic<uint8_t> stable_keys{0};
    std::atomic<uint32_t> frames{0}, skipped{0}, display_errors{0}, frame_us{0};
    void publish_ui(const UiSnapshot& next)
    {
        portENTER_CRITICAL(&ui_lock_);
        ui_ = next;
        portEXIT_CRITICAL(&ui_lock_);
    }
    UiSnapshot ui_snapshot()
    {
        portENTER_CRITICAL(&ui_lock_);
        const auto copy = ui_;
        portEXIT_CRITICAL(&ui_lock_);
        return copy;
    }

   private:
    portMUX_TYPE ui_lock_ = portMUX_INITIALIZER_UNLOCKED;
    UiSnapshot ui_{};
};
std::unique_ptr<AppModule> create_storage(Runtime& runtime);
std::unique_ptr<AppModule> create_network();
std::unique_ptr<AppModule> create_probe();
std::unique_ptr<AppModule> create_input(Runtime& runtime);
std::unique_ptr<AppModule> create_ui(Runtime& runtime);
std::unique_ptr<AppModule> create_console(Runtime& runtime);
}  // namespace c3::device
