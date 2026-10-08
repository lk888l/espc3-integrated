#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "app_task.hpp"
#include "c3/device_app.hpp"
#include "c3/network.hpp"
#include "c3/probe.hpp"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
namespace c3::device
{
namespace
{
Runtime* runtime = nullptr;
int status_command(int, char**)
{
    const auto net = network::snapshot();
    const auto probe = dap::snapshot();
    std::printf("heap=%lu min=%lu wifi=%u sta=%s time=%s synced=%u\n",
                (unsigned long)esp_get_free_heap_size(),
                (unsigned long)esp_get_minimum_free_heap_size(), net.wifi_enabled, net.sta_ip,
                net.time, net.time_synced);
    if (runtime)
    {
        std::printf(
            "keys=%lu drops=%lu late=%lu mask=%02x frames=%lu skipped=%lu spi_errors=%lu "
            "spi_us=%lu\n",
            (unsigned long)runtime->key_events.load(), (unsigned long)runtime->key_drops.load(),
            (unsigned long)runtime->input_late.load(), runtime->stable_keys.load(),
            (unsigned long)runtime->frames.load(), (unsigned long)runtime->skipped.load(),
            (unsigned long)runtime->display_errors.load(), (unsigned long)runtime->frame_us.load());
        const auto ui = runtime->ui_snapshot();
        const auto name = title(ui.view);
        std::printf(
            "ui=%u name=\"%.*s\" selected=%u motion=%u brightness=%u header=%u "
            "animating=%u events=%lu stopwatch_ms=%lu stopwatch_running=%u "
            "countdown_seconds=%lu countdown_running=%u\n",
            unsigned(ui.view), int(name.size()), name.data(), ui.selected,
            unsigned(ui.settings.motion), ui.settings.brightness, unsigned(ui.settings.header),
            ui.animating, (unsigned long)ui.events, (unsigned long)ui.stopwatch_ms,
            ui.stopwatch_running, (unsigned long)ui.countdown_seconds, ui.countdown_running);
    }
    std::printf(
        "dap=%u ready=%u client=%u swd=%u packets=%lu errors=%lu ack=%02x requested=%lu "
        "configured=%lu limit=%lu error=%s\n",
        probe.enabled, probe.ready, probe.connected, probe.swd, (unsigned long)probe.packets,
        (unsigned long)probe.errors, probe.last_ack, (unsigned long)probe.requested_hz,
        (unsigned long)probe.configured_hz, (unsigned long)probe.limit_hz,
        esp_err_to_name(probe.error));
    return 0;
}
int wifi_command(int argc, char** argv)
{
    if (argc == 2 && !std::strcmp(argv[1], "on"))
    {
        network::set_enabled(true);
        return 0;
    }
    if (argc == 2 && !std::strcmp(argv[1], "off"))
    {
        dap::enable(false);
        network::set_enabled(false);
        return 0;
    }
    if ((argc == 3 || argc == 4) && !std::strcmp(argv[1], "set"))
    {
        if (!network::set_credentials(argv[2], argc == 4 ? argv[3] : ""))
        {
            std::printf("Invalid credentials: SSID 1..32 bytes, password empty or 8..63 bytes\n");
            return 1;
        }
        network::set_enabled(true);
        std::printf("Station configuration queued\n");
        return 0;
    }
    const auto net = network::snapshot();
    std::printf("AP: %s  KEY: %s  IP: %s  STA: %s\n", net.ap_ssid, net.ap_password, net.ap_ip,
                net.sta_ip);
    std::printf("wifi on|off; wifi set \"SSID\" \"PASSWORD\"\n");
    return argc == 1 ? 0 : 1;
}
int ntp_command(int, char**)
{
    network::sync_time();
    std::printf("Sync requested; station internet access is required\n");
    return 0;
}
int dap_command(int argc, char** argv)
{
    if (argc == 2 && !std::strcmp(argv[1], "on"))
    {
        network::set_enabled(true);
        dap::enable(true);
        return 0;
    }
    if (argc == 2 && !std::strcmp(argv[1], "off"))
    {
        dap::enable(false);
        return 0;
    }
    if (argc == 3 && !std::strcmp(argv[1], "clock"))
    {
        char* end = nullptr;
        const auto hz = std::strtoul(argv[2], &end, 10);
        if (!*argv[2] || *end || hz < 1000 || hz > 2000000) return 1;
        dap::set_clock_limit(hz);
        return 0;
    }
    if (argc == 1 || (argc == 2 && !std::strcmp(argv[1], "status")))
        return status_command(0, nullptr);
    std::printf("dap on|off|status; dap clock HZ\n");
    return 1;
}
int key_command(int argc, char** argv)
{
    if (argc != 2 || !runtime || !runtime->keys) return 1;
    constexpr const char* names[] = {"up",   "down", "left",  "right",
                                     "jump", "func", "enter", "back"};
    for (unsigned i = 0; i < 8; ++i)
        if (!std::strcmp(argv[1], names[i]))
        {
            const KeyEvent event{Key(i), Gesture::pressed, uint32_t(esp_timer_get_time() / 1000),
                                 0};
            return xQueueSend(runtime->keys, &event, 0) == pdTRUE ? 0 : 1;
        }
    return 1;
}
class ConsoleTask final : public AppTask
{
   public:
    ConsoleTask() : AppTask("console", 4096, 2, 0) {}

   private:
    void write(const char* text, std::size_t n)
    {
        usb_serial_jtag_write_bytes(text, n, pdMS_TO_TICKS(20));
    }
    void main() override
    {
        char line[192]{};
        std::size_t used = 0;
        bool overflow = false;
        write("\r\nc3> ", 6);
        while (!should_exit())
        {
            uint8_t ch;
            if (usb_serial_jtag_read_bytes(&ch, 1, pdMS_TO_TICKS(20)) <= 0) continue;
            if (ch == '\r' || ch == '\n')
            {
                if (!used && !overflow) continue;
                write("\r\n", 2);
                if (!overflow)
                {
                    line[used] = 0;
                    int result = 0;
                    const auto error = esp_console_run(line, &result);
                    if (error != ESP_OK)
                        std::printf("%s\n", esp_err_to_name(error));
                    else if (result)
                        std::printf("command failed (%d)\n", result);
                }
                else
                    std::printf("command too long\n");
                used = 0;
                overflow = false;
                write("c3> ", 4);
            }
            else if (ch == 8 || ch == 127)
            {
                if (used)
                {
                    --used;
                    write("\b \b", 3);
                }
            }
            else if (ch >= 32)
            {
                if (used < sizeof(line) - 1)
                {
                    line[used++] = char(ch);
                    write(reinterpret_cast<char*>(&ch), 1);
                }
                else
                    overflow = true;
            }
        }
    }
};
class ConsoleModule final : public AppModule
{
   public:
    explicit ConsoleModule(Runtime& r) : runtime_(r) {}
    std::string_view name() const override { return "console"; }

   private:
    Runtime& runtime_;
    ConsoleTask task_;
    bool driver_ = false, engine_ = false;
    bool on_initialize() override
    {
        usb_serial_jtag_driver_config_t usb{};
        usb.tx_buffer_size = 1024;
        usb.rx_buffer_size = 256;
        if (usb_serial_jtag_driver_install(&usb) != ESP_OK) return false;
        driver_ = true;
        usb_serial_jtag_vfs_use_driver();
        esp_console_config_t config = ESP_CONSOLE_CONFIG_DEFAULT();
        config.max_cmdline_args = 5;
        config.max_cmdline_length = 192;
        if (esp_console_init(&config) != ESP_OK) return false;
        engine_ = true;
        runtime = &runtime_;
        if (esp_console_register_help_command() != ESP_OK) return false;
        const esp_console_cmd_t commands[] = {{.command = "status",
                                               .help = "Show runtime health and DAP state",
                                               .hint = nullptr,
                                               .func = status_command,
                                               .argtable = nullptr,
                                               .func_w_context = nullptr,
                                               .context = nullptr},
                                              {.command = "wifi",
                                               .help = "Wi-Fi status, on/off or set credentials",
                                               .hint = "on|off|set SSID PASSWORD",
                                               .func = wifi_command,
                                               .argtable = nullptr,
                                               .func_w_context = nullptr,
                                               .context = nullptr},
                                              {.command = "ntp",
                                               .help = "Request network time synchronization",
                                               .hint = nullptr,
                                               .func = ntp_command,
                                               .argtable = nullptr,
                                               .func_w_context = nullptr,
                                               .context = nullptr},
                                              {.command = "dap",
                                               .help = "Control Wi-Fi CMSIS-DAP",
                                               .hint = "on|off|status|clock HZ",
                                               .func = dap_command,
                                               .argtable = nullptr,
                                               .func_w_context = nullptr,
                                               .context = nullptr},
                                              {.command = "key",
                                               .help = "Inject one menu press",
                                               .hint = "up|down|left|right|enter|back|func|jump",
                                               .func = key_command,
                                               .argtable = nullptr,
                                               .func_w_context = nullptr,
                                               .context = nullptr}};
        for (const auto& command : commands)
            if (esp_console_cmd_register(&command) != ESP_OK) return false;
        return task_.start();
    }
    bool on_deinitialize() override
    {
        if (!task_.stop()) return false;
        runtime = nullptr;
        if (engine_)
        {
            if (esp_console_deinit() != ESP_OK) return false;
            engine_ = false;
        }
        if (driver_)
        {
            usb_serial_jtag_vfs_use_nonblocking();
            usb_serial_jtag_driver_uninstall();
            driver_ = false;
        }
        return true;
    }
};
}  // namespace
std::unique_ptr<AppModule> create_console(Runtime& r) { return std::make_unique<ConsoleModule>(r); }
}  // namespace c3::device
