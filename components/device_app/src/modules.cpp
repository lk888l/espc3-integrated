#include <algorithm>
#include <array>
#include <new>

#include "app_task.hpp"
#include "c3/board.hpp"
#include "c3/device_app.hpp"
#include "c3/network.hpp"
#include "c3/oled.hpp"
#include "c3/probe.hpp"
#include "c3/piano_audio.hpp"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "soc/gpio_struct.h"

namespace c3::device
{
namespace
{
constexpr char tag[] = "device";
uint32_t now_ms() { return uint32_t(esp_timer_get_time() / 1000); }
void pace(TickType_t& next, TickType_t period, std::atomic<uint32_t>* late = nullptr)
{
    const auto current = xTaskGetTickCount();
    if (int32_t(current - next) >= int32_t(period))
    {
        if (late) late->fetch_add(1);
        next = current;  // discard missed periods, keep other tasks runnable
    }
    vTaskDelayUntil(&next, period);
}
class StorageTask final : public AppTask
{
   public:
    explicit StorageTask(Runtime& r) : AppTask("settings", 3072, 1, 0), runtime_(r) {}

   private:
    Runtime& runtime_;
    bool pending_ = false;
    Settings latest_{};
    uint32_t changed_ = 0;
    uint8_t saved_pet_style_ = 0;
    bool pet_pending_ = false;
    uint32_t pet_changed_ = 0;
    void save_pet_style()
    {
        if (!pet_pending_) return;
        nvs_handle_t handle;
        auto result = nvs_open("pet", NVS_READWRITE, &handle);
        if (result == ESP_OK)
        {
            result = nvs_set_u8(handle, "style", saved_pet_style_);
            if (result == ESP_OK) result = nvs_commit(handle);
            nvs_close(handle);
        }
        if (result == ESP_OK) pet_pending_ = false;
        else
        {
            ESP_LOGW(tag, "pet style save failed: %s", esp_err_to_name(result));
            pet_changed_ = now_ms();
        }
    }
    void flush()
    {
        if (!pending_) return;
        const uint8_t record[] = {1, uint8_t(latest_.motion), latest_.brightness,
                                  uint8_t(latest_.header)};
        nvs_handle_t handle;
        auto result = nvs_open("display", NVS_READWRITE, &handle);
        if (result == ESP_OK)
        {
            result = nvs_set_blob(handle, "settings", record, sizeof(record));
            if (result == ESP_OK) result = nvs_commit(handle);
            nvs_close(handle);
        }
        if (result != ESP_OK)
        {
            ESP_LOGE(tag, "settings save failed: %s; retrying", esp_err_to_name(result));
            changed_ = now_ms();
        }
        else
            pending_ = false;
    }
    void main() override
    {
        saved_pet_style_ = runtime_.pet_style.load();
        while (!should_exit())
        {
            Settings next;
            if (xQueueReceive(runtime_.settings, &next, pdMS_TO_TICKS(25)) == pdTRUE &&
                next.valid())
            {
                latest_ = next;
                pending_ = true;
                changed_ = now_ms();
            }
            if (pending_ && now_ms() - changed_ >= 750) flush();
            const auto style = runtime_.pet_style.load();
            if (style != saved_pet_style_)
            {
                saved_pet_style_ = style;
                pet_pending_ = true;
                pet_changed_ = now_ms();
            }
            if (pet_pending_ && now_ms() - pet_changed_ >= 750) save_pet_style();
        }
        Settings next;
        if (xQueueReceive(runtime_.settings, &next, 0) == pdTRUE && next.valid())
        {
            latest_ = next;
            pending_ = true;
        }
        flush();
        save_pet_style();
    }
};
class StorageModule final : public AppModule
{
   public:
    explicit StorageModule(Runtime& r) : runtime_(r), task_(r) {}
    std::string_view name() const override { return "storage"; }

   private:
    Runtime& runtime_;
    StorageTask task_;
    bool on_initialize() override
    {
        const auto result = nvs_flash_init();
        if (result != ESP_OK)
        {
            ESP_LOGE(tag, "NVS initialization failed: %s", esp_err_to_name(result));
            return false;
        }
        runtime_.settings = xQueueCreate(1, sizeof(Settings));
        if (!runtime_.settings) return false;
        nvs_handle_t handle;
        if (nvs_open("display", NVS_READONLY, &handle) == ESP_OK)
        {
            uint8_t record[4]{};
            std::size_t size = sizeof(record);
            const auto read = nvs_get_blob(handle, "settings", record, &size);
            nvs_close(handle);
            Settings loaded{record[0], Motion(record[1]), record[2], Header(record[3])};
            if (read == ESP_OK && size == sizeof(record) && loaded.valid())
                runtime_.initial_settings = loaded;
        }
        if (nvs_open("pet", NVS_READONLY, &handle) == ESP_OK)
        {
            uint8_t style = 0;
            if (nvs_get_u8(handle, "style", &style) == ESP_OK && style <= 4)
                runtime_.pet_style = style;
            std::size_t size = 0;
            if (nvs_get_blob(handle, "pack", nullptr, &size) == ESP_OK &&
                size >= 64 && size <= PetPack::max_bytes)
            {
                auto bytes = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[size]);
                auto pack = std::unique_ptr<PetPack>(new (std::nothrow) PetPack);
                if (bytes && pack && nvs_get_blob(handle, "pack", bytes.get(), &size) == ESP_OK &&
                    pack->decode(std::span<const uint8_t>(bytes.get(), size)))
                    runtime_.pet_pack = std::move(pack);
                else
                    ESP_LOGW(tag, "invalid or unreadable custom pet; using built-in style");
            }
            nvs_close(handle);
        }
        if (runtime_.pet_style == 4 && !runtime_.pet_pack) runtime_.pet_style = 0;
        return task_.start();
    }
    bool on_deinitialize() override
    {
        if (!task_.stop()) return false;
        if (runtime_.settings)
        {
            vQueueDelete(runtime_.settings);
            runtime_.settings = nullptr;
        }
        return true;
    }
};
class NetworkModule final : public AppModule
{
   public:
    std::string_view name() const override { return "network"; }

   private:
    bool on_initialize() override { return network::initialize() == ESP_OK; }
    bool on_deinitialize() override { return network::deinitialize(); }
};
class ProbeModule final : public AppModule
{
   public:
    std::string_view name() const override { return "wifi_dap"; }

   private:
    bool on_initialize() override { return dap::initialize() == ESP_OK; }
    bool on_deinitialize() override { return dap::deinitialize(); }
};
class InputTask final : public AppTask
{
   public:
    explicit InputTask(Runtime& r) : AppTask("buttons", 3072, 6, 0), runtime_(r) {}

   private:
    Runtime& runtime_;
    void main() override
    {
        ButtonBank bank;
        std::array<KeyEvent, 32> events{};
        TickType_t next = xTaskGetTickCount();
        while (!should_exit())
        {
            // Read all physical levels in one C3 register access.
            const uint32_t levels = GPIO.in.data;
            uint8_t pressed = 0;
            for (unsigned i = 0; i < board::keys.size(); ++i)
                if (board::keys[i] >= 0 && !(levels & (1U << unsigned(board::keys[i]))))
                    pressed |= uint8_t(1U << i);
            const uint16_t enter_window = runtime_.ui_snapshot().view == View::tetris
                                              ? tetris_double_click_ms : 0;
            const auto count = bank.sample(pressed, now_ms(), events, enter_window);
            runtime_.stable_keys = bank.stable_mask();
            runtime_.key_events.fetch_add(count);
            for (unsigned i = 0; i < count; ++i)
            {
                const auto gesture = events[i].gesture;
                const bool optional = gesture == Gesture::repeat || gesture == Gesture::click ||
                                      gesture == Gesture::double_click;
                if ((optional && uxQueueSpacesAvailable(runtime_.keys) <= 8) ||
                    xQueueSend(runtime_.keys, &events[i], 0) != pdTRUE)
                    runtime_.key_drops.fetch_add(1);
            }
            pace(next, pdMS_TO_TICKS(CONFIG_C3_INPUT_PERIOD_MS), &runtime_.input_late);
        }
    }
};
class InputModule final : public AppModule
{
   public:
    explicit InputModule(Runtime& r) : runtime_(r), task_(r) {}
    std::string_view name() const override { return "input"; }

   private:
    Runtime& runtime_;
    InputTask task_;
    bool on_initialize() override
    {
        runtime_.keys = xQueueCreate(64, sizeof(KeyEvent));
        if (!runtime_.keys) return false;
        uint64_t bits = 0;
        for (const auto pin : board::keys)
            if (pin >= 0) bits |= 1ULL << unsigned(pin);
        if (bits)
        {
            gpio_config_t pins{};
            pins.pin_bit_mask = bits;
            pins.mode = GPIO_MODE_INPUT;
            pins.pull_up_en = GPIO_PULLUP_ENABLE;
            if (gpio_config(&pins) != ESP_OK) return false;
        }
        return task_.start();
    }
    bool on_deinitialize() override
    {
        if (!task_.stop()) return false;
        for (const auto pin : board::keys)
            if (pin >= 0) gpio_reset_pin(gpio_num_t(pin));
        if (runtime_.keys)
        {
            vQueueDelete(runtime_.keys);
            runtime_.keys = nullptr;
        }
        return true;
    }
};
class UiTask final : public AppTask
{
   public:
    UiTask(Runtime& r, Oled& oled, PianoAudio& audio)
        : AppTask("oled_ui", 8192, 4, 0), runtime_(r), oled_(oled), audio_(audio) {}

   private:
    Runtime& runtime_;
    Oled& oled_;
    PianoAudio& audio_;
    Frame frame_{};
    void effects(Model& model)
    {
        const auto flags = model.take_effects();
        if (flags & stop_probe) dap::enable(false);
        if (flags & toggle_wifi)
        {
            const bool enabled = !network::enabled_requested();
            if (!enabled)
            {
                dap::enable(false);
                model.probe_stopped();
            }
            network::set_enabled(enabled);
        }
        if (flags & sync_time) network::sync_time();
        if (flags & set_probe_clock) dap::set_clock_limit(model.probe_clock());
        if (flags & start_probe)
        {
            network::set_enabled(true);
            dap::set_clock_limit(model.probe_clock());
            dap::enable(true);
        }
        if (flags & save_settings)
        {
            const auto settings = model.settings();
            if (xQueueOverwrite(runtime_.settings, &settings) != pdTRUE)
                ESP_LOGE(tag, "settings mailbox unavailable");
        }
    }
    void main() override
    {
        Model model(runtime_.initial_settings);
        model.pet().set_custom(runtime_.pet_pack.get());
        model.pet().select(runtime_.pet_style.load(), now_ms());
        TickType_t next = xTaskGetTickCount();
        uint32_t network_at = now_ms() - 250, recover_at = 0, backoff = 100;
        bool broken = false;
        Telemetry telemetry;
        while (!should_exit())
        {
            const uint32_t now = now_ms();
            std::array<PetResult, 4> pet_results{};
            unsigned pet_count = 0;
            for (; pet_count < pet_results.size(); ++pet_count)
            {
                PetRequest request;
                if (xQueueReceive(runtime_.pet_requests, &request, 0) != pdTRUE) break;
                bool ok = true;
                switch (request.operation)
                {
                    case PetOperation::open:
                        ok = model.pet().select(request.value, now);
                        if (ok) model.open(View::pet);
                        break;
                    case PetOperation::action:
                        ok = model.view() == View::pet && request.value < unsigned(PetAction::count);
                        if (ok) model.pet().act(PetAction(request.value), now);
                        break;
                    case PetOperation::close:
                        if (model.view() == View::pet) model.open(View::home, false);
                        break;
                    case PetOperation::install:
                        ok = request.pack != nullptr;
                        if (ok)
                        {
                            runtime_.pet_pack.reset(request.pack);
                            model.pet().set_custom(runtime_.pet_pack.get());
                            model.pet().select(4, now);
                            model.open(View::pet);
                        }
                        break;
                }
                pet_results[pet_count] = {request.id, ok};
            }
            for (unsigned i = 0; i < 64; ++i)
            {
                KeyEvent event;
                if (xQueueReceive(runtime_.keys, &event, 0) != pdTRUE) break;
                model.handle(event);
            }
            model.tick(now);
            audio_.update(model.view() == View::piano && model.game_status() == GameStatus::playing,
                          model.game().note(), model.game().note_sequence(), now);
            effects(model);
            if (!network::enabled_requested())
            {
                dap::enable(false);
                model.probe_stopped();
            }
            if (now - network_at >= 250)
            {
                telemetry = network::snapshot();
                network_at = now;
            }
            telemetry.free_heap = esp_get_free_heap_size();
            telemetry.min_heap = esp_get_minimum_free_heap_size();
            telemetry.key_events = runtime_.key_events;
            telemetry.key_drops = runtime_.key_drops;
            telemetry.input_late = runtime_.input_late;
            telemetry.stable_keys = runtime_.stable_keys;
            telemetry.frames = oled_.frames;
            telemetry.skipped_frames = oled_.skipped;
            telemetry.display_errors = oled_.errors;
            telemetry.frame_us = oled_.last_us;
            const auto probe = dap::snapshot();
            telemetry.probe_ready = probe.ready;
            telemetry.probe_connected = probe.connected;
            telemetry.probe_swd = probe.swd;
            telemetry.probe_packets = probe.packets;
            telemetry.probe_errors = probe.errors;
            telemetry.probe_hz = probe.configured_hz;
            telemetry.probe_requested_hz = probe.requested_hz;
            telemetry.probe_limit_hz = probe.limit_hz;
            telemetry.probe_ack = probe.last_ack;
            telemetry.probe_error = probe.error;
            telemetry.swd_clk = CONFIG_C3_SWD_CLK;
            telemetry.swd_io = CONFIG_C3_SWD_IO;
            telemetry.swd_reset = CONFIG_C3_SWD_RESET;
            render(frame_, model, telemetry, now);
            if (broken && int32_t(now - recover_at) >= 0)
            {
                broken = oled_.recover(model.settings().contrast()) != ESP_OK;
                recover_at = now + backoff;
                backoff = std::min<uint32_t>(backoff * 2, 5000);
            }
            if (!broken)
            {
                const auto result = oled_.present(frame_, model.settings().contrast());
                if (result != ESP_OK)
                {
                    ESP_LOGW(tag, "OLED transfer failed: %s", esp_err_to_name(result));
                    broken = true;
                    recover_at = now + backoff;
                }
                else
                    backoff = 100;
            }
            runtime_.publish_ui({model.view(), model.selected(model.view()), model.settings(),
                                 model.motion().active(), model.event_count(), model.stopwatch_ms(),
                                 model.countdown_seconds(), model.stopwatch_running(),
                                 model.countdown_running(), model.game_index(),
                                 uint8_t(model.game_status()), model.game_lives(), model.game().note(),
                                 model.game_score(), model.game_steps(), model.game().note_sequence(),
                                 uint32_t(uxTaskGetStackHighWaterMark(nullptr)),
                                 model.game().tetris_rotation(), model.game().tetris_piece(),
                                 model.pet().style(), uint8_t(model.pet().action()),
                                 model.pet().sequence(),
                                 uint32_t(runtime_.pet_pack ? runtime_.pet_pack->size() : 0)});
            runtime_.pet_style = model.pet().style();
            for (unsigned i = 0; i < pet_count; ++i)
                xQueueSend(runtime_.pet_results, &pet_results[i], 0);
            runtime_.frames = oled_.frames;
            runtime_.skipped = oled_.skipped;
            runtime_.display_errors = oled_.errors;
            runtime_.frame_us = oled_.last_us;
            pace(next, pdMS_TO_TICKS(CONFIG_C3_UI_PERIOD_MS));
        }
        dap::enable(false);
        audio_.stop();
    }
};
class UiModule final : public AppModule
{
   public:
    explicit UiModule(Runtime& r) : runtime_(r), task_(r, oled_, audio_) {}
    std::string_view name() const override { return "ui"; }

   private:
    Runtime& runtime_;
    Oled oled_;
    PianoAudio audio_;
    UiTask task_;
    bool on_initialize() override
    {
        runtime_.pet_requests = xQueueCreate(4, sizeof(PetRequest));
        runtime_.pet_results = xQueueCreate(4, sizeof(PetResult));
        if (!runtime_.pet_requests || !runtime_.pet_results) return false;
        return oled_.initialize(runtime_.initial_settings.contrast()) == ESP_OK &&
               audio_.initialize() && task_.start();
    }
    bool on_deinitialize() override
    {
        if (!task_.stop()) return false;
        if (runtime_.pet_requests)
        {
            PetRequest request;
            while (xQueueReceive(runtime_.pet_requests, &request, 0) == pdTRUE)
                delete request.pack;
            vQueueDelete(runtime_.pet_requests);
            runtime_.pet_requests = nullptr;
        }
        if (runtime_.pet_results)
        {
            vQueueDelete(runtime_.pet_results);
            runtime_.pet_results = nullptr;
        }
        audio_.deinitialize();
        return oled_.deinitialize();
    }
};
}  // namespace
std::unique_ptr<AppModule> create_storage(Runtime& r) { return std::make_unique<StorageModule>(r); }
std::unique_ptr<AppModule> create_network() { return std::make_unique<NetworkModule>(); }
std::unique_ptr<AppModule> create_probe() { return std::make_unique<ProbeModule>(); }
std::unique_ptr<AppModule> create_input(Runtime& r) { return std::make_unique<InputModule>(r); }
std::unique_ptr<AppModule> create_ui(Runtime& r) { return std::make_unique<UiModule>(r); }
}  // namespace c3::device
