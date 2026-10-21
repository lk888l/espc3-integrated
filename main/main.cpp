#include <memory>
#include <utility>

#include "app_manager.hpp"
#include "c3/board.hpp"
#include "c3/device_app.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
extern "C" void app_main()
{
    constexpr char tag[] = "main";
    c3::device::Runtime runtime;
    app::AppManager manager;
    const auto add = [&](std::unique_ptr<AppModule> module)
    {
        const auto result = manager.register_module(std::move(module));
        if (!result) ESP_LOGE(tag, "module registration failed (%u)", unsigned(result.status));
        return bool(result);
    };
    if (!add(c3::device::create_storage(runtime)) || !add(c3::device::create_network()) ||
        !add(c3::device::create_probe()) || !add(c3::device::create_pc_monitor()) ||
        !add(c3::device::create_input(runtime)) ||
        !add(c3::device::create_ui(runtime)) || !add(c3::device::create_console(runtime)))
        return;
    const auto result = manager.initialize_all();
    if (!result)
    {
        ESP_LOGE(tag, "initialization failed at %.*s (%u)", int(result.module_name.size()),
                 result.module_name.data(), unsigned(result.status));
        if (!manager.has_cleanup_failure()) return;
        ESP_LOGE(tag, "cleanup incomplete: retaining modules and task-owned resources");
    }
    else
    {
        ESP_LOGI(tag, "C3 GameBox started; SPI2 SCK=%d MOSI=%d CS=%d; SWD CLK=%d IO=%d",
                 CONFIG_C3_OLED_SCLK, CONFIG_C3_OLED_MOSI, CONFIG_C3_OLED_CS, CONFIG_C3_SWD_CLK,
                 CONFIG_C3_SWD_IO);
    }
    while (true)
    {
        manager.process_all();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
