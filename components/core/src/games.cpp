// Gameplay adapted from STM-GameBox (MIT); see third_party/STM-GameBox.LICENSE.
#include "c3/games.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "c3/core.hpp"

namespace c3
{
namespace
{
constexpr uint8_t jump_arc[] = {0, 5, 10, 14, 18, 21, 23, 24, 23, 21, 18, 14, 10, 5, 0};
// Four row-major 4x4 masks per tetromino: I, O, T, S, Z, J, L.
constexpr uint16_t masks[7][4] = {
    {0x00F0, 0x2222, 0x00F0, 0x2222}, {0x0066, 0x0066, 0x0066, 0x0066},
    {0x0072, 0x0262, 0x0270, 0x0232}, {0x0036, 0x0462, 0x0036, 0x0462},
    {0x0063, 0x0264, 0x0063, 0x0264}, {0x0071, 0x0226, 0x0470, 0x0322},
    {0x0074, 0x0622, 0x0170, 0x0223}};
constexpr std::string_view names[] = {"DINO", "SNAKE", "AIR RAID", "TETRIS", "PONG", "PIANO"};
constexpr std::string_view subtitles[] = {"Jump & survive", "Turn, eat & grow", "Dodge and fire",
                                          "Clear the lines", "Challenge the CPU", "Eight-note keyboard"};
constexpr auto set = PixelOp::set, clear = PixelOp::clear;
bool overlaps(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)
{
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}
void centered(Canvas& c, int x, int y, std::string_view text)
{
    c.text(x + (128 - int(text.size()) * 6) / 2, y, text);
}
void number(Canvas& c, int x, int y, uint32_t value)
{
    char text[12];
    std::snprintf(text, sizeof(text), "%lu", static_cast<unsigned long>(value));
    c.text(x, y, text);
}
}  // namespace

std::string_view game_title(GameId id)
{
    return unsigned(id) < unsigned(GameId::count) ? names[unsigned(id)] : "GAME";
}
std::string_view game_subtitle(GameId id)
{
    return unsigned(id) < unsigned(GameId::count) ? subtitles[unsigned(id)] : "";
}
GameSession::GameSession() { reset(GameId::dino, 0); }
uint32_t GameSession::random()
{
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
}
uint32_t GameSession::bounded(uint32_t upper) { return upper ? random() % upper : 0; }
void GameSession::reset(GameId id, uint32_t seed)
{
    id_ = unsigned(id) < unsigned(GameId::count) ? id : GameId::dino;
    status_ = GameStatus::ready;
    rng_ = seed_ = seed ? seed : 0xA341316CU;
    score_ = 0;
    scene_ms_ = 0;
    held_ = enter_release_flags_ = enter_release_count_ = 0;
    enter_eligible_ = enter_ignore_release_ = false;
    clock_armed_ = note_sounding_ = false;
    switch (id_)
    {
        case GameId::dino:
            dino_ = {};
            break;
        case GameId::snake:
            snake_.length = 3;
            snake_.direction = snake_.requested = 1;  // clockwise: up, right, down, left
            snake_.body[0] = {15, 6};
            snake_.body[1] = {14, 6};
            snake_.body[2] = {13, 6};
            place_food();
            break;
        case GameId::air_raid:
            air_ = {};
            break;
        case GameId::tetris:
            tetris_ = {};
            tetris_.next = uint8_t(bounded(7));
            tetris_spawn();
            break;
        case GameId::pong:
            pong_ = {};
            reset_ball(bounded(2) ? 1 : -1);
            break;
        case GameId::piano:
            piano_ = {};
            break;
        case GameId::count:
            break;
    }
}
void GameSession::start()
{
    status_ = GameStatus::playing;
    clock_armed_ = false;
    held_ = 0;
}
void GameSession::restart() { reset(id_, random() ^ now_ ^ seed_); }
uint32_t GameSession::period() const
{
    switch (id_)
    {
        case GameId::dino: return 45;
        case GameId::snake: return 150 - std::min<uint32_t>(score_ * 3, 80);
        case GameId::air_raid: return 33;
        case GameId::tetris: return 500 - std::min<uint32_t>(tetris_.lines / 8, 9) * 45;
        case GameId::pong: return 22;
        default: return 33;
    }
}
void GameSession::tick(uint32_t now)
{
    now_ = now;
    if (note_sounding_ && int32_t(now - note_until_) >= 0) note_sounding_ = false;
    if (status_ != GameStatus::playing || id_ == GameId::piano) return;
    scene_ms_ = now;
    if (!clock_armed_)
    {
        next_step_ = now + period();
        clock_armed_ = true;
    }
    unsigned catch_up = 0;
    while (status_ == GameStatus::playing && int32_t(now - next_step_) >= 0 && catch_up < 6)
    {
        step();
        next_step_ += period();
        ++catch_up;
    }
    if (catch_up == 6 && int32_t(now - next_step_) >= 0) next_step_ = now + period();
}
void GameSession::handle(KeyEvent event)
{
    now_ = event.at_ms;
    if (route_tetris_enter(event)) return;
    const auto key = event.key;
    const auto gesture = event.gesture;
    if (gesture == Gesture::released)
    {
        held_ &= uint8_t(~mask(key));
        return;
    }
    if (key == Key::enter && gesture == Gesture::long_press)
    {
        if (status_ == GameStatus::playing)
        {
            status_ = GameStatus::paused;
            held_ = 0;
            note_sounding_ = false;
        }
        return;
    }
    const bool pressed = gesture == Gesture::pressed;
    const bool repeat = gesture == Gesture::repeat && (held_ & mask(key));
    if (status_ == GameStatus::paused)
    {
        if (pressed && key == Key::enter) start();
        else if (pressed && key == Key::up) restart();
        return;
    }
    if (status_ == GameStatus::game_over)
    {
        if (pressed && key == Key::enter) restart();
        return;
    }
    if (status_ == GameStatus::ready)
    {
        if (pressed && (key == Key::enter || (id_ == GameId::dino && key == Key::jump))) start();
        return;
    }
    if (pressed)
    {
        held_ |= mask(key);
    }
    if (!pressed && !repeat && gesture != Gesture::double_click) return;
    switch (id_)
    {
        case GameId::dino:
            if (pressed && (key == Key::up || key == Key::enter || key == Key::jump)) jump();
            break;
        case GameId::snake:
            if (pressed && (key == Key::up || key == Key::left))
                snake_.requested = uint8_t((snake_.direction + 3) & 3);
            else if (pressed && (key == Key::down || key == Key::right))
                snake_.requested = uint8_t((snake_.direction + 1) & 3);
            break;
        case GameId::air_raid:
            if ((pressed || repeat) && (key == Key::enter || key == Key::jump)) fire();
            break;
        case GameId::tetris:
            if ((pressed || repeat) && (key == Key::up || key == Key::left)) tetris_move(-1);
            else if ((pressed || repeat) && (key == Key::down || key == Key::right)) tetris_move(1);
            else if (pressed && key == Key::function)
            {
                while (tetris_down()) score_ += 2;
                clock_armed_ = false;
            }
            else if (pressed && key == Key::jump) tetris_rotate(-1);
            break;
        case GameId::piano:
            if ((pressed || repeat) && (key == Key::up || key == Key::left))
                piano_.note = uint8_t((piano_.note + 7) & 7);
            else if ((pressed || repeat) && (key == Key::down || key == Key::right))
                piano_.note = uint8_t((piano_.note + 1) & 7);
            else if (pressed && (key == Key::enter || key == Key::jump)) play_note();
            break;
        case GameId::pong:
            if (pressed && (key == Key::left || key == Key::right)) pong_.right_manual = true;
            break;
        default: break;
    }
}
void GameSession::step()
{
    switch (id_)
    {
        case GameId::dino: step_dino(); break;
        case GameId::snake: step_snake(); break;
        case GameId::air_raid: step_air(); break;
        case GameId::tetris: (void)tetris_down(); break;
        case GameId::pong: step_pong(); break;
        default: break;
    }
}
void GameSession::jump()
{
    if (!dino_.jump_frame) dino_.jump_frame = 1;
}
void GameSession::step_dino()
{
    if (dino_.jump_frame && ++dino_.jump_frame >= std::size(jump_arc)) dino_.jump_frame = 0;
    if (dino_.spawn_steps) --dino_.spawn_steps;
    if (!dino_.spawn_steps)
    {
        for (auto& obstacle : dino_.obstacles)
        {
            if (obstacle.active) continue;
            const bool tall = bounded(3) == 0;
            obstacle = {127, uint8_t(tall ? 6 : 9), uint8_t(tall ? 15 : 9), true};
            break;
        }
        dino_.spawn_steps = uint16_t(19 + bounded(13) - std::min<uint32_t>(score_ / 10, 6));
    }
    const int speed = score_ < 25 ? 3 : (score_ < 70 ? 4 : 5);
    const int bottom = 58 - jump_arc[dino_.jump_frame];
    for (auto& obstacle : dino_.obstacles)
    {
        if (!obstacle.active) continue;
        obstacle.x -= speed;
        if (overlaps(12, bottom - 11, 10, 12, obstacle.x, 59 - obstacle.height,
                     obstacle.width, obstacle.height))
        {
            status_ = GameStatus::game_over;
            return;
        }
        if (obstacle.x + obstacle.width < 0)
        {
            obstacle.active = false;
            ++score_;
        }
    }
}
bool GameSession::snake_occupies(Point point, unsigned limit) const
{
    for (unsigned i = 0; i < limit; ++i)
        if (snake_.body[i].x == point.x && snake_.body[i].y == point.y) return true;
    return false;
}
void GameSession::place_food()
{
    unsigned candidate = bounded(30 * 12);
    for (unsigned attempt = 0; attempt < 30 * 12; ++attempt)
    {
        const Point point{uint8_t(candidate % 30), uint8_t(candidate / 30)};
        if (!snake_occupies(point, snake_.length))
        {
            snake_.food = point;
            return;
        }
        candidate = (candidate + 1) % (30 * 12);
    }
}
void GameSession::step_snake()
{
    snake_.direction = snake_.requested;
    int x = snake_.body[0].x, y = snake_.body[0].y;
    switch (snake_.direction)
    {
        case 0: --y; break;
        case 1: ++x; break;
        case 2: ++y; break;
        case 3: --x; break;
    }
    const Point next{uint8_t(x), uint8_t(y)};
    const bool ate = next.x == snake_.food.x && next.y == snake_.food.y;
    if (x < 0 || x >= 30 || y < 0 || y >= 12 ||
        snake_occupies(next, ate ? snake_.length : snake_.length - 1))
    {
        status_ = GameStatus::game_over;
        return;
    }
    if (ate && snake_.length < snake_.body.size()) ++snake_.length;
    for (unsigned i = snake_.length - 1; i > 0; --i) snake_.body[i] = snake_.body[i - 1];
    snake_.body[0] = next;
    if (ate)
    {
        ++score_;
        place_food();
    }
}
void GameSession::fire()
{
    for (auto& bullet : air_.bullets)
        if (!bullet.active)
        {
            bullet = {17, int16_t(air_.player_y + 3), true};
            return;
        }
}
void GameSession::step_air()
{
    const int direction = int(bool(held_ & (mask(Key::down) | mask(Key::right)))) -
                          int(bool(held_ & (mask(Key::up) | mask(Key::left))));
    air_.player_y = int16_t(std::clamp(int(air_.player_y) + direction * 2, 12, 56));
    if (air_.spawn_steps) --air_.spawn_steps;
    if (!air_.spawn_steps)
    {
        for (auto& enemy : air_.enemies)
            if (!enemy.active)
            {
                enemy = {127, int16_t(13 + bounded(43)), true};
                break;
            }
        air_.spawn_steps = uint16_t(14 + bounded(11) - std::min<uint32_t>(score_ / 8, 5));
    }
    for (auto& bullet : air_.bullets)
        if (bullet.active)
        {
            bullet.x += 4;
            if (bullet.x >= 128) bullet.active = false;
        }
    for (auto& enemy : air_.enemies)
    {
        if (!enemy.active) continue;
        enemy.x -= score_ < 20 ? 2 : 3;
        for (auto& bullet : air_.bullets)
            if (bullet.active && overlaps(bullet.x, bullet.y, 3, 2, enemy.x, enemy.y, 9, 7))
            {
                bullet.active = enemy.active = false;
                ++score_;
                break;
            }
        if (enemy.active && (enemy.x + 9 < 0 ||
                              overlaps(5, air_.player_y, 12, 7, enemy.x, enemy.y, 9, 7)))
        {
            enemy.active = false;
            if (--air_.lives == 0)
            {
                status_ = GameStatus::game_over;
                return;
            }
        }
    }
}
bool GameSession::piece_cell(uint8_t piece, uint8_t rotation, int x, int y)
{
    return piece < 7 && x >= 0 && x < 4 && y >= 0 && y < 4 &&
           (masks[piece][rotation & 3] & (1U << (y * 4 + x)));
}
bool GameSession::tetris_fits(int x, int y, uint8_t rotation) const
{
    for (int py = 0; py < 4; ++py)
        for (int px = 0; px < 4; ++px)
        {
            if (!piece_cell(tetris_.piece, rotation, px, py)) continue;
            const int wx = x + px, wy = y + py;
            if (wx < 0 || wx >= 10 || wy >= 15 ||
                (wy >= 0 && (tetris_.rows[unsigned(wy)] & (1U << wx)))) return false;
        }
    return true;
}
bool GameSession::tetris_active(int x, int y) const
{
    return piece_cell(tetris_.piece, tetris_.rotation, x - tetris_.x, y - tetris_.y);
}
bool GameSession::route_tetris_enter(KeyEvent event)
{
    if (id_ != GameId::tetris || event.key != Key::enter) return false;
    switch (event.gesture)
    {
        case Gesture::pressed:
            // Starting/resuming consumes this press, including its later click.
            enter_eligible_ = status_ == GameStatus::playing;
            enter_ignore_release_ = false;
            break;
        case Gesture::released:
            if (!enter_ignore_release_)
            {
                // ButtonBank can emit the previous click after the next release.
                // Keep those two outcomes in release order, across start/resume.
                if (enter_release_count_ < 2)
                {
                    if (enter_eligible_) enter_release_flags_ |= uint8_t(1U << enter_release_count_);
                    ++enter_release_count_;
                }
                else
                {
                    // Lost/malformed gesture outcomes must never create an action.
                    enter_release_flags_ = enter_release_count_ = 0;
                }
            }
            enter_eligible_ = enter_ignore_release_ = false;
            break;
        case Gesture::long_press:
            enter_release_flags_ = enter_release_count_ = 0;
            enter_eligible_ = false;
            enter_ignore_release_ = true;
            break;
        case Gesture::click:
        {
            const bool eligible = enter_release_count_ && (enter_release_flags_ & 1U);
            if (enter_release_count_)
            {
                --enter_release_count_;
                enter_release_flags_ >>= 1;
            }
            if (status_ == GameStatus::playing && eligible) tetris_rotate(1);
            return true;
        }
        case Gesture::double_click:
        {
            const bool eligible = enter_release_count_ == 2 && enter_release_flags_ == 3;
            enter_release_flags_ = enter_release_count_ = 0;
            if (status_ == GameStatus::playing && eligible)
            {
                while (tetris_down()) score_ += 2;
                clock_armed_ = false;
            }
            return true;
        }
        default:
            break;
    }
    return false;
}
void GameSession::tetris_move(int direction)
{
    const int x = tetris_.x + direction;
    if (tetris_fits(x, tetris_.y, tetris_.rotation)) tetris_.x = int8_t(x);
}
void GameSession::tetris_rotate(int direction)
{
    const auto rotation = uint8_t((tetris_.rotation + (direction > 0 ? 1 : 3)) & 3);
    for (const int kick : {0, -1, 1, -2, 2})
        if (tetris_fits(tetris_.x + kick, tetris_.y, rotation))
        {
            tetris_.x += int8_t(kick);
            tetris_.rotation = rotation;
            return;
        }
}
bool GameSession::tetris_down()
{
    if (tetris_fits(tetris_.x, tetris_.y + 1, tetris_.rotation))
    {
        ++tetris_.y;
        return true;
    }
    tetris_lock();
    return false;
}
void GameSession::tetris_lock()
{
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
        {
            if (!piece_cell(tetris_.piece, tetris_.rotation, x, y)) continue;
            const int wy = tetris_.y + y;
            if (wy < 0)
            {
                status_ = GameStatus::game_over;
                return;
            }
            tetris_.rows[unsigned(wy)] |= uint16_t(1U << (tetris_.x + x));
        }
    unsigned cleared = 0;
    for (int row = 14; row >= 0;)
    {
        if (tetris_.rows[unsigned(row)] != 0x03FF)
        {
            --row;
            continue;
        }
        ++cleared;
        for (int shift = row; shift > 0; --shift)
            tetris_.rows[unsigned(shift)] = tetris_.rows[unsigned(shift - 1)];
        tetris_.rows[0] = 0;
    }
    constexpr unsigned points[] = {0, 100, 300, 500, 800};
    score_ += points[cleared];
    tetris_.lines += uint16_t(cleared);
    tetris_spawn();
}
void GameSession::tetris_spawn()
{
    tetris_.piece = tetris_.next;
    tetris_.next = uint8_t(bounded(7));
    tetris_.rotation = 0;
    tetris_.x = 3;
    tetris_.y = -1;
    if (!tetris_fits(tetris_.x, tetris_.y, tetris_.rotation)) status_ = GameStatus::game_over;
}
void GameSession::reset_ball(int direction)
{
    pong_.ball_x = 64;
    pong_.ball_y = int16_t(23 + bounded(24));
    pong_.vx = direction < 0 ? -1 : 1;
    pong_.vy = bounded(2) ? -1 : 1;
}
void GameSession::step_pong()
{
    const int direction = int(bool(held_ & mask(Key::down))) - int(bool(held_ & mask(Key::up)));
    pong_.left_y = int16_t(std::clamp(int(pong_.left_y) + direction * 2,
                                     11 + int(pong_.left_half), 62 - int(pong_.left_half)));
    // A deliberately fallible opponent: it follows the ball only on two of every three frames.
    if (pong_.right_manual)
    {
        const int right_direction = int(bool(held_ & mask(Key::right))) -
                                    int(bool(held_ & mask(Key::left)));
        pong_.right_y += int16_t(right_direction * 2);
    }
    else if (++pong_.ai_frame % 3 != 0 && pong_.vx > 0)
    {
        if (pong_.right_y < pong_.ball_y - 2) ++pong_.right_y;
        if (pong_.right_y > pong_.ball_y + 2) --pong_.right_y;
    }
    pong_.right_y = int16_t(std::clamp(int(pong_.right_y), 11 + int(pong_.right_half),
                                      62 - int(pong_.right_half)));
    pong_.ball_x += pong_.vx;
    pong_.ball_y += pong_.vy;
    if (pong_.ball_y <= 12) { pong_.ball_y = 12; pong_.vy = 1; }
    else if (pong_.ball_y >= 61) { pong_.ball_y = 61; pong_.vy = -1; }
    if (pong_.vx < 0 && pong_.ball_x <= 8 && pong_.ball_x >= 6 &&
        std::abs(pong_.ball_y - pong_.left_y) <= pong_.left_half)
    {
        pong_.ball_x = 9;
        pong_.vx = 1;
        if (pong_.ball_y != pong_.left_y) pong_.vy = pong_.ball_y < pong_.left_y ? -1 : 1;
        ++score_;
    }
    else if (pong_.vx > 0 && pong_.ball_x >= 119 && pong_.ball_x <= 121 &&
             std::abs(pong_.ball_y - pong_.right_y) <= pong_.right_half)
    {
        pong_.ball_x = 118;
        pong_.vx = -1;
        if (pong_.ball_y != pong_.right_y) pong_.vy = pong_.ball_y < pong_.right_y ? -1 : 1;
    }
    else if (pong_.ball_x < 0 || pong_.ball_x > 127)
    {
        const bool left_missed = pong_.ball_x < 0;
        auto& half = left_missed ? pong_.left_half : pong_.right_half;
        if (!left_missed) score_ += 5;
        if (half > 2)
        {
            --half;
            reset_ball(left_missed ? 1 : -1);
        }
        else
        {
            pong_.winner = left_missed ? 2 : 1;
            status_ = GameStatus::game_over;
        }
    }
}
uint16_t GameSession::note_frequency() const
{
    constexpr uint16_t frequencies[] = {262, 294, 330, 349, 392, 440, 494, 523};
    return frequencies[piano_.note & 7];
}
void GameSession::play_note()
{
    ++note_sequence_;
    ++score_;
    note_sounding_ = true;
    note_until_ = now_ + 180;
}

void GameSession::draw_overlay(Canvas& c, int x) const
{
    if (status_ == GameStatus::playing) return;
    c.rounded(x + 3, 13, 122, 50, clear, true);
    c.rounded(x + 3, 13, 122, 50);
    if (status_ == GameStatus::ready)
    {
        constexpr std::string_view hint1[] = {"UP / ENTER: JUMP", "UP:LEFT DOWN:RIGHT", "UP/DOWN: FLY",
                                              "UP:LEFT DOWN:RIGHT", "UP/DOWN: PADDLE", "UP/DOWN: NOTE"};
        constexpr std::string_view hint2[] = {"HOLD ENTER: PAUSE", "HOLD ENTER: PAUSE", "ENTER: FIRE",
                                              "CLICK:ROT 2X:DROP", "CPU OPPONENT", "ENTER: PLAY"};
        centered(c, x, 17, "READY");
        centered(c, x, 28, hint1[unsigned(id_)]);
        centered(c, x, 38, hint2[unsigned(id_)]);
        centered(c, x, 51, "ENTER START");
    }
    else if (status_ == GameStatus::paused)
    {
        centered(c, x, 17, "PAUSED");
        centered(c, x, 28, "ENTER: RESUME");
        centered(c, x, 39, "UP: RESTART");
        centered(c, x, 51, "UP+DOWN: BACK");
    }
    else
    {
        centered(c, x, 17, id_ == GameId::pong && pong_.winner == 1 ? "YOU WIN!" : "GAME OVER");
        char text[24];
        std::snprintf(text, sizeof(text), "SCORE %lu", static_cast<unsigned long>(score_));
        centered(c, x, 28, text);
        centered(c, x, 39, "ENTER: RESTART");
        centered(c, x, 51, "UP+DOWN: BACK");
    }
}
void GameSession::draw(Canvas& c, int x) const
{
    // The caller owns clearing and clipping so this also works during page transitions.
    if (id_ != GameId::tetris)
    {
        c.text(x + 2, 1, id_ == GameId::pong && pong_.right_manual ? "PONG 2P" : game_title(id_));
        c.text(x + (id_ == GameId::air_raid ? 57 : 85), 1, "S:");
        number(c, x + (id_ == GameId::air_raid ? 69 : 97), 1, score_);
        c.line(x, 10, x + 127, 10);
    }
    switch (id_)
    {
        case GameId::dino:
        {
            c.line(x, 59, x + 127, 59);
            for (int dash = 0; dash < 128; dash += 9)
            {
                const int shifted = (dash - int(scene_ms_ / 45 % 9) + 128) % 128;
                c.line(x + shifted, 62, x + shifted + 3, 62);
            }
            const int bottom = 58 - jump_arc[dino_.jump_frame];
            c.rounded(x + 12, bottom - 8, 8, 8, set, true);
            c.rect(x + 17, bottom - 11, 7, 5, set, true);
            c.pixel(x + 22, bottom - 10, clear);
            c.line(x + 13, bottom - 1, x + 13, bottom + 1);
            c.line(x + 18, bottom - 1, x + 18, bottom + 1);
            for (const auto& obstacle : dino_.obstacles)
                if (obstacle.active)
                {
                    c.rect(x + obstacle.x, 59 - obstacle.height, obstacle.width, obstacle.height, set, true);
                    c.line(x + obstacle.x - 2, 63 - obstacle.height, x + obstacle.x, 63 - obstacle.height);
                }
            break;
        }
        case GameId::snake:
            c.rect(x + 2, 12, 124, 51);
            for (unsigned i = 0; i < snake_.length; ++i)
            {
                const auto point = snake_.body[i];
                c.rect(x + 4 + point.x * 4, 14 + point.y * 4, 3, 3, set, true);
            }
            if ((scene_ms_ / 220 & 1) == 0)
                c.rect(x + 4 + snake_.food.x * 4, 14 + snake_.food.y * 4, 3, 3);
            break;
        case GameId::air_raid:
        {
            c.text(x + 103, 1, "L:");
            number(c, x + 115, 1, air_.lives);
            const int py = air_.player_y;
            c.line(x + 5, py + 3, x + 16, py);
            c.line(x + 5, py + 3, x + 16, py + 6);
            c.line(x + 5, py + 3, x + 16, py + 3);
            c.line(x + 8, py, x + 8, py + 6);
            for (const auto& bullet : air_.bullets)
                if (bullet.active) c.line(x + bullet.x, bullet.y, x + bullet.x + 2, bullet.y);
            for (const auto& enemy : air_.enemies)
                if (enemy.active)
                {
                    c.rounded(x + enemy.x, enemy.y, 9, 7);
                    c.line(x + enemy.x - 3, enemy.y + 3, x + enemy.x, enemy.y + 3);
                    c.pixel(x + enemy.x + 2, enemy.y + 2);
                }
            break;
        }
        case GameId::tetris:
            c.rect(x + 2, 1, 42, 62);
            for (int y = 0; y < 15; ++y)
                for (int px = 0; px < 10; ++px)
                    if ((tetris_.rows[unsigned(y)] & (1U << px)) || tetris_active(px, y))
                        c.rect(x + 3 + px * 4, 2 + y * 4, 3, 3, set, true);
            c.text(x + 49, 1, "TETRIS");
            c.text(x + 49, 12, "SCORE");
            number(c, x + 49, 21, score_);
            c.text(x + 49, 32, "NEXT");
            for (int y = 0; y < 4; ++y)
                for (int px = 0; px < 4; ++px)
                    if (piece_cell(tetris_.next, 0, px, y))
                        c.rect(x + 52 + px * 4, 41 + y * 4, 3, 3, set, true);
            c.text(x + 78, 42, "LINES");
            number(c, x + 78, 51, tetris_.lines);
            break;
        case GameId::pong:
            c.line(x, 63, x + 127, 63);
            for (int y = 13; y < 61; y += 6) c.line(x + 64, y, x + 64, y + 2);
            c.line(x + 7, pong_.left_y - pong_.left_half, x + 7, pong_.left_y + pong_.left_half);
            c.line(x + 120, pong_.right_y - pong_.right_half, x + 120, pong_.right_y + pong_.right_half);
            c.rect(x + pong_.ball_x - 1, pong_.ball_y - 1, 3, 3, set, true);
            break;
        case GameId::piano:
        {
            constexpr std::string_view labels[] = {"C", "D", "E", "F", "G", "A", "B", "C"};
            for (unsigned i = 0; i < 8; ++i)
            {
                const int px = x + 4 + int(i) * 15;
                const bool sounding = i == piano_.note && note_sounding_;
                c.rounded(px, 15, 14, 34, set, sounding);
                c.text(px + 4, 37, labels[i], sounding);
                if (i == piano_.note) c.line(px + 3, 52, px + 10, 52);
            }
            c.text(x + 4, 57, "U/D NOTE  ENTER PLAY");
            break;
        }
        default: break;
    }
    draw_overlay(c, x);
}
}  // namespace c3
