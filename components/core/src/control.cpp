#include "c3/control.hpp"

#include <algorithm>
#include <cstring>
#include <initializer_list>

#include "c3/json_reader.hpp"

namespace c3::control
{
namespace
{
// Every v1 request is a flat object. The return mask also checks required fields.
template <class F>
unsigned fields(std::string_view body, std::initializer_list<std::string_view> names, F read)
{
    Json json(body);
    if (!json.take('{')) return 0;
    unsigned seen = 0;
    do
    {
        char name[16]{};
        if (!json.string(name, sizeof(name)) || !json.take(':')) return 0;
        const auto found = std::find(names.begin(), names.end(), name);
        if (found == names.end()) return 0;
        const unsigned index = unsigned(found - names.begin());
        if ((seen & (1U << index)) || !read(json, index)) return 0;
        seen |= 1U << index;
    } while (json.take(','));
    return json.take('}') && json.done() ? seen : 0;
}
bool index_value(Json& json, int8_t& value, unsigned count)
{
    uint64_t n = 0;
    if (!integer(json, n) || n >= count) return false;
    value = int8_t(n);
    return true;
}
}  // namespace
bool read_code(std::string_view body, uint32_t& code)
{
    uint32_t n = 0;
    const auto seen = fields(body, {"code"}, [&](Json& json, unsigned) {
        char digits[7]{};
        if (!json.string(digits, sizeof(digits)) || std::strlen(digits) != 6) return false;
        for (char c : std::string_view(digits))
        {
            if (c < '0' || c > '9') return false;
            n = n * 10 + unsigned(c - '0');
        }
        return true;
    });
    if (!seen) return false;
    code = n;
    return true;
}
bool read_pet(std::string_view body, PetChange& change)
{
    PetChange next;
    const auto seen = fields(body, {"style", "action", "close"}, [&](Json& json, unsigned field) {
        if (field == 0) return index_value(json, next.style, PetSession::style_count);
        if (field == 2) return next.close = json.literal("true");
        char name[12]{};
        if (!json.string(name, sizeof(name))) return false;
        for (unsigned i = 0; i < unsigned(PetAction::count); ++i)
            if (pet_action_name(PetAction(i)) == name)
            {
                next.action = PetAction(i);
                return true;
            }
        return false;
    });
    if (!seen || (next.close && seen != 4)) return false;
    change = next;
    return true;
}
bool read_display(std::string_view body, DisplayPatch& patch)
{
    DisplayPatch next;
    if (!fields(body, {"motion", "brightness", "header"}, [&](Json& json, unsigned field) {
        if (field == 0) return index_value(json, next.motion, 3);
        if (field == 1) return index_value(json, next.brightness, 4);
        return index_value(json, next.header, 4);
    })) return false;
    patch = next;
    return true;
}
bool read_station(std::string_view body, Station& station)
{
    Station next;
    const auto seen = fields(body, {"ssid", "password", "reconnect"}, [&](Json& json, unsigned field) {
        if (field == 2) return next.reconnect = json.literal("true");
        if (field == 0) return json.string(next.ssid, sizeof(next.ssid)) && next.ssid[0];
        return json.string(next.password, sizeof(next.password)) &&
               (!next.password[0] || std::strlen(next.password) >= 8);
    });
    if (seen != 3 && seen != 4) return false;
    station = next;
    return true;
}
}  // namespace c3::control
