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
    for (unsigned card = 0; card < menu(View::home).size(); ++card)
    {
        render(frame, model, t, now);
        save(frame, root / ("home-" + std::to_string(card) + ".pgm"));
        model.handle({Key::down, Gesture::pressed, ++now, 0});
        settle();
    }
    for (const auto view : {View::games, View::tools, View::settings, View::clock, View::network,
                           View::debug, View::about})
    {
        model.open(view);
        settle();
        render(frame, model, t, now);
        save(frame, root / (std::to_string(unsigned(view)) + ".pgm"));
    }
    for (unsigned game = 0; game < 8; ++game)
    {
        model.open(View(unsigned(View::dino) + game));
        settle();
        render(frame, model, t, now);
        save(frame, root / ("ready-" + std::to_string(game) + ".pgm"));
        model.handle({Key::enter, Gesture::pressed, ++now, 0});
        model.handle({Key::enter, Gesture::released, ++now, 1});
        if (game == 0 || game == 5 || game == 6 || game == 7)
            model.handle({Key::enter, Gesture::pressed, ++now, 0});
        for (unsigned n = 0; n < 40; ++n) model.tick(now += 8);
        render(frame, model, t, now);
        save(frame, root / ("game-" + std::to_string(game) + ".pgm"));
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
    model.open(View::pet);
    for (unsigned style = 0; style < PetSession::builtin_styles; ++style)
    {
        model.pet().select(uint8_t(style), now);
        for (unsigned action = 0; action < unsigned(PetAction::count); ++action)
        {
            model.pet().act(PetAction(action), now);
            render(frame, model, t, now);
            save(frame, root / ("pet-" + std::to_string(style) + "-" +
                                std::string(pet_action_name(PetAction(action))) + ".pgm"));
        }
    }
    model.pet().select(0, now);
    for (unsigned i = 0; i < 64; ++i)
    {
        if (i == 40) model.pet().act(PetAction::happy, now);
        if (i == 56) model.pet().act(PetAction::dance, now);
        model.tick(now);
        render(frame, model, t, now);
        char name[40];
        std::snprintf(name, sizeof(name), "pet-animation-%02u.pgm", i);
        save(frame, root / name);
        now += 100;
    }
}
