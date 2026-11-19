#pragma once
#include <atomic>
#include <memory>

#include "app_module.hpp"
#include "c3/control.hpp"
#include "c3/core.hpp"
#include "c3/pet.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
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
enum class UiOperation : uint8_t { open_pet, pet_action, close_pet, install_pet, settings };
struct UiRequest
{
    UiOperation operation{};
    uint8_t value = 0;
    PetAction action = PetAction::count;  // optional action immediately after selecting a style
    control::DisplayPatch display{};
    uint32_t id = 0;
    PetPack* pack = nullptr;  // ownership passes to UI on a successful enqueue
};
struct UiReply { uint32_t id = 0; bool ok = false; };
enum class ApplyResult { applied, unavailable, busy, timeout };
struct Runtime
{
    Settings initial_settings{};
    QueueHandle_t keys = nullptr, settings = nullptr;
    QueueHandle_t ui_requests = nullptr, ui_replies = nullptr;
    SemaphoreHandle_t ui_sender = nullptr;
    uint32_t request_id = 0;  // serialized by ui_sender
    std::unique_ptr<PetPack> pet_pack;
    std::atomic<uint8_t> pet_style{0};
    std::atomic<uint32_t> key_events{0}, key_drops{0}, input_late{0};
    std::atomic<uint8_t> stable_keys{0};
    std::atomic<uint32_t> frames{0}, skipped{0}, display_errors{0}, frame_us{0};
    void publish_ui(const UiSnapshot& next, const Frame* pet_frame)
    {
        portENTER_CRITICAL(&ui_lock_);
        ui_ = next;
        frame_valid_ = pet_frame != nullptr;
        if (pet_frame) pet_frame_ = *pet_frame;
        portEXIT_CRITICAL(&ui_lock_);
    }
    bool copy_pet_frame(Frame& out)
    {
        portENTER_CRITICAL(&ui_lock_);
        const bool available = frame_valid_;
        if (available) out = pet_frame_;
        portEXIT_CRITICAL(&ui_lock_);
        return available;
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
    Frame pet_frame_{};
    bool frame_valid_ = false;
};
ApplyResult apply_ui(Runtime& runtime, UiRequest request, std::unique_ptr<PetPack> pack = {});
std::unique_ptr<AppModule> create_storage(Runtime& runtime);
std::unique_ptr<AppModule> create_network();
std::unique_ptr<AppModule> create_probe();
std::unique_ptr<AppModule> create_pc_monitor();
std::unique_ptr<AppModule> create_input(Runtime& runtime);
std::unique_ptr<AppModule> create_ui(Runtime& runtime);
std::unique_ptr<AppModule> create_console(Runtime& runtime);
std::unique_ptr<AppModule> create_local_api(Runtime& runtime);
}  // namespace c3::device
