#pragma once
#include <span>
namespace c3
{
constexpr bool board_gpio(int pin, bool allow_strapping = false)
{
    if (!((pin >= 0 && pin <= 10) || pin == 20 || pin == 21)) return false;
    return allow_strapping || (pin != 2 && pin != 8 && pin != 9);
}
constexpr bool unique_pins(std::span<const int> pins)
{
    for (unsigned i = 0; i < pins.size(); ++i)
        if (pins[i] >= 0)
            for (unsigned j = i + 1; j < pins.size(); ++j)
                if (pins[i] == pins[j]) return false;
    return true;
}
}  // namespace c3
