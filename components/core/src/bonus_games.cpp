#include "c3/bonus_games.hpp"

#include <algorithm>
#include <cstdio>
#include <limits>

#include "c3/core.hpp"

namespace c3
{
namespace
{
constexpr auto set = PixelOp::set, clear = PixelOp::clear;
constexpr int bird_x = 28, pipe_width = 11, pipe_gap = 23;

void centered(Canvas& canvas, int x, int y, std::string_view text)
{
    canvas.text(x + (128 - int(text.size()) * 6) / 2, y, text);
}

void add_score(uint32_t& score, uint32_t points)
{
    score += std::min(points, std::numeric_limits<uint32_t>::max() - score);
}
}  // namespace

uint32_t BonusSession::random()
{
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
}

void BonusSession::reset(bool flappy, uint32_t seed)
{
    flappy_ = flappy;
    seed_ = seed;
    restart();
}

void BonusSession::restart()
{
    rng_ = seed_ ? seed_ : 0xA341316CU;
    score_ = 0;
    lives_ = flappy_ ? 1 : 3;
    status_ = GameStatus::ready;
    held_ = 0;
    clock_armed_ = false;
    remainder_ = 0;
    breakout_ = {};
    bird_ = {};
    for (unsigned i = 0; i < bird_.pipes.size(); ++i)
        bird_.pipes[i] = {int16_t((100 + 45 * i) * 16), uint8_t(18 + random() % 17), false};
}

void BonusSession::launch()
{
    breakout_.serving = false;
    const int speed = std::min<int>(breakout_.level, 6) * 16;
    breakout_.vx = int16_t((random() & 1 ? 1 : -1) * (144 + speed / 2));
    breakout_.vy = int16_t(-208 - speed);
}

void BonusSession::handle(KeyEvent event)
{
    const auto bit = mask(event.key);
    if (event.gesture == Gesture::released)
    {
        held_ &= uint8_t(~bit);
        return;
    }
    if (event.gesture == Gesture::long_press && event.key == Key::enter)
    {
        if (status_ == GameStatus::playing)
        {
            status_ = GameStatus::paused;
            held_ = 0;
            clock_armed_ = false;
        }
        return;
    }
    const bool pressed = event.gesture == Gesture::pressed;
    const bool repeated = (event.gesture == Gesture::repeat ||
                           event.gesture == Gesture::long_press) && (held_ & bit);
    if (!pressed && !repeated) return;
    if (pressed) held_ |= bit;

    if (status_ == GameStatus::ready)
    {
        if (event.key == Key::enter && pressed)
        {
            status_ = GameStatus::playing;
            clock_armed_ = false;
            if (flappy_)
                bird_.velocity = -240;
            else
                launch();
        }
        return;
    }
    if (status_ == GameStatus::paused)
    {
        if (pressed && event.key == Key::up) restart();
        else if (pressed && event.key == Key::enter)
        {
            status_ = GameStatus::playing;
            clock_armed_ = false;
        }
        return;
    }
    if (status_ == GameStatus::game_over)
    {
        if (pressed && event.key == Key::enter) restart();
        return;
    }

    if (flappy_)
    {
        if (pressed && (event.key == Key::enter || event.key == Key::up ||
                        event.key == Key::jump))
            bird_.velocity = -240;
    }
    else
    {
        int direction = 0;
        if (event.key == Key::up || event.key == Key::left) direction = -1;
        if (event.key == Key::down || event.key == Key::right) direction = 1;
        if (direction)
        {
            breakout_.paddle_x = uint8_t(std::clamp(int(breakout_.paddle_x) + direction * 9,
                                                   1, 105));
            if (breakout_.serving)
                breakout_.ball_x = int16_t((breakout_.paddle_x + 11) * 256);
        }
        if (pressed && event.key == Key::enter && breakout_.serving) launch();
    }
}

void BonusSession::tick(uint32_t now)
{
    if (status_ != GameStatus::playing)
    {
        clock_armed_ = false;
        return;
    }
    if (!clock_armed_)
    {
        last_tick_ = now;
        clock_armed_ = true;
        return;
    }
    // Bound catch-up work after a blocked display/network task; a pause never
    // advances physics by the time spent outside the game.
    remainder_ += uint16_t(std::min<uint32_t>(now - last_tick_, 128));
    last_tick_ = now;
    while (remainder_ >= 16 && status_ == GameStatus::playing)
    {
        remainder_ -= 16;
        if (flappy_)
            step_flappy();
        else
            step_breakout();
    }
}

void BonusSession::step_breakout()
{
    auto& b = breakout_;
    if (b.serving) return;
    const int old_x = b.ball_x / 256, old_y = b.ball_y / 256;
    int next_x = b.ball_x + b.vx, next_y = b.ball_y + b.vy;
    if (next_x < 2 * 256)
    {
        next_x = 4 * 256 - next_x;
        b.vx = int16_t(-b.vx);
    }
    else if (next_x > 125 * 256)
    {
        next_x = 250 * 256 - next_x;
        b.vx = int16_t(-b.vx);
    }
    if (next_y < 13 * 256)
    {
        next_y = 26 * 256 - next_y;
        b.vy = int16_t(-b.vy);
    }
    int bx = next_x / 256, by = next_y / 256;
    bool hit = false;
    for (unsigned row = 0; row < 3 && !hit; ++row)
        for (unsigned col = 0; col < 5 && !hit; ++col)
        {
            const auto brick = uint16_t(1U << (row * 5 + col));
            if (!(b.bricks & brick)) continue;
            const int left = 4 + int(col) * 24, top = 15 + int(row) * 6;
            if (bx + 1 < left || bx - 1 >= left + 22 || by + 1 < top || by - 1 >= top + 4)
                continue;
            b.bricks &= uint16_t(~brick);
            add_score(score_, (3 - row) * 10);
            if (old_y + 1 < top || old_y - 1 >= top + 4)
            {
                b.vy = int16_t(-b.vy);
                next_y = b.ball_y;
            }
            else if (old_x + 1 < left || old_x - 1 >= left + 22)
            {
                b.vx = int16_t(-b.vx);
                next_x = b.ball_x;
            }
            else
            {
                b.vy = int16_t(-b.vy);
                next_y = b.ball_y;
            }
            hit = true;
        }
    bx = next_x / 256;
    by = next_y / 256;
    if (b.vy > 0 && old_y < 58 && by >= 58 &&
        bx + 1 >= b.paddle_x && bx - 1 < b.paddle_x + 22)
    {
        next_y = 57 * 256;
        const int offset = bx - (b.paddle_x + 11);
        b.vx = int16_t(std::clamp(offset * 24, -240, 240));
        if (b.vx > -64 && b.vx < 64) b.vx = b.vx < 0 ? -64 : 64;
        b.vy = int16_t(-224 - std::min<int>(b.level, 6) * 16);
    }
    b.ball_x = int16_t(next_x);
    b.ball_y = int16_t(next_y);
    if (by > 63)
    {
        if (--lives_ == 0) status_ = GameStatus::game_over;
        else
        {
            b.serving = true;
            b.ball_x = int16_t((b.paddle_x + 11) * 256);
            b.ball_y = 56 * 256;
        }
    }
    else if (b.bricks == 0)
    {
        if (b.level < 255) ++b.level;
        b.bricks = 0x7fff;
        b.serving = true;
        b.ball_x = int16_t((b.paddle_x + 11) * 256);
        b.ball_y = 56 * 256;
        add_score(score_, 100);
    }
}

void BonusSession::step_flappy()
{
    auto& b = bird_;
    ++b.frame;
    b.velocity = int16_t(std::min<int>(b.velocity + 15, 384));
    b.y = int16_t(b.y + b.velocity);
    const int y = b.y / 256;
    if (y - 2 < 12 || y + 2 >= 62)
    {
        status_ = GameStatus::game_over;
        lives_ = 0;
        return;
    }
    const int speed = 9 + int(std::min<uint32_t>(score_ / 8, 4));
    for (auto& pipe : b.pipes)
    {
        pipe.x = int16_t(pipe.x - speed);
        const int px = pipe.x / 16;
        if (bird_x + 2 >= px && bird_x - 2 < px + pipe_width &&
            (y - 2 < pipe.gap || y + 2 >= pipe.gap + pipe_gap))
        {
            status_ = GameStatus::game_over;
            lives_ = 0;
            return;
        }
        if (!pipe.passed && px + pipe_width < bird_x - 2)
        {
            pipe.passed = true;
            add_score(score_, 1);
        }
    }
    for (auto& pipe : b.pipes)
        if (pipe.x < -pipe_width * 16)
        {
            int furthest = -pipe_width * 16;
            for (const auto& other : b.pipes) furthest = std::max<int>(furthest, other.x);
            pipe = {int16_t(furthest + 45 * 16), uint8_t(18 + random() % 17), false};
        }
}

void BonusSession::draw(Canvas& canvas, int x) const
{
    char value[12];
    canvas.text(x + 2, 1, flappy_ ? "FLAPPY" : "BREAKOUT");
    std::snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(score_));
    // Six digits keep the title and score legible on the 128-pixel display.
    std::string_view score_text(value);
    if (score_text.size() > 6) score_text.remove_prefix(score_text.size() - 6);
    canvas.text(x + 126 - int(score_text.size()) * 6, 1, score_text);
    canvas.line(x, 10, x + 127, 10);
    if (flappy_)
    {
        for (const auto& pipe : bird_.pipes)
        {
            const int px = x + pipe.x / 16;
            canvas.rect(px, 12, pipe_width, pipe.gap - 12);
            canvas.rect(px - 1, pipe.gap - 3, pipe_width + 2, 3);
            const int bottom = pipe.gap + pipe_gap;
            canvas.rect(px, bottom, pipe_width, 62 - bottom);
            canvas.rect(px - 1, bottom, pipe_width + 2, 3);
        }
        const int y = std::clamp(int(bird_.y / 256), 14, 59);
        canvas.rounded(x + bird_x - 3, y - 2, 7, 5, set, true);
        canvas.pixel(x + bird_x + 1, y - 1, clear);
        canvas.line(x + bird_x - 3, y + (bird_.frame & 8 ? 0 : 2),
                    x + bird_x - 5, y + (bird_.frame & 8 ? -1 : 3));
        canvas.pixel(x + bird_x + 4, y);
        canvas.line(x, 62, x + 127, 62);
        for (int i = -8; i < 128; i += 8)
            canvas.pixel(x + i + ((bird_.frame / 2) & 7), 63);
    }
    else
    {
        for (unsigned row = 0; row < 3; ++row)
            for (unsigned col = 0; col < 5; ++col)
                if (breakout_.bricks & (1U << (row * 5 + col)))
                {
                    const int bx = x + 4 + int(col) * 24, by = 15 + int(row) * 6;
                    canvas.rect(bx, by, 22, 4, set, row != 1);
                }
        canvas.line(x, 12, x, 63);
        canvas.line(x + 127, 12, x + 127, 63);
        canvas.rounded(x + breakout_.paddle_x, 59, 22, 3, set, true);
        canvas.rect(x + breakout_.ball_x / 256 - 1,
                    std::min<int>(breakout_.ball_y / 256 - 1, 61), 3, 3, set, true);
        for (unsigned i = 0; i < lives_; ++i) canvas.rect(x + 3 + int(i) * 4, 62, 2, 2, set, true);
        if (status_ == GameStatus::playing && breakout_.serving)
            centered(canvas, x, 42, "ENTER: SERVE");
    }
    if (status_ != GameStatus::playing) draw_overlay(canvas, x);
}

void BonusSession::draw_overlay(Canvas& canvas, int x) const
{
    canvas.rounded(x + 4, 15, 120, 48, clear, true);
    canvas.rounded(x + 4, 15, 120, 48);
    if (status_ == GameStatus::ready)
    {
        centered(canvas, x, 19, "ENTER: START");
        centered(canvas, x, 29, flappy_ ? "UP / ENTER: FLAP" : "UP/DOWN: MOVE");
        centered(canvas, x, 39, "HOLD ENTER: PAUSE");
    }
    else if (status_ == GameStatus::paused)
    {
        centered(canvas, x, 19, "PAUSED");
        centered(canvas, x, 29, "ENTER: RESUME");
        centered(canvas, x, 39, "UP: RESTART");
    }
    else
    {
        centered(canvas, x, 23, "GAME OVER");
        centered(canvas, x, 39, "ENTER: RESTART");
    }
    centered(canvas, x, 51, "BACK: EXIT");
}
}  // namespace c3
