#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <type_traits>

#include "c3/core.hpp"

using namespace c3;

namespace
{
constexpr std::array<View, 8> game_views = {View::dino, View::snake, View::air_raid, View::tetris,
                                          View::pong, View::piano, View::breakout, View::flappy};

KeyEvent event(Key key, uint32_t now, Gesture gesture = Gesture::pressed)
{
    return {key, gesture, now, gesture == Gesture::long_press ? 650U : 0U};
}

Frame draw(const Model& model, uint32_t now)
{
    Frame frame{};
    render(frame, model, {}, now);
    return frame;
}

template <typename Session>
Frame draw(const Session& session, int offset = 0)
{
    Frame frame{};
    Canvas canvas(frame);
    session.draw(canvas, offset);
    return frame;
}

void menu_and_lifecycle_tests()
{
    Settings settings;
    settings.motion = Motion::off;
    Model model(settings);
    uint32_t now = 0;
    model.tick(now);
    model.tick(now = 600);
    assert(menu(View::home).size() == 6 && menu(View::home)[0].view == View::games);
    assert(menu(View::games).size() == game_views.size());
    assert(model.game_index() == 255);
    model.handle(event(Key::enter, ++now));
    assert(model.view() == View::games && !model.gaming());

    for (unsigned index = 0; index < game_views.size(); ++index)
    {
        while (model.selected(View::games) != index) model.handle(event(Key::down, ++now));
        model.handle(event(Key::enter, ++now));
        assert(model.view() == game_views[index] && model.game_index() == index);
        assert(model.game_status() == GameStatus::ready && model.game_steps() == 0);
        // Delayed gestures from selecting the menu must not start the new game.
        for (const auto gesture : {Gesture::released, Gesture::click, Gesture::double_click,
                                   Gesture::repeat})
            model.handle(event(Key::enter, ++now, gesture));
        assert(model.game_status() == GameStatus::ready && model.game_score() == 0);

        model.handle(event(Key::enter, ++now));
        assert(model.game_status() == GameStatus::playing);
        // A double-click begun on READY cannot hard-drop the first Tetris piece.
        model.handle(event(Key::enter, ++now, Gesture::released));
        model.handle(event(Key::enter, ++now, Gesture::double_click));
        assert(model.game_score() == 0);
        model.handle(event(Key::up, ++now));
        model.handle(event(Key::up, ++now, Gesture::released));
        model.tick(now += 5);
        model.tick(now += 100);
        assert(model.game_status() == GameStatus::playing && model.game_steps() > 0);
        model.handle(event(Key::enter, now += 650, Gesture::long_press));
        assert(model.view() == game_views[index] && model.game_status() == GameStatus::paused);
        model.tick(now += 5);
        const auto score = model.game_score(), steps = model.game_steps();
        const auto lives = model.game_lives();
        const auto paused = draw(model, now);
        assert(std::any_of(paused.begin(), paused.end(), [](auto byte) { return byte != 0; }));
        for (const auto gesture : {Gesture::released, Gesture::click, Gesture::repeat})
            model.handle(event(Key::enter, ++now, gesture));
        model.tick(now += 3000);
        assert(model.game_status() == GameStatus::paused && model.game_steps() == steps);
        assert(model.game_score() == score && model.game_lives() == lives);
        assert(draw(model, now) == paused);

        model.handle(event(Key::enter, ++now));
        assert(model.game_status() == GameStatus::playing);
        model.tick(now += 5);
        assert(model.game_steps() > steps);
        model.handle(event(Key::enter, ++now, Gesture::long_press));
        model.handle(event(Key::up, ++now));
        assert(model.view() == game_views[index] && model.game_status() == GameStatus::ready);
        assert(model.game_score() == 0);
        model.handle(event(Key::back, ++now));
        assert(model.view() == View::games && model.selected(View::games) == index);
        assert(model.game_index() == 255);
        model.handle(event(Key::enter, ++now, Gesture::released));
        model.handle(event(Key::enter, ++now, Gesture::click));
        assert(model.view() == View::games);
        model.handle(event(Key::enter, ++now));
        assert(model.view() == game_views[index] && model.game_status() == GameStatus::ready);
        assert(model.game_steps() == 0 && model.game_score() == 0);
        model.handle(event(Key::back, ++now));
    }
    model.handle(event(Key::back, ++now));
    assert(model.view() == View::home && model.selected(View::home) == 0);
}

void dino_and_snake_tests()
{
    GameSession dino;
    dino.reset(GameId::dino, 1);
    dino.handle(event(Key::enter, 0));
    dino.tick(0);
    // Jump just before the first obstacle reaches the dinosaur; clear it, then
    // stop jumping so a later obstacle exercises the collision and restart.
    for (unsigned step = 1; step <= 75; ++step)
    {
        dino.tick(step * 45);
        if (step == 49) dino.handle(event(Key::up, step * 45));
    }
    assert(dino.status() == GameStatus::playing && dino.score() == 1);
    for (unsigned step = 76; step < 200 && dino.status() == GameStatus::playing; ++step)
        dino.tick(step * 45);
    assert(dino.status() == GameStatus::game_over);
    const auto over = draw(dino);
    dino.tick(30000);
    assert(draw(dino) == over && dino.score() == 1);
    dino.handle(event(Key::enter, 30001));
    assert(dino.status() == GameStatus::ready && dino.score() == 0);

    GameSession snake;
    snake.reset(GameId::snake, 37);  // The first food lies on the initial route.
    snake.handle(event(Key::enter, 0));
    snake.tick(0);
    for (uint32_t now = 5; now < 5000 && snake.status() == GameStatus::playing; now += 5)
        snake.tick(now);
    assert(snake.score() == 1 && snake.status() == GameStatus::game_over);
    snake.handle(event(Key::enter, 5001));
    assert(snake.status() == GameStatus::ready && snake.score() == 0);
}

void air_and_pong_tests()
{
    GameSession air;
    air.reset(GameId::air_raid, 1);
    assert(air.lives() == 3);
    air.handle(event(Key::enter, 0));
    air.handle(event(Key::enter, 1));
    air.tick(1);
    for (unsigned step = 1; step < 1000 && air.status() == GameStatus::playing; ++step)
    {
        air.handle(event(Key::enter, step * 33, Gesture::repeat));
        air.tick(step * 33);
    }
    assert(air.score() > 0 && air.lives() == 0 && air.status() == GameStatus::game_over);

    GameSession pong;
    pong.reset(GameId::pong, 1);
    pong.handle(event(Key::enter, 0));
    pong.tick(0);
    for (unsigned step = 1; step < 10000 && pong.status() == GameStatus::playing; ++step)
        pong.tick(step * 22);
    assert(pong.status() == GameStatus::game_over && pong.winner() == 1 && pong.score() > 0);
}

void tetris_tests()
{
    GameSession game;
    game.reset(GameId::tetris, 30);  // Two horizontal I pieces followed by an O.
    game.handle(event(Key::enter, 0));
    game.handle(event(Key::enter, 1, Gesture::double_click));
    assert(game.score() == 0 && game.lines() == 0);
    uint32_t now = 1;
    for (const int shift : {-3, 1, 4})
    {
        for (int step = 0; step < (shift < 0 ? -shift : shift); ++step)
            game.handle(event(shift < 0 ? Key::up : Key::down, ++now));
        game.handle(event(Key::function, ++now));
    }
    assert(game.status() == GameStatus::playing && game.lines() == 1);
    assert(game.score() >= 100);

    // Real three-button double-click: both presses happen during this game.
    const auto score = game.score();
    for (unsigned click = 0; click < 2; ++click)
    {
        game.handle(event(Key::enter, ++now));
        game.handle(event(Key::enter, ++now, Gesture::released));
    }
    game.handle(event(Key::enter, ++now, Gesture::double_click));
    assert(game.score() > score);
    for (unsigned piece = 0; piece < 40 && game.status() == GameStatus::playing; ++piece)
        game.handle(event(Key::function, ++now));
    assert(game.status() == GameStatus::game_over);
    game.handle(event(Key::enter, ++now));
    assert(game.status() == GameStatus::ready && game.score() == 0 && game.lines() == 0);
}

void piano_tests()
{
    GameSession piano;
    piano.reset(GameId::piano, 1);
    constexpr uint32_t start = 0xffffff80U;
    piano.handle(event(Key::enter, start));
    piano.handle(event(Key::down, start + 1, Gesture::repeat));
    assert(piano.note() == 0 && piano.note_frequency() == 262);
    piano.handle(event(Key::up, start + 2));
    assert(piano.note() == 7 && piano.note_frequency() == 523);
    piano.handle(event(Key::up, start + 3, Gesture::released));
    piano.handle(event(Key::up, start + 4, Gesture::repeat));
    assert(piano.note() == 7);
    piano.handle(event(Key::down, start + 5));
    piano.handle(event(Key::enter, start + 6));
    assert(piano.note() == 0 && piano.score() == 1 && piano.note_sequence() == 1);
    assert(piano.note_playing());
    piano.tick(uint32_t(start + 185));
    assert(piano.note_playing());
    piano.tick(uint32_t(start + 186));
    assert(!piano.note_playing());
    piano.handle(event(Key::enter, uint32_t(start + 187), Gesture::click));
    assert(piano.score() == 1 && piano.note_sequence() == 1);
    piano.handle(event(Key::enter, uint32_t(start + 188)));
    piano.handle(event(Key::enter, uint32_t(start + 189), Gesture::long_press));
    assert(piano.status() == GameStatus::paused && !piano.note_playing());
    const auto sequence = piano.note_sequence();
    piano.handle(event(Key::up, uint32_t(start + 190)));
    assert(piano.status() == GameStatus::ready && piano.score() == 0);
    // The sound adapter consumes a monotonic sequence, including across reset.
    piano.handle(event(Key::enter, uint32_t(start + 191)));
    piano.handle(event(Key::enter, uint32_t(start + 192)));
    assert(piano.note_sequence() == sequence + 1 && piano.note_playing());
}

void bonus_gameplay_tests()
{
    BonusSession breakout;
    breakout.reset(false, 1);
    breakout.handle(event(Key::enter, 0));
    breakout.tick(0);
    for (unsigned frame = 1; frame < 3000 && breakout.status() == GameStatus::playing; ++frame)
    {
        breakout.tick(frame * 16);
        if (breakout.status() == GameStatus::playing)
            breakout.handle(event(Key::enter, frame * 16));  // Serve again after each miss.
    }
    assert(breakout.score() > 0 && breakout.lives() == 0);
    assert(breakout.status() == GameStatus::game_over);
    breakout.handle(event(Key::enter, 50001));
    assert(breakout.status() == GameStatus::ready && breakout.lives() == 3);

    BonusSession flappy;
    flappy.reset(true, 1);
    flappy.handle(event(Key::enter, 0));
    flappy.tick(0);
    unsigned frame = 0;
    for (frame = 1; frame <= 200 && flappy.score() == 0; ++frame)
    {
        flappy.tick(frame * 16);
        if (frame % 31 == 0) flappy.handle(event(Key::up, frame * 16));
    }
    assert(flappy.score() == 1 && flappy.status() == GameStatus::playing);
    for (; frame < 500 && flappy.status() == GameStatus::playing; ++frame)
        flappy.tick(frame * 16);
    assert(flappy.status() == GameStatus::game_over && flappy.lives() == 0);
    const auto over = draw(flappy);
    flappy.tick(30000);
    assert(flappy.score() == 1 && draw(flappy) == over);
    flappy.handle(event(Key::enter, 30001));
    assert(flappy.status() == GameStatus::ready && flappy.score() == 0);
}

template <typename Session>
void deterministic_replay(Session& first, Session& second)
{
    uint32_t now = 0xffffe000U, rng = 0xC3B04A11U;
    bool wrapped = false;
    constexpr std::array<Key, 7> keys = {Key::up, Key::down, Key::left, Key::right,
                                       Key::enter, Key::jump, Key::function};
    constexpr std::array<Gesture, 6> gestures = {
        Gesture::pressed, Gesture::released, Gesture::click,
        Gesture::repeat, Gesture::double_click, Gesture::long_press};
    for (unsigned iteration = 0; iteration < 6000; ++iteration)
    {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        const auto old_now = now;
        now += iteration % 257 == 0 ? 5000 : 1 + rng % 35;
        wrapped = wrapped || now < old_now;
        auto input = event(keys[rng % keys.size()], now, gestures[(rng >> 8) % gestures.size()]);
        if (first.status() != GameStatus::playing)
            input = event(first.status() == GameStatus::paused && (rng & 1) ? Key::up : Key::enter,
                          now);
        first.handle(input);
        second.handle(input);
        first.tick(now);
        second.tick(now);
        assert(first.status() == second.status() && first.score() == second.score());
        assert(first.lives() == second.lives());
        if constexpr (std::is_same_v<Session, GameSession>)
        {
            assert(first.note() == second.note() && first.note_sequence() == second.note_sequence());
            assert(first.note_playing() == second.note_playing() && first.lines() == second.lines());
            assert(first.winner() == second.winner());
        }
        if (iteration % 8 == 0)
        {
            constexpr int offsets[] = {-127, -64, 0, 64, 127};
            const int offset = offsets[(iteration / 8) % 5];
            assert(draw(first, offset) == draw(second, offset));
        }
    }
    assert(wrapped);
}

void deterministic_and_wrap_tests()
{
    for (unsigned id = 0; id < unsigned(GameId::count); ++id)
    {
        GameSession first, second;
        first.reset(GameId(id), 0x3210U + id);
        second.reset(GameId(id), 0x3210U + id);
        assert(draw(first) == draw(second));
        deterministic_replay(first, second);
    }
    for (const bool flappy : {false, true})
    {
        BonusSession first, second;
        first.reset(flappy, 0x12345678U);
        second.reset(flappy, 0x12345678U);
        assert(draw(first) == draw(second));
        deterministic_replay(first, second);
    }
}
}  // namespace

int main()
{
    menu_and_lifecycle_tests();
    dino_and_snake_tests();
    air_and_pong_tests();
    tetris_tests();
    piano_tests();
    bonus_gameplay_tests();
    deterministic_and_wrap_tests();
    std::puts("Eight-game lifecycle, gameplay, deterministic replay and timer wrap passed");
}
