#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "c3/core.hpp"
#include "font.hpp"

namespace c3
{
void Canvas::clip(int x, int y, int w, int h)
{
    left_ = std::clamp(x, 0, 128);
    top_ = std::clamp(y, 0, 64);
    right_ = std::clamp(x + std::max(w, 0), left_, 128);
    bottom_ = std::clamp(y + std::max(h, 0), top_, 64);
}
void Canvas::pixel(int x, int y, PixelOp op)
{
    if (x < left_ || x >= right_ || y < top_ || y >= bottom_) return;
    auto& byte = frame_[unsigned(y) / 8 * 128 + unsigned(x)];
    const uint8_t bit = uint8_t(1U << (unsigned(y) % 8));
    switch (op)
    {
        case PixelOp::set:
            byte |= bit;
            break;
        case PixelOp::clear:
            byte &= uint8_t(~bit);
            break;
        case PixelOp::invert:
            byte ^= bit;
            break;
    }
}
void Canvas::line(int x0, int y0, int x1, int y1, PixelOp op)
{
    const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -std::abs(y1 - y0),
              sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;)
    {
        pixel(x0, y0, op);
        if (x0 == x1 && y0 == y1) break;
        const int twice = 2 * error;
        if (twice >= dy)
        {
            error += dy;
            x0 += sx;
        }
        if (twice <= dx)
        {
            error += dx;
            y0 += sy;
        }
    }
}
void Canvas::rect(int x, int y, int w, int h, PixelOp op, bool fill)
{
    if (w <= 0 || h <= 0) return;
    for (int py = std::max(y, top_); py < std::min(y + h, bottom_); ++py)
        for (int px = std::max(x, left_); px < std::min(x + w, right_); ++px)
            if (fill || px == x || px == x + w - 1 || py == y || py == y + h - 1) pixel(px, py, op);
}
void Canvas::rounded(int x, int y, int w, int h, PixelOp op, bool fill)
{
    if (w < 5 || h < 5)
    {
        rect(x, y, w, h, op, fill);
        return;
    }
    const auto inside = [=](int px, int py)
    {
        const int lx = px - x, ly = py - y;
        if (lx < 0 || ly < 0 || lx >= w || ly >= h) return false;
        const int ex = std::min(lx, w - 1 - lx), ey = std::min(ly, h - 1 - ly);
        return !((ex == 0 && ey < 2) || (ey == 0 && ex < 2));
    };
    for (int py = std::max(y, top_); py < std::min(y + h, bottom_); ++py)
        for (int px = std::max(x, left_); px < std::min(x + w, right_); ++px)
            if (inside(px, py) && (fill || !inside(px - 1, py) || !inside(px + 1, py) ||
                                   !inside(px, py - 1) || !inside(px, py + 1)))
                pixel(px, py, op);
}
void Canvas::text(int x, int y, std::string_view str, bool inverted, int scale)
{
    if (scale < 1 || scale > 3) return;
    for (unsigned char ch : str)
    {
        if (x >= right_) break;
        if (ch < 32 || ch > 126) ch = '?';
        if (inverted) rect(x, y, 6 * scale, 8 * scale, PixelOp::set, true);
        for (int column = 0; column < 6; ++column)
            for (int row = 0; row < 8; ++row)
                if (glyphs[ch - 32][column] & (1U << row))
                    rect(x + column * scale, y + row * scale, scale, scale,
                         inverted ? PixelOp::clear : PixelOp::set, true);
        x += 6 * scale;
    }
}
namespace
{
constexpr auto set = PixelOp::set, clear = PixelOp::clear, invert = PixelOp::invert;
void header(Canvas& c, int x, std::string_view label)
{
    c.rect(x, 0, 128, 11, set, true);
    c.text(x + 4, 2, label, true);
    for (int dot : {119, 122, 125}) c.pixel(x + dot, 3, clear);
}
void footer(Canvas& c, int x, std::string_view hint)
{
    c.line(x, 54, x + 127, 54);
    c.text(x + 4, 56, hint);
}
void icon(Canvas& c, Icon icon, int x, int y)
{
    switch (icon)
    {
        case Icon::pet:
            c.rounded(x + 1, y + 5, 9, 14, set, true);
            c.rounded(x + 14, y + 5, 9, 14, set, true);
            c.rect(x + 5, y + 9, 3, 6, clear, true);
            c.rect(x + 18, y + 9, 3, 6, clear, true);
            break;
        case Icon::games:
            c.rounded(x, y + 3, 23, 16);
            c.line(x + 4, y + 11, x + 10, y + 11);
            c.line(x + 7, y + 8, x + 7, y + 14);
            c.rect(x + 16, y + 8, 2, 2, set, true);
            c.rect(x + 19, y + 12, 2, 2, set, true);
            break;
        case Icon::clock:
            c.rounded(x, y, 22, 22);
            c.line(x + 11, y + 3, x + 11, y + 11);
            c.line(x + 11, y + 11, x + 17, y + 15);
            break;
        case Icon::network:
            c.line(x + 2, y + 4, x + 19, y + 4);
            c.line(x + 2, y + 4, x, y + 7);
            c.line(x + 19, y + 4, x + 21, y + 7);
            c.line(x + 5, y + 9, x + 16, y + 9);
            c.line(x + 8, y + 14, x + 13, y + 14);
            c.rect(x + 10, y + 19, 2, 2, set, true);
            break;
        case Icon::settings:
            c.rounded(x + 2, y + 2, 19, 19);
            c.rounded(x + 8, y + 8, 7, 7);
            c.line(x + 11, y - 1, x + 11, y + 3);
            c.line(x + 11, y + 19, x + 11, y + 23);
            c.line(x - 1, y + 11, x + 3, y + 11);
            c.line(x + 19, y + 11, x + 23, y + 11);
            break;
        case Icon::tools:
            c.line(x + 1, y + 2, x + 21, y + 22);
            c.line(x + 2, y + 1, x + 22, y + 21);
            c.line(x + 3, y + 21, x + 21, y + 3);
            c.rounded(x + 15, y, 8, 8);
            c.rect(x, y + 19, 5, 4);
            break;
    }
}
void home_card(Canvas& c, const Model& m, const Telemetry& t, unsigned index, int x, uint32_t now)
{
    const auto entry = menu(View::home)[index];
    c.rounded(x + 2, 1, 124, 62);
    switch (m.settings().header)
    {
        case Header::time:
            c.text(x + 7, 3, t.time);
            break;
        case Header::date:
            c.text(x + 7, 3, t.date);
            break;
        case Header::title:
            c.text(x + 7, 3, "C3 GAMEBOX");
            break;
        case Header::pet:
        {
            // Same two walking frames and 72-step track as the reference menu.
            constexpr uint16_t pet[2][9] = {
                {0x0900, 0x0f80, 0x0bc2, 0x0ffd, 0x07fe, 0x07f8, 0x01f8, 0x0148, 0x0244},
                {0x0900, 0x0f80, 0x0bc2, 0x0ffd, 0x07fe, 0x07f8, 0x01f8, 0x0250, 0x0290}};
            const unsigned period = m.settings().motion == Motion::full ? 250 : 500;
            const unsigned tick = m.settings().motion == Motion::off ? 18 : now / period,
                           phase = tick % 72;
            const bool right = phase < 36;
            const int pet_x = x + 7 + int(right ? phase : 71 - phase) * 2;
            for (int row = 0; row < 9; ++row)
                for (int col = 0; col < 12; ++col)
                    if (pet[tick & 1][row] & (1U << col))
                        c.pixel(pet_x + (right ? col : 11 - col), row + 2);
            break;
        }
    }
    char position[6];
    const unsigned cards = unsigned(menu(View::home).size());
    std::snprintf(position, sizeof(position), "%u/%u", index + 1, cards);
    c.rounded(x + 92, 2, 32, 9, set, true);
    c.text(x + 98, 3, position, true);
    c.line(x + 4, 12, x + 123, 12);
    c.rounded(x + 7, 16, 34, 27);
    icon(c, entry.icon, x + 12, 21);
    c.rounded(x + 45, 17, int(entry.label.size()) * 6 + 8, 11, set, true);
    c.text(x + 49, 19, entry.label, true);
    c.line(x + 46, 30, x + 122, 30);
    c.text(x + 46, 33, entry.subtitle);
    c.line(x + 4, 47, x + 123, 47);
    for (unsigned dot = 0; dot < cards; ++dot)
    {
        if (dot == index)
            c.rect(x + 8 + int(dot) * 7, 54, 5, 3, set, true);
        else
            c.pixel(x + 10 + int(dot) * 7, 55);
    }
    c.text(x + 46, 52, "<> MOVE");
    c.text(x + 92, 52, "ENTER");
}
std::string_view setting_value(const Model& m, Action action, const Telemetry& t)
{
    switch (action)
    {
        case Action::motion:
            return std::array<std::string_view, 3>{"OFF", "LOW",
                                                   "FULL"}[unsigned(m.settings().motion)];
        case Action::brightness:
            return std::array<std::string_view, 4>{"LOW", "MID", "HIGH",
                                                   "MAX"}[m.settings().brightness];
        case Action::header:
            return std::array<std::string_view, 4>{"TIME", "DATE", "PET",
                                                   "TITLE"}[unsigned(m.settings().header)];
        case Action::wifi:
            return t.wifi_enabled ? "ON" : "OFF";
        default:
            return ">";
    }
}
void list(Canvas& c, const Model& m, const Telemetry& t, View view, int x, bool interactive)
{
    const auto entries = menu(view);
    const int selected = m.selected(view), top = std::max(selected - 2, 0);
    const int scroll = interactive ? m.motion().scroll_y.value() : -top * 16;
    c.clip(x, 12, 128, 52);
    for (unsigned i = 0; i < entries.size(); ++i)
    {
        const int y = 14 + int(i) * 16 + scroll;
        c.text(x + 8, y, entries[i].label);
        if (view == View::settings)
        {
            const auto value = setting_value(m, entries[i].action, t);
            c.text(x + 120 - int(value.size()) * 6, y, value);
        }
    }
    const int y = interactive ? m.motion().cursor_y.value() : 14 + (selected - top) * 16;
    const int width = interactive              ? m.motion().cursor_width.value()
                      : view == View::settings ? 118
                                               : int(entries[selected].label.size()) * 6 + 10;
    c.rounded(x + 3, y, width, 15, invert, true);
    if (entries.size() > 1)
    {
        c.line(x + 125, 15, x + 125, 61);
        c.rect(x + 123, 15 + selected * 42 / (int(entries.size()) - 1), 5, 4, set, true);
    }
    c.clip(x, 0, 128, 64);
    header(c, x, title(view));
}
void view(Canvas& c, const Model& m, const Telemetry& t, View v, int x, uint32_t now,
          bool interactive)
{
    c.clip(x, 0, 128, 64);
    char buffer[80]{};
    if (v == View::home)
    {
        if (interactive && !m.motion().carousel_x.settled())
        {
            const int offset = m.motion().carousel_x.value();
            home_card(c, m, t, m.previous_card(), x + offset - m.carousel_direction() * 128, now);
            home_card(c, m, t, m.selected(v), x + offset, now);
        }
        else
            home_card(c, m, t, m.selected(v), x, now);
        return;
    }
    if (!menu(v).empty())
    {
        list(c, m, t, v, x, interactive);
        return;
    }
    if (v == View::pet)
    {
        m.pet().draw(c, now, x);
        return;
    }
    if (v >= View::dino && v <= View::flappy)
    {
        // The session remains available while the page slides out to the game list.
        if (v == View::breakout || v == View::flappy) m.bonus().draw(c, x);
        else m.game().draw(c, x);
        return;
    }
    header(c, x, title(v));
    switch (v)
    {
        case View::clock:
            c.text(x + 16, 18, t.time, false, 2);
            c.text(x + 34, 37, t.date);
            c.text(x + 10, 46,
                   t.time_synced         ? "SNTP SYNCED"
                   : t.station_connected ? "WAITING FOR NTP"
                                         : "JOIN WI-FI TO SYNC");
            footer(c, x, "OK SYNC UP+DN BACK");
            break;
        case View::network:
            if (!t.wifi_enabled)
            {
                c.text(x + 8, 20, "WI-FI OFF");
                c.text(x + 8, 36, "SETTINGS > WI-FI");
            }
            else
            {
                std::snprintf(buffer, sizeof(buffer), "AP %s", t.ap_ssid);
                c.text(x + 4, 14, buffer);
                std::snprintf(buffer, sizeof(buffer), "KEY %s", t.ap_password);
                c.text(x + 4, 24, buffer);
                std::snprintf(buffer, sizeof(buffer), "IP %s", t.ap_ip);
                c.text(x + 4, 34, buffer);
                if (t.station_connected)
                    std::snprintf(buffer, sizeof(buffer), "STA %s", t.sta_ip);
                else
                    std::snprintf(buffer, sizeof(buffer), "STA OFF / USB CFG");
                c.text(x + 4, 44, buffer);
            }
            footer(c, x, "OK SYNC UP+DN BACK");
            break;
        case View::stopwatch:
        {
            const uint32_t ms = m.stopwatch_ms();
            std::snprintf(buffer, sizeof(buffer), "%02lu:%02lu.%02lu",
                          (unsigned long)(ms / 60000 % 100), (unsigned long)(ms / 1000 % 60),
                          (unsigned long)(ms / 10 % 100));
            c.text(x + 16, 20, buffer, false, 2);
            c.text(x + 40, 42, m.stopwatch_running() ? "RUNNING" : "PAUSED");
            footer(c, x, "OK RUN/STOP UP RESET");
            break;
        }
        case View::countdown:
        {
            const uint32_t seconds = m.countdown_seconds();
            std::snprintf(buffer, sizeof(buffer), "%02lu:%02lu", (unsigned long)(seconds / 60),
                          (unsigned long)(seconds % 60));
            c.text(x + 28, 19, buffer, false, 2);
            c.text(x + 34, 41,
                   m.countdown_running() ? "RUNNING"
                   : seconds             ? "SET MINUTES"
                                         : "TIME IS UP");
            footer(c, x, "UP/DN SET  OK RUN");
            break;
        }
        case View::input_lab:
        {
            constexpr std::string_view keys[] = {"UP",   "DOWN", "LEFT",  "RIGHT",
                                                 "JUMP", "FUNC", "ENTER", "BACK"};
            constexpr std::string_view gestures[] = {"PRESS",  "RELEASE", "CLICK",
                                                     "DOUBLE", "LONG",    "REPEAT"};
            const auto e = m.last_event();
            std::snprintf(buffer, sizeof(buffer), "%s %s", keys[unsigned(e.key)].data(),
                          gestures[unsigned(e.gesture)].data());
            c.text(x + 5, 15, buffer);
            std::snprintf(buffer, sizeof(buffer), "KEYS %02X EVT %lu", t.stable_keys,
                          (unsigned long)t.key_events);
            c.text(x + 5, 27, buffer);
            std::snprintf(buffer, sizeof(buffer), "DROP %lu LATE %lu", (unsigned long)t.key_drops,
                          (unsigned long)t.input_late);
            c.text(x + 5, 39, buffer);
            footer(c, x, "20MS DB UP+DN BACK");
            break;
        }
        case View::system:
            std::snprintf(buffer, sizeof(buffer), "HEAP%lu MIN%lu", (unsigned long)t.free_heap,
                          (unsigned long)t.min_heap);
            c.text(x + 4, 14, buffer);
            std::snprintf(buffer, sizeof(buffer), "DRAW %lu SKIP %lu", (unsigned long)t.frames,
                          (unsigned long)t.skipped_frames);
            c.text(x + 4, 24, buffer);
            std::snprintf(buffer, sizeof(buffer), "SPI %luUS ERR %lu", (unsigned long)t.frame_us,
                          (unsigned long)t.display_errors);
            c.text(x + 4, 34, buffer);
            std::snprintf(buffer, sizeof(buffer), "KEY DROP %lu", (unsigned long)t.key_drops);
            c.text(x + 4, 44, buffer);
            footer(c, x, "ESP32-C3 / SPI2 DMA");
            break;
        case View::debug:
            c.text(x + 4, 14,
                   !m.probe_wanted() ? "PAUSED"
                   : t.probe_ready   ? t.probe_swd         ? "SWD ACTIVE"
                                       : t.probe_connected ? "HOST CONNECTED"
                                                           : "LISTENING"
                                     : "STARTING / ERROR");
            std::snprintf(buffer, sizeof(buffer), "%s:4441",
                          t.station_connected ? t.sta_ip : t.ap_ip);
            c.text(x + 4, 24, buffer);
            std::snprintf(buffer, sizeof(buffer), "LIMIT %luK ACK %02X",
                          (unsigned long)(t.probe_limit_hz / 1000), t.probe_ack);
            c.text(x + 4, 34, buffer);
            std::snprintf(buffer, sizeof(buffer), "CLK %d IO %d RST %d", t.swd_clk, t.swd_io,
                          t.swd_reset);
            c.text(x + 4, 44, buffer);
            footer(c, x, "UP/DN RATE OK PAUSE");
            break;
        case View::about:
            c.text(x + 10, 17, "C3 GAMEBOX 1.1.2");
            c.text(x + 10, 29, "SSD1306 / 128X64");
            c.text(x + 10, 41, "ESP-IDF / C++20");
            footer(c, x, "GAMEBOX MOTION PORT");
            break;
        default:
            break;
    }
}
}  // namespace
void render(Frame& frame, const Model& model, const Telemetry& telemetry, uint32_t now)
{
    Canvas canvas(frame);
    canvas.clear();
    if (model.booting())
    {
        canvas.rounded(8, 7, 112, 50);
        canvas.rounded(18, 15, 92, 17, PixelOp::set, true);
        canvas.text(34, 20, "C3 GAMEBOX", true);
        canvas.text(25, 39, "ESP-IDF / C++20");
        canvas.line(18, 52, 18 + int(now % 600) * 92 / 600, 52);
        return;
    }
    if (model.view() == View::pet)
    {
        model.pet().draw(canvas, now);
        return;
    }
    if (model.motion().page_x.settled())
        view(canvas, model, telemetry, model.view(), 0, now, true);
    else
    {
        const int x = model.motion().page_x.value(),
                  previous_x = x + (model.forward() ? -128 : 128);
        view(canvas, model, telemetry, model.previous_view(), previous_x, now, false);
        view(canvas, model, telemetry, model.view(), x, now, true);
    }
}
}  // namespace c3
