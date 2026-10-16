#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "c3/core.hpp"

using namespace c3;

namespace
{
std::vector<uint8_t> pack_bytes(std::array<uint8_t, 8> counts = {1, 0, 2, 0, 0, 0, 0, 0})
{
    unsigned frames = 0;
    for (auto count : counts) frames += count;
    std::vector<uint8_t> bytes(PetPack::header_bytes + frames * PetPack::frame_bytes);
    bytes[0] = 'P'; bytes[1] = 'E'; bytes[2] = 'T'; bytes[3] = '1';
    bytes[4] = 64; bytes[5] = 32; bytes[6] = 8;
    constexpr char name[] = "Test Buddy";
    std::copy_n(name, sizeof(name) - 1, bytes.begin() + 8);
    for (unsigned i = 0; i < counts.size(); ++i)
    {
        bytes[24 + i] = counts[i];
        bytes[32 + i * 2] = 120;
    }
    return bytes;
}

KeyEvent key(Key key, Gesture gesture, uint32_t now)
{
    return {key, gesture, now, gesture == Gesture::long_press ? 650U : 0U};
}
Frame draw(const PetSession& pet, uint32_t now)
{
    Frame frame{};
    Canvas canvas(frame);
    pet.draw(canvas, now);
    return frame;
}

void pack_validation()
{
    PetPack pack;
    auto bytes = pack_bytes();
    assert(pack.decode(bytes));
    assert(pack.valid() && pack.name() == "Test Buddy");
    assert(pack.frame_count(PetAction::idle) == 1 && pack.frame_count(PetAction::happy) == 2);
    assert(pack.interval_ms(PetAction::happy) == 120);
    assert(pack.bytes().size() == bytes.size());
    const auto good = bytes;
    for (const auto offset : {0U, 4U, 5U, 6U, 7U, 8U, 24U, 25U, 32U, 33U, 48U, 63U})
    {
        bytes = good;
        bytes[offset] = offset == 24 || offset == 32 ? 0 : 255;
        assert(!pack.decode(bytes));
        assert(pack.valid() && pack.name() == "Test Buddy" && pack.size() == good.size());
        assert(std::equal(pack.bytes().begin(), pack.bytes().end(), good.begin()));
    }
    bytes = good;
    bytes.pop_back();
    assert(!pack.decode(bytes));
    bytes = good;
    bytes.push_back(0);
    assert(!pack.decode(bytes));
    assert(!pack.decode({}));
    assert(!pack.decode(pack_bytes({4, 4, 4, 4, 4, 4, 4, 4})));
    assert(pack.decode(pack_bytes({4, 4, 4, 4, 4, 4, 0, 0})) && pack.size() == PetPack::max_bytes);
    assert(pack.frame(PetAction::count, 0).empty());
}

void custom_animation_and_pixels()
{
    PetPack pack;
    auto bytes = pack_bytes();
    bytes[64] = 0x80;
    bytes[64 + 255] = 0x01;
    bytes[64 + 256] = 0x40;
    bytes[64 + 512] = 0x20;
    assert(pack.decode(bytes));
    PetSession pet;
    assert(!pet.select(4, 0) && !pet.select(255, 0));
    pet.set_custom(&pack);
    assert(pet.has_custom() && pet.select(4, 0) && pet.name() == "Test Buddy");
    auto idle = draw(pet, 0);
    assert(idle[0] == 3 && idle[1] == 3 && idle[2] == 0);
    assert(idle[1022] == 0xc0 && idle[1023] == 0xc0);
    pet.act(PetAction::happy, 500);
    assert(draw(pet, 500)[2] == 3 && draw(pet, 500)[4] == 0);
    assert(draw(pet, 620)[2] == 0 && draw(pet, 620)[4] == 3);
    pet.tick(739);
    assert(pet.action() == PetAction::happy);
    pet.tick(740);
    assert(pet.action() == PetAction::idle);
    pet.act(PetAction::sad, 800);
    assert(draw(pet, 800) == idle);  // Missing actions display idle frames.
    pet.tick(920);
    assert(pet.action() == PetAction::idle);
    pet.act(PetAction::sleep, 1000);
    pet.tick(90000);
    assert(pet.action() == PetAction::sleep && draw(pet, 90000) == idle);
    pet.set_custom(nullptr);
    assert(pet.style() == 0 && !pet.has_custom());
}

void interaction_and_clock_wrap()
{
    PetSession pet;
    pet.tick(0);
    pet.handle(key(Key::up, Gesture::pressed, 10));
    assert(pet.style() == 3);
    pet.handle(key(Key::down, Gesture::pressed, 20));
    assert(pet.style() == 0);
    pet.handle(key(Key::enter, Gesture::click, 30));
    assert(pet.action() == PetAction::idle);  // A menu confirmation cannot leak into the pet.
    pet.handle(key(Key::enter, Gesture::pressed, 100));
    pet.handle(key(Key::enter, Gesture::click, 400));
    assert(pet.action() == PetAction::happy);
    pet.handle(key(Key::enter, Gesture::pressed, 500));
    pet.handle(key(Key::enter, Gesture::double_click, 700));
    assert(pet.action() == PetAction::eat);
    pet.handle(key(Key::enter, Gesture::pressed, 800));
    pet.handle(key(Key::enter, Gesture::long_press, 1450));
    assert(pet.action() == PetAction::sleep);
    pet.tick(100000);
    assert(pet.action() == PetAction::sleep);
    pet.handle(key(Key::enter, Gesture::pressed, 100010));
    pet.handle(key(Key::enter, Gesture::long_press, 100660));
    assert(pet.action() == PetAction::idle);  // The waking hold must not send it back to sleep.
    pet.handle(key(Key::jump, Gesture::pressed, 100700));
    assert(pet.action() == PetAction::dance);
    pet.handle(key(Key::function, Gesture::pressed, 100800));
    assert(pet.action() == PetAction::surprised);
    pet.tick(101800);
    assert(pet.action() == PetAction::idle);
    pet.tick(160800);
    assert(pet.action() == PetAction::sleep);
    pet.handle(key(Key::left, Gesture::pressed, 160900));
    assert(pet.action() == PetAction::idle);
    constexpr uint32_t start = 0xffffff00U;
    pet.act(PetAction::happy, start);
    const auto sequence = pet.sequence();
    pet.tick(uint32_t(start + 1399));
    assert(pet.action() == PetAction::happy);
    pet.tick(uint32_t(start + 1400));
    assert(pet.action() == PetAction::idle && pet.sequence() == sequence + 1);
    pet.tick(uint32_t(start + 60000));
    assert(pet.action() == PetAction::sleep);
}

void builtins_and_full_screen_lifecycle()
{
    PetSession pet;
    std::array<Frame, 4> styles{};
    for (unsigned style = 0; style < 4; ++style)
    {
        assert(pet.select(uint8_t(style), 0));
        styles[style] = draw(pet, 0);
        assert(std::any_of(styles[style].begin(), styles[style].end(), [](auto b) { return b != 0; }));
        for (unsigned action = 1; action < unsigned(PetAction::count); ++action)
        {
            pet.act(PetAction(action), 0);
            assert(draw(pet, 0) != styles[style]);
        }
        for (unsigned previous = 0; previous < style; ++previous)
            assert(styles[style] != styles[previous]);
    }
    Model model;
    model.open(View::pet);
    assert(!model.booting() && model.view() == View::pet && !model.gaming());
    assert(model.motion().page_x.settled() && model.game_index() == 255);
    assert(menu(View::home).back().view == View::pet);
    Frame frame{};
    render(frame, model, {}, 0);
    assert(frame == draw(model.pet(), 0));
    model.handle(key(Key::enter, Gesture::pressed, 10));
    model.handle(key(Key::enter, Gesture::long_press, 660));
    assert(model.view() == View::pet && model.pet().action() == PetAction::sleep);
    model.handle(key(Key::back, Gesture::pressed, 700));
    assert(model.view() == View::home && model.motion().page_x.settled());
    model.open(View::pet);
    assert(model.pet().action() == PetAction::idle);
    static_assert(unsigned(View::flappy) == 19 && unsigned(View::pet) == 20);
    static_assert(sizeof(PetSession) < 128);
}
}  // namespace

int main()
{
    pack_validation();
    custom_animation_and_pixels();
    interaction_and_clock_wrap();
    builtins_and_full_screen_lifecycle();
    std::puts("Desktop pet packs, animation, gestures, full-screen lifecycle and timer wrap passed");
}
