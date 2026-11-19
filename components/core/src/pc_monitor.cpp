#include "c3/pc_monitor.hpp"

#include "c3/json_reader.hpp"
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace c3::pc
{
namespace
{
bool optional_float(Json& json, float& output, bool& valid, double minimum, double maximum)
{
    if (json.null()) { valid = false; output = 0; return true; }
    std::string_view token;
    if (!json.number(token) || token.size() >= 64) return false;
    char buffer[64]{};
    std::memcpy(buffer, token.data(), token.size());
    char* end = nullptr;
    const double value = std::strtod(buffer, &end);
    if (end != buffer + token.size() || !std::isfinite(value) || value < minimum || value > maximum)
        return false;
    output = float(value);
    valid = true;
    return true;
}
}  // namespace

bool parse_sample(std::string_view packet, Stats& output)
{
    if (packet.empty() || packet.size() >= max_packet_bytes) return false;
    static constexpr std::string_view keys[] = {"v", "session", "seq", "host", "cpu_pct", "ram_used",
        "ram_total", "gpu_name", "gpu_pct", "vram_used", "vram_total", "gpu_temp_c"};
    Json json(packet);
    Stats next;
    uint16_t seen = 0;
    bool vram_used_valid = false, vram_total_valid = false;
    if (!json.take('{')) return false;
    for (unsigned field = 0; field < 12; ++field)
    {
        char key[24];
        if (!json.string(key, sizeof(key)) || !json.take(':')) return false;
        unsigned index = 0;
        while (index < 12 && keys[index] != key) ++index;
        if (index == 12 || (seen & (1U << index))) return false;
        seen |= uint16_t(1U << index);
        uint64_t value = 0;
        switch (index)
        {
            case 0: if (!integer(json, value) || value != 1) return false; break;
            case 1:
                if (!json.string(next.session, sizeof(next.session)) || !next.session[0]) return false;
                for (const char c : std::string_view(next.session))
                    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
                break;
            case 2:
                if (!integer(json, value) || value > UINT32_MAX) return false;
                next.seq = uint32_t(value); break;
            case 3: if (!json.string(next.host, sizeof(next.host)) || !next.host[0]) return false; break;
            case 4: if (!optional_float(json, next.cpu_pct, next.cpu_valid, 0, 100)) return false; break;
            case 5: if (!integer(json, next.ram_used)) return false; break;
            case 6: if (!integer(json, next.ram_total)) return false; break;
            case 7: if (!json.string(next.gpu_name, sizeof(next.gpu_name))) return false; break;
            case 8: if (!optional_float(json, next.gpu_pct, next.gpu_valid, 0, 100)) return false; break;
            case 9:
                if (!json.null()) { if (!integer(json, next.vram_used)) return false; vram_used_valid = true; }
                break;
            case 10:
                if (!json.null()) { if (!integer(json, next.vram_total)) return false; vram_total_valid = true; }
                break;
            case 11:
                if (!optional_float(json, next.gpu_temp_c, next.gpu_temp_valid, -100, 250)) return false;
                break;
        }
        if (field < 11 && !json.take(',')) return false;
    }
    if (!json.take('}') || !json.done() || !next.ram_total || next.ram_used > next.ram_total ||
        vram_used_valid != vram_total_valid || (vram_total_valid &&
        (!next.vram_total || next.vram_used > next.vram_total))) return false;
    next.vram_valid = vram_total_valid;
    output = next;
    return true;
}

bool newer_sequence(uint32_t incoming, uint32_t previous)
{
    const uint32_t difference = incoming - previous;
    return difference && difference < 0x80000000U;
}
bool LatestSample::accept(const Stats& incoming, uint32_t received_ms)
{
    if (received_ && !std::strcmp(incoming.session, stats_.session) &&
        !newer_sequence(incoming.seq, stats_.seq)) return false;
    stats_ = incoming;
    received_ms_ = received_ms;
    received_ = true;
    return true;
}
void LatestSample::reset() { stats_ = {}; received_ms_ = 0; received_ = false; }
uint32_t LatestSample::age_ms(uint32_t now) const { return received_ ? now - received_ms_ : 0; }
bool LatestSample::stale(uint32_t now) const { return !received_ || age_ms(now) >= stale_after_ms; }
}  // namespace c3::pc
