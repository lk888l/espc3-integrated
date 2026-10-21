#include "c3/core.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace c3
{
std::size_t ButtonBank::sample(uint8_t pressed, uint32_t now, std::span<KeyEvent> output,
                              uint16_t enter_double_click_ms)
{
    if (!config_.valid()) return 0;
    const uint16_t enter_window = enter_double_click_ms > 2 * config_.debounce_ms &&
                                  enter_double_click_ms < config_.long_press_ms
                                      ? enter_double_click_ms : config_.double_click_ms;
    std::size_t count = 0;
    const auto emit = [&](Key key, Gesture gesture, uint32_t held = 0)
    {
        if (count < output.size()) output[count++] = {key, gesture, now, held};
    };
    for (unsigned i = 0; i < states_.size(); ++i)
    {
        auto& s = states_[i];
        const auto key = Key(i);
        const bool raw = pressed & (1U << i);
        if (raw != s.raw)
        {
            s.raw = raw;
            s.raw_at = now;
        }
        if (s.stable != s.raw && now - s.raw_at >= config_.debounce_ms)
        {
            s.stable = s.raw;
            if (s.stable)
            {
                s.press_at = now;
                s.long_sent = false;
                emit(key, Gesture::pressed);
            }
            else
            {
                const uint32_t held = now - s.press_at;
                emit(key, Gesture::released, held);
                if (s.long_sent)
                    s.pending = false;
                else if (s.pending && now - s.release_at < s.pending_double_click_ms)
                {
                    s.pending = false;
                    emit(key, Gesture::double_click, held);
                }
                else
                {
                    if (s.pending) emit(key, Gesture::click, s.pending_held);
                    s.pending = true;
                    s.release_at = now;
                    s.pending_held = held;
                    // Finish a pending gesture with the window from its first
                    // release, even if the UI changes pages before it completes.
                    s.pending_double_click_ms = key == Key::enter ? enter_window
                                                                  : config_.double_click_ms;
                }
            }
        }
        if (s.stable)
        {
            if (!s.long_sent && now - s.press_at >= config_.long_press_ms)
            {
                s.long_sent = true;
                s.pending = false;
                s.repeat_at = now + config_.repeat_delay_ms;
                emit(key, Gesture::long_press, now - s.press_at);
            }
            else if (s.long_sent && int32_t(now - s.repeat_at) >= 0)
            {
                // A late sample produces one repeat; never replay missed intervals.
                s.repeat_at = now + config_.repeat_interval_ms;
                emit(key, Gesture::repeat, now - s.press_at);
            }
        }
        if (s.pending && now - s.release_at >= s.pending_double_click_ms)
        {
            s.pending = false;
            emit(key, Gesture::click, s.pending_held);
        }
    }
    return count;
}
uint8_t ButtonBank::stable_mask() const
{
    uint8_t result = 0;
    for (unsigned i = 0; i < states_.size(); ++i)
        if (states_[i].stable) result |= uint8_t(1U << i);
    return result;
}
void Spring::step(Motion speed)
{
    if (speed == Motion::off)
    {
        position_ = target_;
        velocity_ = 0;
        return;
    }
    const int64_t stiffness = speed == Motion::reduced ? 34 : 62;
    const int64_t damping = speed == Motion::reduced ? 206 : 184;
    const int64_t distance = int64_t(target_) - position_;
    const int64_t velocity = (int64_t(velocity_) + distance * stiffness / 256) * damping / 256;
    constexpr auto lo = std::numeric_limits<int32_t>::min(),
                   hi = std::numeric_limits<int32_t>::max();
    velocity_ = int32_t(std::clamp<int64_t>(velocity, lo, hi));
    position_ = int32_t(std::clamp<int64_t>(int64_t(position_) + velocity_, lo, hi));
    if (std::abs(int64_t(target_) - position_) <= 8 && std::abs(int64_t(velocity_)) <= 8)
    {
        position_ = target_;
        velocity_ = 0;
    }
}
void MenuMotion::step(Motion speed)
{
    cursor_y.step(speed);
    cursor_width.step(speed);
    scroll_y.step(speed);
    page_x.step(speed);
    carousel_x.step(speed);
}
bool MenuMotion::active() const
{
    return !cursor_y.settled() || !cursor_width.settled() || !scroll_y.settled() ||
           !page_x.settled() || !carousel_x.settled();
}
std::span<const Entry> menu(View view)
{
    static constexpr Entry home[] = {
        {"Games", "8 MINI GAMES", View::games, Action::open, Icon::games},
        {"Tools", "INPUT/TIMERS", View::tools, Action::open, Icon::tools},
        {"Network", "WI-FI & NTP", View::network, Action::open, Icon::network},
        {"Clock", "NETWORK TIME", View::clock, Action::open, Icon::clock},
        {"Settings", "PERSONALIZE", View::settings, Action::open, Icon::settings},
        {"Desktop Pet", "FULL SCREEN", View::pet, Action::open, Icon::pet}};
    static constexpr Entry games[] = {
        {"Dino", "", View::dino, Action::open, Icon::games},
        {"Snake", "", View::snake, Action::open, Icon::games},
        {"Air Raid", "", View::air_raid, Action::open, Icon::games},
        {"Tetris", "", View::tetris, Action::open, Icon::games},
        {"Pong", "", View::pong, Action::open, Icon::games},
        {"Piano", "", View::piano, Action::open, Icon::games},
        {"Breakout", "", View::breakout, Action::open, Icon::games},
        {"Flappy", "", View::flappy, Action::open, Icon::games}};
    static constexpr Entry tools[] = {{"Stopwatch", "", View::stopwatch, Action::open, Icon::clock},
                                      {"Countdown", "", View::countdown, Action::open, Icon::clock},
                                      {"Input Lab", "", View::input_lab, Action::open, Icon::tools},
                                      {"System", "", View::system, Action::open, Icon::tools},
                                      {"Wi-Fi DAP", "", View::debug, Action::open, Icon::network},
                                      {"PC Monitor", "", View::pc_monitor, Action::open, Icon::network},
                                      {"About", "", View::about, Action::open, Icon::settings}};
    static constexpr Entry settings[] = {
        {"Motion", "", View::settings, Action::motion, Icon::settings},
        {"Brightness", "", View::settings, Action::brightness, Icon::settings},
        {"Home Header", "", View::settings, Action::header, Icon::settings},
        {"Wi-Fi", "", View::settings, Action::wifi, Icon::network},
        {"About", "", View::about, Action::open, Icon::settings}};
    switch (view)
    {
        case View::home:
            return home;
        case View::tools:
            return tools;
        case View::games:
            return games;
        case View::settings:
            return settings;
        default:
            return {};
    }
}
std::string_view title(View v)
{
    static constexpr std::string_view names[] = {
        "C3 GAMEBOX", "TOOLS",     "NETWORK", "CLOCK",     "SETTINGS", "STOPWATCH",
        "COUNTDOWN",  "INPUT LAB", "SYSTEM",  "WI-FI DAP", "ABOUT", "GAMES",
        "DINO", "SNAKE", "AIR RAID", "TETRIS", "PONG", "PIANO", "BREAKOUT", "FLAPPY", "DESKTOP PET", "PC MONITOR"};
    static_assert(std::size(names) == unsigned(View::count));
    return unsigned(v) < unsigned(View::count) ? names[unsigned(v)] : "";
}
Model::Model(Settings settings) : settings_(settings.valid() ? settings : Settings{}) {}
void Model::tick(uint32_t now)
{
    now_ = now;
    if (!have_tick_)
    {
        have_tick_ = true;
        last_tick_ = now;
        boot_started_ = now;
    }
    if (booting_ && now - boot_started_ >= 600) booting_ = false;
    motion_remainder_ += std::min<uint32_t>(now - last_tick_, 64);
    last_tick_ = now;
    while (motion_remainder_ >= 8)
    {
        motion_.step(settings_.motion);
        motion_remainder_ -= 8;
    }
    if (countdown_running_ && countdown_seconds() == 0)
    {
        countdown_base_ = 0;
        countdown_running_ = false;
    }
    if (gaming())
    {
        if (game_status() == GameStatus::playing) ++game_steps_;
        if (bonus_game()) bonus_.tick(now);
        else game_.tick(now);
    }
    if (view_ == View::pet) pet_.tick(now);
}
void Model::handle(KeyEvent event)
{
    now_ = event.at_ms;
    last_event_ = event;
    ++event_count_;
    if (booting_)
    {
        if (event.gesture == Gesture::pressed) booting_ = false;
        return;
    }
    // Handle Back before every page-specific route. Only the debounced press
    // navigates: a hold, repeat or delayed click must not exit another level.
    if (event.key == Key::back)
    {
        if (event.gesture == Gesture::pressed) back();
        return;
    }
    if (gaming())
    {
        if (bonus_game()) bonus_.handle(event);
        else game_.handle(event);
        return;
    }
    if (view_ == View::pet)
    {
        pet_.handle(event);
        return;
    }
    if (event.key == Key::function && event.gesture == Gesture::double_click)
    {
        open(View::clock);
        return;
    }
    if (event.key == Key::function && event.gesture == Gesture::long_press)
    {
        open(View::input_lab);
        return;
    }
    if (event.gesture != Gesture::pressed && event.gesture != Gesture::repeat) return;
    if (event.key == Key::function)
    {
        if (view_ == View::stopwatch) reset_stopwatch();
        if (view_ == View::countdown)
        {
            countdown_running_ = false;
            countdown_base_ = 300;
        }
        return;
    }
    const bool direction = event.key == Key::up || event.key == Key::down ||
                           event.key == Key::left || event.key == Key::right;
    if (direction)
    {
        const int d = (event.key == Key::up || event.key == Key::left) ? -1 : 1;
        if (view_ == View::pc_monitor)
            pc_monitor_page_ ^= 1;
        else if (view_ == View::debug)
        {
            probe_speed_ = uint8_t((int(probe_speed_) + d + 5) % 5);
            effect(set_probe_clock);
        }
        else if (view_ == View::stopwatch)
        {
            if (event.key == Key::up) reset_stopwatch();
        }
        else if (view_ == View::countdown && !countdown_running_)
        {
            const int delta = (event.key == Key::left || event.key == Key::right) ? 1 : 60;
            // Up increases a timer; Down decreases it.
            const int sign = event.key == Key::up || event.key == Key::right ? 1 : -1;
            countdown_base_ =
                uint32_t(std::clamp<int64_t>(int64_t(countdown_base_) + sign * delta, 0, 359999));
        }
        else if (view_ == View::settings && (event.key == Key::left || event.key == Key::right))
            confirm();
        else
            move(d);
        return;
    }
    if (event.gesture == Gesture::pressed && (event.key == Key::enter || event.key == Key::jump))
        confirm();
}
void Model::move(int d)
{
    const auto entries = menu(view_);
    if (entries.empty()) return;
    auto& selected = selection_[unsigned(view_)];
    const uint8_t old = selected;
    selected = uint8_t((int(selected) + d + int(entries.size())) % int(entries.size()));
    if (view_ == View::home)
    {
        previous_card_ = old;
        carousel_direction_ = d;
        motion_.carousel_x.snap(int16_t(d * 128));
        motion_.carousel_x.target(0);
    }
    else
        update_targets();
}
void Model::confirm()
{
    const auto entries = menu(view_);
    if (!entries.empty())
    {
        const auto& entry = entries[selected(view_)];
        switch (entry.action)
        {
            case Action::open:
                open(entry.view);
                return;
            case Action::motion:
                settings_.motion = Motion((unsigned(settings_.motion) + 1) % 3);
                break;
            case Action::brightness:
                settings_.brightness = (settings_.brightness + 1) % 4;
                break;
            case Action::header:
                settings_.header = Header((unsigned(settings_.header) + 1) % 4);
                break;
            case Action::wifi:
                effect(toggle_wifi);
                return;
        }
        effect(save_settings);
        return;
    }
    switch (view_)
    {
        case View::pc_monitor:
            pc_monitor_page_ ^= 1;
            break;
        case View::clock:
        case View::network:
            effect(sync_time);
            break;
        case View::stopwatch:
            if (stopwatch_running_)
                stopwatch_total_ = stopwatch_ms();
            else
                stopwatch_started_ = now_;
            stopwatch_running_ = !stopwatch_running_;
            break;
        case View::countdown:
            if (countdown_running_)
                countdown_base_ = countdown_seconds();
            else
                countdown_started_ = now_;
            countdown_running_ = !countdown_running_ && countdown_base_ > 0;
            break;
        case View::debug:
            probe_wanted_ = !probe_wanted_;
            effect(probe_wanted_ ? start_probe : stop_probe);
            break;
        default:
            break;
    }
}
void Model::open(View next, bool forward)
{
    if (next == View::pet) booting_ = false;
    if (next == view_ || unsigned(next) >= unsigned(View::count)) return;
    if (view_ == View::debug)
    {
        probe_wanted_ = false;
        effect(stop_probe);
    }
    if (next == View::about) about_parent_ = view_;
    previous_view_ = view_;
    view_ = next;
    if (gaming())
    {
        // A fresh session on every entry; the menu-confirm press is never forwarded.
        game_steps_ = 0;
        const uint32_t seed = now_ ^ (++game_serial_ * 2654435761U);
        if (bonus_game()) bonus_.reset(next == View::flappy, seed);
        else game_.reset(GameId(unsigned(next) - unsigned(View::dino)), seed);
    }
    if (next == View::pet) pet_.select(pet_.style(), now_);
    if (next == View::pc_monitor) pc_monitor_page_ = 0;
    forward_ = forward;
    motion_.carousel_x.snap(0);
    motion_.page_x.snap(int16_t(forward ? 128 : -128));
    motion_.page_x.target(0);
    if (next == View::pet || previous_view_ == View::pet) motion_.page_x.snap(0);
    update_targets(true);
    if (next == View::debug)
    {
        probe_wanted_ = true;
        effect(start_probe);
    }
}
void Model::back()
{
    if (view_ == View::home) return;
    const auto destination = view_ == View::about ? about_parent_
                             : gaming() ? View::games
                             : (view_ == View::tools || view_ == View::network ||
                                view_ == View::clock || view_ == View::settings || view_ == View::games ||
                                view_ == View::pet)
                                 ? View::home
                                 : View::tools;
    open(destination, false);
}
void Model::update_targets(bool snap)
{
    const auto entries = menu(view_);
    if (entries.empty()) return;
    const int selected = selection_[unsigned(view_)], top = std::max(selected - 2, 0);
    const int16_t y = int16_t(14 + (selected - top) * 16), scroll = int16_t(-top * 16);
    const int16_t width =
        view_ == View::settings ? 118 : int16_t(entries[selected].label.size() * 6 + 10);
    if (snap)
    {
        motion_.cursor_y.snap(y);
        motion_.cursor_width.snap(width);
        motion_.scroll_y.snap(scroll);
    }
    else
    {
        motion_.cursor_y.target(y);
        motion_.cursor_width.target(width);
        motion_.scroll_y.target(scroll);
    }
}
uint32_t Model::stopwatch_ms() const
{
    return stopwatch_total_ + (stopwatch_running_ ? now_ - stopwatch_started_ : 0);
}
uint32_t Model::countdown_seconds() const
{
    const uint32_t elapsed = countdown_running_ ? (now_ - countdown_started_) / 1000 : 0;
    return elapsed < countdown_base_ ? countdown_base_ - elapsed : 0;
}
void Model::reset_stopwatch()
{
    stopwatch_total_ = 0;
    stopwatch_started_ = now_;
}
}  // namespace c3
