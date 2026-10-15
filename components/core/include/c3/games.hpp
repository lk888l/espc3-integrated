#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace c3
{
class Canvas;
struct KeyEvent;

enum class GameId : uint8_t
{
    dino,
    snake,
    air_raid,
    tetris,
    pong,
    piano,
    count
};
enum class GameStatus : uint8_t
{
    ready,
    playing,
    paused,
    game_over
};
std::string_view game_title(GameId id);
std::string_view game_subtitle(GameId id);

// Fixed-size, hardware-independent adaptations of STM-GameBox's six activities.
// Up/Down are the two movement keys; hold Enter to pause any activity.
class GameSession
{
   public:
    GameSession();
    void reset(GameId id, uint32_t seed);
    void tick(uint32_t now);
    void handle(KeyEvent event);
    void draw(Canvas& canvas, int x = 0) const;
    GameId id() const { return id_; }
    GameStatus status() const { return status_; }
    uint32_t score() const { return score_; }
    uint8_t lives() const { return id_ == GameId::air_raid ? air_.lives : 0; }
    uint16_t lines() const { return tetris_.lines; }
    uint8_t tetris_rotation() const { return tetris_.rotation; }
    uint8_t tetris_piece() const { return tetris_.piece; }
    uint8_t winner() const { return pong_.winner; }
    uint8_t note() const { return piano_.note; }
    uint32_t note_sequence() const { return note_sequence_; }
    bool note_playing() const { return note_sounding_; }
    uint16_t note_frequency() const;

   private:
    struct Point
    {
        uint8_t x{}, y{};
    };
    struct Obstacle
    {
        int16_t x{};
        uint8_t width{}, height{};
        bool active{};
    };
    struct Entity
    {
        int16_t x{}, y{};
        bool active{};
    };
    struct Snake
    {
        std::array<Point, 96> body{};
        Point food{};
        uint8_t length = 3, direction = 1, requested = 1;
    } snake_;
    struct Dino
    {
        std::array<Obstacle, 3> obstacles{};
        uint16_t spawn_steps = 18;
        uint8_t jump_frame{};
    } dino_;
    struct Air
    {
        std::array<Entity, 3> bullets{}, enemies{};
        int16_t player_y = 32;
        uint16_t spawn_steps = 10;
        uint8_t lives = 3;
    } air_;
    struct Tetris
    {
        std::array<uint16_t, 15> rows{};
        uint16_t lines{};
        int8_t x = 3, y = -1;
        uint8_t piece{}, next{}, rotation{};
    } tetris_;
    struct Pong
    {
        int16_t ball_x = 64, ball_y = 34, left_y = 36, right_y = 36;
        int8_t vx = 1, vy = 1;
        uint8_t left_half = 8, right_half = 8, winner{}, ai_frame{};
        bool right_manual = false;
    } pong_;
    struct Piano
    {
        uint8_t note{};
    } piano_;
    uint32_t random();
    uint32_t bounded(uint32_t upper);
    uint32_t period() const;
    void start();
    void restart();
    void step();
    void jump();
    void fire();
    void play_note();
    void place_food();
    bool snake_occupies(Point point, unsigned limit) const;
    void step_snake();
    void step_dino();
    void step_air();
    void step_pong();
    void reset_ball(int direction);
    static bool piece_cell(uint8_t piece, uint8_t rotation, int x, int y);
    bool tetris_fits(int x, int y, uint8_t rotation) const;
    bool tetris_active(int x, int y) const;
    void tetris_move(int direction);
    void tetris_rotate(int direction);
    bool route_tetris_enter(KeyEvent event);
    bool tetris_down();
    void tetris_lock();
    void tetris_spawn();
    void draw_overlay(Canvas& canvas, int x) const;
    GameId id_ = GameId::dino;
    GameStatus status_ = GameStatus::ready;
    uint32_t rng_ = 0xA341316CU, seed_ = 0, score_ = 0, now_ = 0, next_step_ = 0;
    uint32_t note_sequence_ = 0, note_until_ = 0, scene_ms_ = 0;
    uint8_t held_ = 0, enter_release_flags_ = 0, enter_release_count_ = 0;
    bool enter_eligible_ = false, enter_ignore_release_ = false;
    bool clock_armed_ = false, note_sounding_ = false;
};
}  // namespace c3
