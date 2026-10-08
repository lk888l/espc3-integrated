#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace c3::dap
{
inline constexpr std::size_t packet_size = 64;
inline constexpr uint16_t tcp_port = 4441, discovery_port = 4442;
inline constexpr char discovery_request[] = "C3_DAP_V1?", discovery_tag[] = "C3_DAP_V1!";
struct Packet
{
    uint16_t size = 0;
    uint8_t data[packet_size]{};
};
inline uint16_t read16(const uint8_t* p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
inline uint32_t read32(const uint8_t* p)
{
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline void write16(uint8_t* p, uint16_t n)
{
    p[0] = n;
    p[1] = n >> 8;
}
inline void write32(uint8_t* p, uint32_t n)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = n >> (8 * i);
}
inline void tcp_header(uint8_t* h, uint16_t n, uint8_t type = 2)
{
    write32(h, 0x00504144);
    write16(h + 4, n);
    h[6] = type;
    h[7] = 0;
}
inline bool tcp_length(const uint8_t* h, uint16_t& n)
{
    n = read16(h + 4);
    return read32(h) == 0x00504144 && h[6] == 1 && h[7] == 0 && n > 0 && n <= packet_size;
}
inline bool discovery_query(const uint8_t* p, std::size_t n)
{
    return p && n == sizeof(discovery_request) - 1 && std::memcmp(p, discovery_request, n) == 0;
}
inline std::array<uint8_t, sizeof(discovery_tag) - 1 + 14> discovery_reply(const char* serial)
{
    std::array<uint8_t, sizeof(discovery_tag) - 1 + 14> result{};
    constexpr auto tag_size = sizeof(discovery_tag) - 1;
    std::memcpy(result.data(), discovery_tag, tag_size);
    std::memcpy(result.data() + tag_size, serial, 12);
    result[tag_size + 12] = tcp_port >> 8;
    result[tag_size + 13] = tcp_port & 255;
    return result;
}
// Bound request reads and worst-case response writes before padded dispatch.
inline bool valid_request(const uint8_t* p, std::size_t n)
{
    if (!p || n == 0 || n > packet_size) return false;
    std::size_t need = 1, output = 1;
    switch (p[0])
    {
        case 0x00:
        case 0x02:
        case 0x13:
            need = 2;
            break;
        case 0x01:
        case 0x09:
            need = 3;
            break;
        case 0x03:
        case 0x07:
        case 0x0a:
            break;
        case 0x04:
        case 0x08:
            need = 6;
            break;
        case 0x10:
            need = 7;
            break;
        case 0x11:
            need = 5;
            break;
        case 0x12:
            if (n < 2) return false;
            need = 2 + ((p[1] ? unsigned(p[1]) : 256U) + 7) / 8;
            break;
        case 0x05:
            if (n < 3) return false;
            need = 3;
            output = 3;
            for (unsigned i = 0; i < p[2]; ++i)
            {
                if (need >= n) return false;
                const uint8_t flags = p[need++];
                if (flags & 0x40) return false;
                if (flags & 2)
                {
                    if (flags & 0x20) return false;
                    if (flags & 0x10)
                        need += 4;
                    else
                        output += 4 + ((flags & 0x80) ? 4 : 0);
                }
                else
                {
                    if (flags & 0x10) return false;
                    need += 4;
                    if ((flags & 0x80) && !(flags & 0x20)) output += 4;
                }
                if (need > n || output > packet_size) return false;
            }
            break;
        case 0x06:
        {
            if (n < 5 || (p[4] & 0xf0)) return false;
            const std::size_t count = read16(p + 2);
            need = 5 + ((p[4] & 2) ? 0 : 4 * count);
            output = 4 + ((p[4] & 2) ? 4 * count : 0);
            break;
        }
        case 0x1d:
            if (n < 2) return false;
            need = 2;
            output = 2;
            for (unsigned i = 0; i < p[1]; ++i)
            {
                if (need >= n) return false;
                const uint8_t info = p[need++];
                if (info & 0x40) return false;
                const std::size_t bytes = (((info & 63) ? (info & 63) : 64) + 7) / 8;
                if (info & 0x80)
                    output += bytes;
                else
                    need += bytes;
                if (need > n || output > packet_size) return false;
            }
            break;
        default:
            return false;
    }
    return need <= n && output <= packet_size;
}
}  // namespace c3::dap
