#pragma once

#include <array>
#include <cstdint>

#include "c3/games.hpp"

namespace c3
{
class Canvas;
struct KeyEvent;

// Two original games, kept independent from the imported GameBox activities.
// All simulation data is bounded and lives inside the session.
class BonusSession
{
   public:
    void reset(bool flappy, uint32_t seed);
    void tick(uint32_t now);
    void handle(KeyEvent event);
    void draw(Canvas& canvas, int x = 0) const;
    GameStatus status() const { return status_; }
    uint32_t score() const { return score_; }
    uint8_t lives() const { return lives_; }

   private:
    struct Breakout
    {
        int16_t ball_x = 64 * 256, ball_y = 56 * 256;
        int16_t vx = 144, vy = -208;
        uint16_t bricks = 0x7fff;
        uint8_t paddle_x = 53, level = 0;
        bool serving = true;
    } breakout_;
    struct Pipe
    {
        int16_t x{};  // Q12.4: subpixel movement without floating point.
        uint8_t gap{};
        bool passed{};
    };
    struct Bird
    {
        std::array<Pipe, 3> pipes{};
        int16_t y = 35 * 256, velocity = 0;
        uint8_t frame = 0;
    } bird_;
    uint32_t random();
    void restart();
    void launch();
    void step_breakout();
    void step_flappy();
    void draw_overlay(Canvas& canvas, int x) const;
    GameStatus status_ = GameStatus::ready;
    uint32_t rng_ = 0xA341316CU, seed_ = 0, score_ = 0, last_tick_ = 0;
    uint16_t remainder_ = 0;
    uint8_t lives_ = 3, held_ = 0;
    bool flappy_ = false, clock_armed_ = false;
};
static_assert(sizeof(BonusSession) <= 512);
}  // namespace c3
