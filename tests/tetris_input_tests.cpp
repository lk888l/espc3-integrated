#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "c3/core.hpp"

using namespace c3;

namespace
{
Frame draw(const GameSession& game)
{
    Frame frame{};
    Canvas canvas(frame);
    game.draw(canvas);
    return frame;
}

struct Input
{
    explicit Input(uint32_t origin = 0) : origin(origin)
    {
        game.reset(GameId::tetris, 2);  // A T piece exposes accidental double rotation.
        sample(0, 0);
    }

    void sample(uint8_t raw, uint32_t elapsed)
    {
        std::array<KeyEvent, 16> output{};
        const auto count =
            bank.sample(raw, uint32_t(origin + elapsed), output, tetris_double_click_ms);
        for (std::size_t i = 0; i < count; ++i)
        {
            events.push_back(output[i]);
            game.handle(output[i]);
        }
    }

    void press(uint32_t elapsed)
    {
        sample(mask(Key::enter), elapsed);
        sample(mask(Key::enter), elapsed + 20);
    }

    void release(uint32_t elapsed)
    {
        sample(0, elapsed);
        sample(0, elapsed + 20);
    }

    void tap(uint32_t elapsed)
    {
        press(elapsed);
        release(elapsed + 40);
    }

    void start()
    {
        press(40);
        assert(game.status() == GameStatus::playing);
        const auto initial = draw(game);
        release(80);
        sample(0, 380);
        assert(draw(game) == initial);  // The READY tap's delayed click is consumed.
        events.clear();
    }

    GameSession game;
    ButtonBank bank;
    uint32_t origin;
    std::vector<KeyEvent> events;
};

void rotate_clockwise(GameSession& game)
{
    // The extended reverse-rotation key provides an independent reference path.
    for (unsigned i = 0; i < 3; ++i)
        game.handle({Key::jump, Gesture::pressed, i, 0});
}

void hard_drop(GameSession& game)
{
    game.handle({Key::function, Gesture::pressed, 0, 0});
}

void single_click_tests()
{
    Input input;
    input.start();
    auto expected = input.game;
    const auto initial = draw(input.game);
    input.press(500);
    assert(draw(input.game) == initial);
    input.release(540);
    assert(draw(input.game) == initial);
    input.sample(0, 709);  // One millisecond before the double-click window closes.
    assert(draw(input.game) == initial);
    input.sample(0, 710);
    rotate_clockwise(expected);
    assert(draw(input.game) != initial && draw(input.game) == draw(expected));
    input.sample(0, 1000);
    assert(draw(input.game) == draw(expected));
    assert(input.events.size() == 3 && input.events.back().gesture == Gesture::click);
}

void double_click_tests(uint32_t origin = 0)
{
    Input input(origin);
    input.start();
    auto expected = input.game;
    const auto initial = draw(input.game);
    input.press(500);
    assert(draw(input.game) == initial);
    input.release(540);
    assert(draw(input.game) == initial);
    input.press(600);
    assert(draw(input.game) == initial);
    input.release(640);
    hard_drop(expected);
    assert(input.game.score() > 0 && input.game.score() == expected.score());
    assert(draw(input.game) == draw(expected));
    input.sample(0, 1000);
    assert(draw(input.game) == draw(expected));
    assert(input.events.size() == 5 && input.events.back().gesture == Gesture::double_click);
}

void pause_and_resume_tests()
{
    Input input;
    input.start();
    const auto initial = draw(input.game);
    input.press(500);
    assert(draw(input.game) == initial);
    input.sample(mask(Key::enter), 1169);
    assert(input.game.status() == GameStatus::playing && draw(input.game) == initial);
    input.sample(mask(Key::enter), 1170);
    assert(input.game.status() == GameStatus::paused);
    input.release(1200);
    input.sample(0, 1520);
    assert(input.game.status() == GameStatus::paused && input.events.size() == 3);
    assert(input.events[1].gesture == Gesture::long_press);

    input.press(1600);
    assert(input.game.status() == GameStatus::playing && draw(input.game) == initial);
    input.release(1640);
    input.sample(0, 1940);
    assert(draw(input.game) == initial);  // Resume's click cannot rotate.

    input.tap(2100);
    input.sample(0, 2440);
    auto expected = input.game;
    rotate_clockwise(expected);
    input.tap(2600);
    input.sample(0, 2940);
    assert(draw(input.game) == draw(expected));
}

void cross_state_double_click_tests()
{
    Input input;
    input.press(40);  // First half starts the game.
    const auto initial = draw(input.game);
    input.release(80);
    input.tap(140);
    assert(input.game.score() == 0 && draw(input.game) == initial);
    input.sample(0, 500);
    assert(draw(input.game) == initial);

    input.press(600);
    input.sample(mask(Key::enter), 1270);
    assert(input.game.status() == GameStatus::paused);
    input.release(1300);
    input.tap(1400);  // A double-click that begins by resuming is also ignored.
    input.tap(1500);
    assert(input.game.status() == GameStatus::playing && input.game.score() == 0);
    assert(draw(input.game) == initial);

    auto expected = input.game;
    input.tap(1800);
    input.tap(1900);
    hard_drop(expected);
    assert(draw(input.game) == draw(expected) && input.game.score() == expected.score());
}

void pending_start_click_during_press_tests()
{
    Input input;
    input.tap(40);  // The READY release at 100 leaves an ineligible click pending.
    const auto initial = draw(input.game);
    input.press(200);
    input.sample(mask(Key::enter), 250);  // Old click arrives while this press is held.
    assert(input.events.back().gesture == Gesture::click && draw(input.game) == initial);
    input.release(300);
    input.sample(0, 470);
    // Compare against the initial orientation rather than rotating the result itself.
    Input reference;
    reference.start();
    rotate_clockwise(reference.game);
    assert(draw(input.game) == draw(reference.game) && draw(input.game) != initial);
    assert(input.game.score() == 0);
}

void pending_start_click_at_second_release_tests()
{
    Input input;
    input.tap(40);
    const auto initial = draw(input.game);
    input.press(200);
    input.sample(0, 230);
    const auto before_release = input.events.size();
    // A delayed scan emits the second release and the expired first click together.
    input.sample(0, 260);
    assert(input.events.size() == before_release + 2);
    assert(input.events[before_release].gesture == Gesture::released);
    assert(input.events[before_release + 1].gesture == Gesture::click);
    assert(draw(input.game) == initial);
    input.sample(0, 410);
    Input reference;
    reference.start();
    rotate_clockwise(reference.game);
    assert(draw(input.game) == draw(reference.game));
}

void consecutive_gesture_tests()
{
    Input input;
    input.start();
    auto expected = input.game;
    for (const uint32_t elapsed : {500U, 900U})
    {
        input.tap(elapsed);
        input.sample(0, elapsed + 340);
        rotate_clockwise(expected);
        assert(draw(input.game) == draw(expected));
    }
    for (const uint32_t elapsed : {1400U, 1900U})
    {
        input.tap(elapsed);
        input.tap(elapsed + 100);
        hard_drop(expected);
        assert(draw(input.game) == draw(expected) && input.game.score() == expected.score());
        input.sample(0, elapsed + 450);
        assert(draw(input.game) == draw(expected));
    }
    input.tap(2400);
    input.sample(0, 2740);
    rotate_clockwise(expected);
    assert(draw(input.game) == draw(expected));
}

void enter_window_only_tests()
{
    ButtonBank bank;
    std::array<KeyEvent, 16> events{};
    const auto sample = [&](uint8_t raw, uint32_t now)
    { return bank.sample(raw, now, events, tetris_double_click_ms); };
    constexpr auto keys = mask(Key::enter) | mask(Key::function);
    assert(sample(keys, 0) == 0);
    assert(sample(keys, 20) == 2);
    assert(sample(0, 40) == 0);
    assert(sample(0, 60) == 2);
    assert(sample(0, 209) == 0);
    assert(sample(0, 210) == 1);
    assert(events[0].key == Key::enter && events[0].gesture == Gesture::click);
    assert(sample(0, 339) == 0);
    assert(sample(0, 340) == 1);
    assert(events[0].key == Key::function && events[0].gesture == Gesture::click);
}

void pending_window_change_tests()
{
    // Changing pages must preserve the window chosen by an existing release.
    struct Windows
    {
        uint16_t initial, changed, expected;
    };
    constexpr std::array<Windows, 2> windows = {
        Windows{0, tetris_double_click_ms, 280}, Windows{tetris_double_click_ms, 0, 150}};
    for (const auto window : windows)
    {
        ButtonBank bank;
        std::array<KeyEvent, 16> events{};
        assert(bank.sample(mask(Key::enter), 0, events, window.initial) == 0);
        assert(bank.sample(mask(Key::enter), 20, events, window.initial) == 1);
        assert(bank.sample(0, 40, events, window.initial) == 0);
        assert(bank.sample(0, 60, events, window.initial) == 1);
        assert(bank.sample(0, 100, events, window.changed) == 0);
        assert(bank.sample(0, 60 + window.expected - 1, events, window.changed) == 0);
        assert(bank.sample(0, 60 + window.expected, events, window.changed) == 1);
        assert(events[0].key == Key::enter && events[0].gesture == Gesture::click);
        assert(bank.sample(0, 500, events, window.changed) == 0);
    }

    // A second release outside 150 ms still belongs to the original 280 ms window.
    ButtonBank bank;
    std::array<KeyEvent, 16> events{};
    assert(bank.sample(mask(Key::enter), 0, events) == 0);
    assert(bank.sample(mask(Key::enter), 20, events) == 1);
    assert(bank.sample(0, 40, events) == 0);
    assert(bank.sample(0, 60, events) == 1);
    assert(bank.sample(mask(Key::enter), 200, events, tetris_double_click_ms) == 0);
    assert(bank.sample(mask(Key::enter), 220, events, tetris_double_click_ms) == 1);
    assert(events[0].gesture == Gesture::pressed);
    assert(bank.sample(0, 240, events, tetris_double_click_ms) == 0);
    assert(bank.sample(0, 260, events, tetris_double_click_ms) == 2);
    assert(events[0].gesture == Gesture::released);
    assert(events[1].gesture == Gesture::double_click);
    assert(bank.sample(0, 600, events, tetris_double_click_ms) == 0);
}
}  // namespace

int main()
{
    // Run the original bug first so an unpatched engine fails at its first press.
    double_click_tests();
    double_click_tests(0xfffffd80U);  // The double-click crosses uint32_t time wrap.
    single_click_tests();
    pause_and_resume_tests();
    cross_state_double_click_tests();
    pending_start_click_during_press_tests();
    pending_start_click_at_second_release_tests();
    consecutive_gesture_tests();
    enter_window_only_tests();
    pending_window_change_tests();
    std::puts("Tetris physical-button gesture tests passed");
}
