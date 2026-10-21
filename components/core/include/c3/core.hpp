#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "c3/games.hpp"
#include "c3/bonus_games.hpp"
#include "c3/pet.hpp"
#include "c3/pc_monitor.hpp"

namespace c3
{
constexpr std::size_t frame_bytes = 128 * 64 / 8;
constexpr uint16_t tetris_double_click_ms = 150;
using Frame = std::array<uint8_t, frame_bytes>;
enum class Key : uint8_t
{
    up,
    down,
    left,
    right,
    jump,
    function,
    enter,
    back,
    count
};
enum class Gesture : uint8_t
{
    pressed,
    released,
    click,
    double_click,
    long_press,
    repeat
};
constexpr uint8_t mask(Key k) { return uint8_t(1U << unsigned(k)); }
struct KeyEvent
{
    Key key{};
    Gesture gesture{};
    uint32_t at_ms{}, held_ms{};
};
struct ButtonConfig
{
    uint16_t debounce_ms = 20, double_click_ms = 280, long_press_ms = 650;
    uint16_t repeat_delay_ms = 110, repeat_interval_ms = 90;
    constexpr bool valid() const
    {
        return debounce_ms && double_click_ms > 2 * debounce_ms &&
               long_press_ms > double_click_ms && repeat_delay_ms && repeat_interval_ms;
    }
};
class ButtonBank
{
   public:
    explicit ButtonBank(ButtonConfig config = {}) : config_(config) {}
    std::size_t sample(uint8_t pressed, uint32_t now, std::span<KeyEvent> output,
                       uint16_t enter_double_click_ms = 0);
    uint8_t stable_mask() const;

   private:
    struct State
    {
        bool raw{}, stable{}, pending{}, long_sent{};
        uint32_t raw_at{}, press_at{}, release_at{}, pending_held{}, repeat_at{};
        uint16_t pending_double_click_ms{};
    };
    std::array<State, unsigned(Key::count)> states_{};
    ButtonConfig config_;
};

enum class Motion : uint8_t
{
    off,
    reduced,
    full
};
class Spring
{
   public:
    explicit constexpr Spring(int16_t value = 0)
        : position_(int32_t(value) * 256), target_(position_)
    {
    }
    void target(int16_t value) { target_ = int32_t(value) * 256; }
    void snap(int16_t value)
    {
        position_ = target_ = int32_t(value) * 256;
        velocity_ = 0;
    }
    void step(Motion speed);
    int16_t value() const { return int16_t((position_ + 128) / 256); }
    bool settled() const { return position_ == target_ && velocity_ == 0; }

   private:
    int32_t position_, target_, velocity_ = 0;
};
struct MenuMotion
{
    Spring cursor_y{14}, cursor_width{52}, scroll_y{}, page_x{}, carousel_x{};
    void step(Motion speed);
    bool active() const;
};
enum class Header : uint8_t
{
    time,
    date,
    pet,
    title
};
struct Settings
{
    uint8_t version = 1;
    Motion motion = Motion::full;
    uint8_t brightness = 2;
    Header header = Header::time;
    bool valid() const
    {
        return version == 1 && unsigned(motion) <= 2 && brightness < 4 && unsigned(header) <= 3;
    }
    uint8_t contrast() const { return std::array<uint8_t, 4>{0x10, 0x50, 0xa0, 0xff}[brightness]; }
};
enum class View : uint8_t
{
    home,
    tools,
    network,
    clock,
    settings,
    stopwatch,
    countdown,
    input_lab,
    system,
    debug,
    about,
    games,
    dino,
    snake,
    air_raid,
    tetris,
    pong,
    piano,
    breakout,
    flappy,
    pet,
    pc_monitor,
    count
};
enum class Icon : uint8_t
{
    tools,
    network,
    clock,
    settings,
    games,
    pet
};
enum class Action : uint8_t
{
    open,
    motion,
    brightness,
    header,
    wifi
};
struct Entry
{
    std::string_view label, subtitle;
    View view;
    Action action;
    Icon icon;
};
std::span<const Entry> menu(View view);
std::string_view title(View view);
enum Effect : uint16_t
{
    none = 0,
    save_settings = 1,
    sync_time = 2,
    toggle_wifi = 4,
    start_probe = 8,
    stop_probe = 16,
    set_probe_clock = 32
};
class Model
{
   public:
    explicit Model(Settings settings = {});
    void tick(uint32_t now);
    void handle(KeyEvent event);
    void open(View view, bool forward = true);
    uint16_t take_effects()
    {
        const auto result = effects_;
        effects_ = 0;
        return result;
    }
    View view() const { return view_; }
    View previous_view() const { return previous_view_; }
    bool forward() const { return forward_; }
    bool booting() const { return booting_; }
    uint8_t selected(View v) const { return selection_[unsigned(v)]; }
    uint8_t previous_card() const { return previous_card_; }
    int carousel_direction() const { return carousel_direction_; }
    const MenuMotion& motion() const { return motion_; }
    const Settings& settings() const { return settings_; }
    KeyEvent last_event() const { return last_event_; }
    uint32_t event_count() const { return event_count_; }
    uint32_t stopwatch_ms() const;
    uint32_t countdown_seconds() const;
    bool stopwatch_running() const { return stopwatch_running_; }
    bool countdown_running() const { return countdown_running_; }
    uint32_t probe_clock() const
    {
        return std::array<uint32_t, 5>{100000, 250000, 500000, 1000000, 2000000}[probe_speed_];
    }
    bool probe_wanted() const { return probe_wanted_; }
    uint8_t pc_monitor_page() const { return pc_monitor_page_; }
    void probe_stopped() { probe_wanted_ = false; }
    void reset_stopwatch();
    bool gaming() const { return view_ >= View::dino && view_ <= View::flappy; }
    bool bonus_game() const { return view_ == View::breakout || view_ == View::flappy; }
    uint8_t game_index() const { return gaming() ? uint8_t(unsigned(view_) - unsigned(View::dino)) : 255; }
    GameStatus game_status() const { return bonus_game() ? bonus_.status() : game_.status(); }
    uint32_t game_score() const { return bonus_game() ? bonus_.score() : game_.score(); }
    uint8_t game_lives() const { return bonus_game() ? bonus_.lives() : game_.lives(); }
    uint32_t game_steps() const { return game_steps_; }
    const GameSession& game() const { return game_; }
    const BonusSession& bonus() const { return bonus_; }
    PetSession& pet() { return pet_; }
    const PetSession& pet() const { return pet_; }

   private:
    void move(int direction);
    void confirm();
    void back();
    void update_targets(bool snap = false);
    void effect(Effect e) { effects_ |= e; }
    Settings settings_;
    GameSession game_;
    BonusSession bonus_;
    PetSession pet_;
    MenuMotion motion_;
    View view_ = View::home, previous_view_ = View::home, about_parent_ = View::settings;
    std::array<uint8_t, unsigned(View::count)> selection_{};
    uint8_t previous_card_ = 0, probe_speed_ = 0;
    uint8_t pc_monitor_page_ = 0;
    int carousel_direction_ = 1;
    bool forward_ = true, booting_ = true, have_tick_ = false, probe_wanted_ = false;
    uint16_t effects_ = 0;
    uint32_t now_ = 0, last_tick_ = 0, motion_remainder_ = 0, boot_started_ = 0;
    uint32_t stopwatch_total_ = 0, stopwatch_started_ = 0;
    uint32_t countdown_base_ = 300, countdown_started_ = 0;
    bool stopwatch_running_ = false, countdown_running_ = false;
    KeyEvent last_event_{};
    uint32_t event_count_ = 0;
    uint32_t game_steps_ = 0, game_serial_ = 0;
};

struct Telemetry
{
    pc::Snapshot pc{};
    char time[9] = "--:--:--", date[11] = "----/--/--";
    char sta_ip[16] = "0.0.0.0", ap_ip[16] = "192.168.4.1", ap_ssid[33]{}, ap_password[64]{};
    bool time_valid{}, time_synced{}, wifi_enabled = true, station_connected{};
    int rssi = 0;
    uint32_t sync_age_s{}, free_heap{}, min_heap{}, key_events{}, key_drops{}, input_late{};
    uint32_t frames{}, skipped_frames{}, display_errors{}, frame_us{};
    uint8_t stable_keys{};
    bool probe_ready{}, probe_connected{}, probe_swd{};
    uint32_t probe_packets{}, probe_errors{}, probe_hz = 100000, probe_requested_hz = 100000,
                                              probe_limit_hz = 100000;
    uint8_t probe_ack{};
    int probe_error{};
    int swd_clk = 20, swd_io = 21, swd_reset = -1;
};

enum class PixelOp
{
    set,
    clear,
    invert
};
class Canvas
{
   public:
    explicit Canvas(Frame& frame) : frame_(frame) {}
    void clear() { frame_.fill(0); }
    void clip(int x, int y, int w, int h);
    void pixel(int x, int y, PixelOp op = PixelOp::set);
    void line(int x0, int y0, int x1, int y1, PixelOp op = PixelOp::set);
    void rect(int x, int y, int w, int h, PixelOp op = PixelOp::set, bool fill = false);
    void rounded(int x, int y, int w, int h, PixelOp op = PixelOp::set, bool fill = false);
    void text(int x, int y, std::string_view text, bool inverted = false, int scale = 1);

   private:
    Frame& frame_;
    int left_ = 0, top_ = 0, right_ = 128, bottom_ = 64;
};
void render(Frame& frame, const Model& model, const Telemetry& telemetry, uint32_t now);
}  // namespace c3
