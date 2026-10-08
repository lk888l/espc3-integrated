#include <cassert>
#include <initializer_list>
#include <random>

#include "c3/dap_protocol.hpp"
using namespace c3::dap;
bool valid(std::initializer_list<uint8_t> p) { return valid_request(p.begin(), p.size()); }
int main()
{
    assert(!valid({}) && !valid({0}) && valid({0, 0xff}));
    assert(valid({5, 0, 1, 2}) && !valid({5, 0, 2, 2}));
    assert(!valid({5, 0, 1, 0, 1, 2, 3}) && valid({5, 0, 1, 0, 1, 2, 3, 0xff}));
    assert(valid({5, 0, 1, 0x12, 1, 2, 3, 4}) && !valid({5, 0, 1, 0x12, 1, 2, 3}));
    assert(!valid({5, 0, 1, 0x62}) && !valid({5, 0, 1, 0x40}));
    uint8_t packet[64]{5, 0, 15};
    for (unsigned i = 0; i < 15; ++i) packet[3 + i] = 2;
    assert(valid_request(packet, 18));
    packet[2] = 16;
    packet[18] = 2;
    assert(!valid_request(packet, 19));
    assert(valid({6, 0, 15, 0, 2}) && !valid({6, 0, 16, 0, 2}));
    assert(!valid({6, 0, 0xff, 0xff, 2}) && !valid({6, 0, 1, 0, 0}));
    assert(valid({6, 0, 1, 0, 0, 0xff, 0xff, 0xff, 0xff}));
    assert(valid({0x1d, 2, 0x88, 8, 0xa5}) && !valid({0x1d, 2, 0x88, 8}));
    assert(!valid({0x1d, 8, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80}));
    assert(!valid({0x12, 0}) && !valid({0x7f, 0xff}) && !valid({0x80}) && !valid({0x14, 0}));
    uint8_t header[8]{};
    uint16_t size = 0;
    tcp_header(header, 64, 1);
    assert(tcp_length(header, size) && size == 64);
    header[6] = 2;
    assert(!tcp_length(header, size));
    header[6] = 1;
    header[7] = 1;
    assert(!tcp_length(header, size));
    header[7] = 0;
    header[4] = 65;
    assert(!tcp_length(header, size));
    header[4] = 0;
    assert(!tcp_length(header, size));
    const auto* query = reinterpret_cast<const uint8_t*>(discovery_request);
    assert(discovery_query(query, sizeof(discovery_request) - 1));
    assert(!discovery_query(query, sizeof(discovery_request)) && !discovery_query(nullptr, 0));
    const auto reply = discovery_reply("0123456789AB");
    constexpr auto tag_length = sizeof(discovery_tag) - 1;
    assert(std::memcmp(reply.data(), discovery_tag, tag_length) == 0);
    assert(std::memcmp(reply.data() + tag_length, "0123456789AB", 12) == 0);
    assert((uint16_t(reply[tag_length + 12]) << 8 | reply[tag_length + 13]) == 4441);
    std::mt19937 random(0x43334441);
    for (unsigned i = 0; i < 100000; ++i)
    {
        for (auto& byte : packet) byte = random();
        (void)valid_request(packet, random() % 65);
    }
}
