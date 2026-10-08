#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "c3/core.hpp"
using namespace c3;
void save(const Frame& frame, const std::filesystem::path& file)
{
    std::ofstream out(file, std::ios::binary);
    out << "P5\n128 64\n255\n";
    for (unsigned y = 0; y < 64; ++y)
        for (unsigned x = 0; x < 128; ++x)
        {
            const char pixel = (frame[y / 8 * 128 + x] & (1U << (y % 8))) ? char(255) : char(0);
            out.write(&pixel, 1);
        }
}
int main(int argc, char** argv)
{
    const std::filesystem::path root = argc > 1 ? argv[1] : "previews";
    std::filesystem::create_directories(root);
    Model model;
    Telemetry t;
    Frame frame{};
    std::strcpy(t.time, "12:34:56");
    std::strcpy(t.date, "2026/10/08");
    std::strcpy(t.ap_ssid, "C3GB-ABCDEF");
    std::strcpy(t.ap_password, "C3-001122ABCDEF");
    t.time_synced = true;
    t.time_valid = true;
    t.probe_ready = true;
    uint32_t now = 0;
    model.tick(now);
    now = 600;
    model.tick(now);
    const auto settle = [&]()
    {
        for (unsigned n = 0; n < 240; ++n)
        {
            now += 5;
            model.tick(now);
        }
    };
    for (unsigned card = 0; card < 4; ++card)
    {
        render(frame, model, t, now);
        save(frame, root / ("home-" + std::to_string(card) + ".pgm"));
        model.handle({Key::down, Gesture::pressed, ++now, 0});
        settle();
    }
    for (const auto view : {View::tools, View::settings, View::clock, View::network, View::debug})
    {
        model.open(view);
        settle();
        render(frame, model, t, now);
        save(frame, root / (std::to_string(unsigned(view)) + ".pgm"));
    }
    model.open(View::home);
    settle();
    model.handle({Key::down, Gesture::pressed, ++now, 0});
    for (unsigned i = 0; i < 32; ++i)
    {
        now += 8;
        model.tick(now);
        render(frame, model, t, now);
        char name[32];
        std::snprintf(name, sizeof(name), "animation-%02u.pgm", i);
        save(frame, root / name);
    }
}
