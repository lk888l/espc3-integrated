#pragma once
#include <array>

#include "c3/core.hpp"
#include "c3/pin_policy.hpp"
#include "sdkconfig.h"
#ifndef CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#error "SWD uses UART0 pins: keep the console on native USB Serial/JTAG"
#endif
namespace c3::board
{
inline constexpr std::array<int, 8> keys = {
    CONFIG_C3_KEY_UP,   CONFIG_C3_KEY_DOWN,     CONFIG_C3_KEY_LEFT,  CONFIG_C3_KEY_RIGHT,
    CONFIG_C3_KEY_JUMP, CONFIG_C3_KEY_FUNCTION, CONFIG_C3_KEY_ENTER, CONFIG_C3_KEY_BACK};
inline constexpr std::array<int, 16> pins = {
    CONFIG_C3_OLED_SCLK,    CONFIG_C3_OLED_MOSI, CONFIG_C3_OLED_CS,   CONFIG_C3_OLED_DC,
    CONFIG_C3_OLED_RESET,   CONFIG_C3_SWD_CLK,   CONFIG_C3_SWD_IO,    CONFIG_C3_KEY_UP,
    CONFIG_C3_KEY_DOWN,     CONFIG_C3_KEY_LEFT,  CONFIG_C3_KEY_RIGHT, CONFIG_C3_KEY_JUMP,
    CONFIG_C3_KEY_FUNCTION, CONFIG_C3_KEY_ENTER, CONFIG_C3_KEY_BACK,  CONFIG_C3_SWD_RESET};
#ifdef CONFIG_C3_ALLOW_STRAPPING_PINS
inline constexpr bool allow_strapping = true;
#else
inline constexpr bool allow_strapping = false;
#endif
constexpr bool valid_plan()
{
    for (unsigned i = 0; i < pins.size(); ++i)
    {
        if (pins[i] == -1 && i >= 7) continue;
        if (!board_gpio(pins[i], allow_strapping)) return false;
    }
    return unique_pins(pins);
}
static_assert(valid_plan(),
              "GPIO conflict, reserved pin, or strapping pin: review C3 GameBox > Board wiring");
static_assert(CONFIG_FREERTOS_HZ == 1000, "Input and animation timing requires a 1 ms RTOS tick");
}  // namespace c3::board
