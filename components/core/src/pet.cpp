#include "c3/pet.hpp"

#include <algorithm>

#include "c3/core.hpp"

namespace c3
{
std::string_view pet_action_name(PetAction action)
{
    constexpr std::string_view names[] = {
        "idle", "blink", "happy", "eat", "sleep", "surprised", "sad", "dance"};
    return unsigned(action) < unsigned(PetAction::count) ? names[unsigned(action)] : "";
}
std::string_view pet_style_name(uint8_t style)
{
    constexpr std::string_view names[] = {"Eyes", "Pixel Cat", "Moon Bunny", "Mochi Slime", "Custom"};
    return style < PetSession::style_count ? names[style] : "";
}
bool PetPack::decode(std::span<const uint8_t> bytes)
{
    if (bytes.size() < header_bytes || bytes.size() > max_bytes ||
        bytes[0] != 'P' || bytes[1] != 'E' || bytes[2] != 'T' || bytes[3] != '1' ||
        bytes[4] != 64 || bytes[5] != 32 || bytes[6] != 8 || bytes[7] != 0)
        return false;
    for (unsigned i = 8; i < 24; ++i)
        if (bytes[i] && (bytes[i] < 32 || bytes[i] > 126)) return false;
    unsigned frames = 0;
    for (unsigned i = 0; i < unsigned(PetAction::count); ++i)
    {
        if (bytes[24 + i] > 4) return false;
        frames += bytes[24 + i];
        const unsigned interval = bytes[32 + i * 2] | (unsigned(bytes[33 + i * 2]) << 8);
        if (interval < 80 || interval > 1000) return false;
    }
    if (!bytes[24] || frames > max_frames || bytes.size() != header_bytes + frames * frame_bytes)
        return false;
    for (unsigned i = 48; i < header_bytes; ++i)
        if (bytes[i]) return false;
    std::copy(bytes.begin(), bytes.end(), raw_.begin());
    size_ = uint16_t(bytes.size());
    return true;
}
std::string_view PetPack::name() const
{
    if (!valid()) return {};
    std::size_t length = 0;
    while (length < 16 && raw_[8 + length]) ++length;
    return {reinterpret_cast<const char*>(raw_.data() + 8), length};
}
uint8_t PetPack::frame_count(PetAction action) const
{
    return valid() && action < PetAction::count ? raw_[24 + unsigned(action)] : 0;
}
uint16_t PetPack::interval_ms(PetAction action) const
{
    if (!valid() || action >= PetAction::count) return 200;
    return uint16_t(raw_[32 + unsigned(action) * 2] |
                    (uint16_t(raw_[33 + unsigned(action) * 2]) << 8));
}
std::span<const uint8_t> PetPack::frame(PetAction action, uint32_t index) const
{
    if (!valid() || action >= PetAction::count) return {};
    if (!frame_count(action)) action = PetAction::idle;
    unsigned offset = header_bytes;
    for (unsigned i = 0; i < unsigned(action); ++i) offset += raw_[24 + i] * frame_bytes;
    offset += (index % frame_count(action)) * frame_bytes;
    return {raw_.data() + offset, frame_bytes};
}

void PetSession::act(PetAction action, uint32_t now)
{
    if (action >= PetAction::count) return;
    now_ = action_at_ = last_input_ = now;
    initialized_ = true;
    action_ = action;
    next_blink_ = now + 3200 + (sequence_ % 4) * 431;
    ++sequence_;
}
bool PetSession::select(uint8_t style, uint32_t now)
{
    if (style >= style_count || (style == custom_style && !has_custom())) return false;
    style_ = style;
    look_ = 0;
    enter_armed_ = waking_enter_ = false;
    act(PetAction::idle, now);
    return true;
}
void PetSession::set_custom(const PetPack* pack)
{
    custom_ = pack && pack->valid() ? pack : nullptr;
    if (style_ == custom_style)
    {
        if (!custom_) style_ = 0;
        act(PetAction::idle, now_);
    }
}
std::string_view PetSession::name() const
{
    return style_ == custom_style && has_custom() ? custom_->name() : pet_style_name(style_);
}
uint32_t PetSession::duration() const
{
    if (style_ == custom_style && has_custom())
    {
        const auto displayed = custom_->frame_count(action_) ? action_ : PetAction::idle;
        return uint32_t(custom_->frame_count(displayed)) * custom_->interval_ms(displayed);
    }
    constexpr uint16_t duration_ms[] = {0, 200, 1400, 1800, 0, 1000, 1800, 2400};
    return duration_ms[unsigned(action_)];
}
void PetSession::tick(uint32_t now)
{
    now_ = now;
    if (!initialized_)
    {
        act(PetAction::idle, now);
        return;
    }
    if (action_ != PetAction::idle && action_ != PetAction::sleep && now - action_at_ >= duration())
    {
        action_ = PetAction::idle;
        action_at_ = now;
        next_blink_ = now + 3000 + (sequence_ % 5) * 379;
        ++sequence_;
    }
    if (action_ == PetAction::idle && now - last_input_ >= 60000)
    {
        // Automatic animations must not refresh the inactivity clock.
        action_ = PetAction::sleep;
        action_at_ = now;
        ++sequence_;
    }
    else if (action_ == PetAction::idle && int32_t(now - next_blink_) >= 0)
    {
        action_ = PetAction::blink;
        action_at_ = now;
        ++sequence_;
    }
}
void PetSession::handle(KeyEvent event)
{
    if (event.key >= Key::count || event.gesture == Gesture::released) return;
    now_ = event.at_ms;
    if (!initialized_) act(PetAction::idle, now_);
    if (event.gesture == Gesture::pressed)
    {
        last_input_ = now_;
        if (action_ == PetAction::sleep)
        {
            act(PetAction::idle, now_);
            waking_enter_ = event.key == Key::enter;
            enter_armed_ = false;
            return;
        }
        if (event.key == Key::enter)
        {
            enter_armed_ = true;
            waking_enter_ = false;
            return;
        }
        if (event.key == Key::up || event.key == Key::down)
        {
            const int styles = has_custom() ? style_count : builtin_styles;
            const int direction = event.key == Key::up ? -1 : 1;
            select(uint8_t((int(style_) + direction + styles) % styles), now_);
        }
        else if (event.key == Key::left || event.key == Key::right)
        {
            look_ = event.key == Key::left ? -4 : 4;
            look_at_ = now_;
            act(PetAction::idle, now_);
        }
        else if (event.key == Key::jump)
            act(PetAction::dance, now_);
        else if (event.key == Key::function)
            act(PetAction::surprised, now_);
        return;
    }
    if (event.key != Key::enter) return;
    if (event.gesture != Gesture::click && event.gesture != Gesture::double_click &&
        event.gesture != Gesture::long_press)
        return;
    if (waking_enter_)
    {
        waking_enter_ = false;
        return;
    }
    if (!enter_armed_) return;
    enter_armed_ = false;
    act(event.gesture == Gesture::long_press ? PetAction::sleep
        : event.gesture == Gesture::double_click ? PetAction::eat : PetAction::happy, now_);
}

namespace
{
constexpr auto white = PixelOp::set, black = PixelOp::clear;
void ellipse(Canvas& c, int cx, int cy, int rx, int ry, PixelOp color = white, bool fill = true)
{
    if (rx < 1 || ry < 1) return;
    const auto inside = [=](int x, int y)
    { return x * x * ry * ry + y * y * rx * rx <= rx * rx * ry * ry; };
    for (int y = -ry; y <= ry; ++y)
        for (int x = -rx; x <= rx; ++x)
            if (inside(x, y) && (fill || !inside(x - 1, y) || !inside(x + 1, y) ||
                                        !inside(x, y - 1) || !inside(x, y + 1)))
                c.pixel(cx + x, cy + y, color);
}
void heart(Canvas& c, int x, int y)
{
    c.rounded(x, y, 6, 6, white, true);
    c.rounded(x + 4, y, 6, 6, white, true);
    c.line(x + 1, y + 5, x + 5, y + 10);
    c.line(x + 8, y + 5, x + 5, y + 10);
    c.line(x + 3, y + 5, x + 5, y + 8);
    c.line(x + 7, y + 5, x + 5, y + 8);
}
void sparkle(Canvas& c, int x, int y)
{
    c.line(x - 3, y, x + 3, y);
    c.line(x, y - 3, x, y + 3);
}
void small_eye(Canvas& c, int x, int y, PetAction action, int look, PixelOp color)
{
    if (action == PetAction::sleep || action == PetAction::blink)
        c.line(x - 4, y + 1, x + 4, y + 1, color);
    else if (action == PetAction::happy || action == PetAction::dance)
    {
        c.line(x - 4, y + 1, x, y - 3, color);
        c.line(x, y - 3, x + 4, y + 1, color);
        c.line(x - 4, y + 2, x, y - 2, color);
        c.line(x, y - 2, x + 4, y + 2, color);
    }
    else if (action == PetAction::sad)
    {
        c.line(x - 4, y - 2, x + 4, y + 1, color);
        ellipse(c, x + look / 2, y + 3, 2, 3, color);
    }
    else
        ellipse(c, x + look / 2, y, action == PetAction::surprised ? 4 : 3,
                action == PetAction::surprised ? 6 : 4, color);
}
}  // namespace

void PetSession::draw(Canvas& c, uint32_t now, int x) const
{
    if (style_ == custom_style && has_custom())
    {
        const auto displayed = custom_->frame_count(action_) ? action_ : PetAction::idle;
        const auto bitmap = custom_->frame(displayed, (now - action_at_) / custom_->interval_ms(displayed));
        for (unsigned row = 0; row < 32; ++row)
            for (unsigned col = 0; col < 64; ++col)
                if (bitmap[row * 8 + col / 8] & (0x80U >> (col % 8)))
                    c.rect(x + int(col) * 2, int(row) * 2, 2, 2, white, true);
        return;
    }

    const unsigned phase = now / 230 % 8;
    const int breath = int(phase < 4 ? phase : 7 - phase) / 2;
    const int sway = action_ == PetAction::dance ? ((now / 180) % 2 ? 4 : -4) : 0;
    const int bob = action_ == PetAction::dance ? ((now / 180) % 2 ? -3 : 1)
                    : action_ == PetAction::happy ? -int((now / 170) % 2) * 2 : breath;
    const int cx = x + 64 + sway;
    constexpr int looking[] = {0, 0, 3, 3, 0, -3, -3, 0};
    const int look = look_ && now - look_at_ < 2400 ? look_ : looking[(now / 1300) % 8];
    const uint32_t elapsed = now - action_at_;

    if (style_ == 0)
    {
        const int cy = 31 + bob;
        for (int side : {-1, 1})
        {
            const int ex = cx + side * 24;
            if (action_ == PetAction::blink || action_ == PetAction::sleep)
            {
                c.rounded(ex - 15, cy + 5, 30, 4, white, true);
                if (action_ == PetAction::sleep)
                    c.line(ex - 13, cy + 7, ex - 17, cy + 3);
            }
            else if (action_ == PetAction::happy || action_ == PetAction::dance)
            {
                for (int thick = 0; thick < 4; ++thick)
                {
                    c.line(ex - 14, cy + 5 + thick, ex, cy - 7 + thick);
                    c.line(ex, cy - 7 + thick, ex + 14, cy + 5 + thick);
                }
            }
            else
            {
                const int h = action_ == PetAction::surprised ? 34 : 27;
                c.rounded(ex - 16, cy - h / 2, 32, h, white, true);
                c.rounded(ex - 6 + look, cy - 7, 12, 18, black, true);
                c.rect(ex - 4 + look, cy - 5, 3, 3, white, true);
                if (action_ == PetAction::sad)
                    for (int cut = 0; cut < 12; ++cut)
                        c.line(ex - 16 + cut, cy - h / 2, ex - 16 + cut,
                               cy - h / 2 + (side < 0 ? 11 - cut : cut), black);
            }
        }
    }
    else
    {
        int face_y = 32 + bob;
        PixelOp color = white;
        if (style_ == 1)
        {
            // A round cat with pointed ears, a curled tail and tiny paws.
            c.line(cx + 20, 53 + bob, cx + 34, 45 + bob);
            c.line(cx + 34, 45 + bob, cx + 35, 34 + bob);
            c.line(cx + 35, 34 + bob, cx + 29, 31 + bob);
            c.rounded(cx - 17, 42 + bob, 35, 19, white, true);
            c.rounded(cx - 24, 17 + bob, 49, 32, white, true);
            c.line(cx - 24, 26 + bob, cx - 22, 7 + bob);
            c.line(cx - 22, 7 + bob, cx - 8, 18 + bob);
            c.line(cx + 24, 26 + bob, cx + 22, 7 + bob);
            c.line(cx + 22, 7 + bob, cx + 8, 18 + bob);
            c.line(cx - 20, 19 + bob, cx - 19, 13 + bob);
            c.line(cx - 19, 13 + bob, cx - 13, 18 + bob);
            c.line(cx + 20, 19 + bob, cx + 19, 13 + bob);
            c.line(cx + 19, 13 + bob, cx + 13, 18 + bob);
            c.rounded(cx - 22, 19 + bob, 45, 28, black, true);
            c.rounded(cx - 15, 47 + bob, 31, 12, black, true);
            c.line(cx - 6, 56 + bob, cx - 6, 60 + bob);
            c.line(cx + 6, 56 + bob, cx + 6, 60 + bob);
            for (int side : {-1, 1})
            {
                c.line(cx + side * 17, 36 + bob, cx + side * 29, 33 + bob);
                c.line(cx + side * 17, 39 + bob, cx + side * 29, 40 + bob);
            }
        }
        else if (style_ == 2)
        {
            // The bunny's long, slightly uneven ears move with its breathing.
            c.rounded(cx - 20, 3 + bob, 13, 31, white, true);
            c.rounded(cx + 7, 4 - breath, 13, 30 + bob + breath, white, true);
            c.rounded(cx - 16, 7 + bob, 5, 18, black, true);
            c.rounded(cx + 11, 9 - breath, 5, 17, black, true);
            c.rounded(cx - 16, 43 + bob, 33, 19, white, true);
            c.rounded(cx - 23, 24 + bob, 47, 29, white, true);
            c.rounded(cx - 21, 26 + bob, 43, 25, black, true);
            c.rounded(cx - 13, 49 + bob, 27, 11, black, true);
            c.rounded(cx - 22, 55 + bob, 15, 7, white, true);
            c.rounded(cx + 8, 55 + bob, 15, 7, white, true);
            ellipse(c, cx + 21, 50 + bob, 5, 5, white, false);
            face_y = 36 + bob;
        }
        else
        {
            // A soft, white mochi silhouette uses black facial features.
            ellipse(c, cx, 40 + bob, 37 + breath, 20 - breath);
            c.rounded(cx - 36 - breath, 41 + bob, 73 + breath * 2, 18 - breath, white, true);
            c.rounded(cx - 20, 20 + bob, 13, 4, black, true);
            c.rounded(cx - 5, 17 + bob, 11, 3, white, true);
            ellipse(c, cx - 13, 26 + bob, 4, 2, black);
            face_y = 38 + bob;
            color = black;
        }
        small_eye(c, cx - 11, face_y, action_, look, color);
        small_eye(c, cx + 11, face_y, action_, look, color);
        if (action_ == PetAction::surprised)
            ellipse(c, cx, face_y + 10, 3, 4, color, false);
        else if (action_ == PetAction::eat)
            ellipse(c, cx, face_y + 10, 4, elapsed / 150 % 2 ? 2 : 4, color);
        else if (action_ == PetAction::sad)
        {
            c.line(cx - 4, face_y + 11, cx, face_y + 9, color);
            c.line(cx, face_y + 9, cx + 4, face_y + 11, color);
        }
        else
        {
            c.line(cx - 5, face_y + 7, cx - 2, face_y + 10, color);
            c.line(cx - 2, face_y + 10, cx, face_y + 8, color);
            c.line(cx, face_y + 8, cx + 2, face_y + 10, color);
            c.line(cx + 2, face_y + 10, cx + 5, face_y + 7, color);
        }
    }

    if (action_ == PetAction::sleep)
    {
        const int lift = int(now / 600 % 3) * 2;
        c.text(x + 102, 15 - lift, "Z");
        c.text(x + 112, 5 - lift, "z");
        sparkle(c, x + 21, 15);
    }
    else if (action_ == PetAction::happy)
    {
        heart(c, x + 13, 10 - int(elapsed / 200 % 3));
        heart(c, x + 105, 23 - int(elapsed / 180 % 3));
    }
    else if (action_ == PetAction::eat)
    {
        const int cy = 52 - int(elapsed / 160 % 2) * 2;
        if (style_ == 3) ellipse(c, cx, cy, 10, 9, black);
        ellipse(c, cx, cy, 9, 8);
        ellipse(c, cx + 6, cy - 5, 4, 4, black);
        for (const int offset : {-4, 0, 4}) c.rect(cx + offset - 1, cy + offset / 2, 2, 2, black, true);
        if (elapsed / 160 % 2) sparkle(c, cx + 16, cy - 5);
    }
    else if (action_ == PetAction::surprised)
    {
        c.rect(x + 106, 9, 3, 11, white, true);
        c.rect(x + 106, 24, 3, 3, white, true);
        c.line(x + 20, 8, x + 15, 4);
        c.line(x + 18, 15, x + 11, 14);
    }
    else if (action_ == PetAction::sad)
        ellipse(c, cx + (style_ == 0 ? 38 : 17), 44 + int(elapsed / 140 % 6), 2, 4,
                style_ == 3 ? black : white);
    else if (action_ == PetAction::dance)
    {
        const int lift = int(elapsed / 180 % 2) * 4;
        c.line(x + 14, 23 + lift, x + 14, 36 + lift);
        c.line(x + 14, 23 + lift, x + 21, 21 + lift);
        ellipse(c, x + 11, 37 + lift, 3, 2);
        c.line(x + 110, 29 - lift, x + 110, 41 - lift);
        c.line(x + 110, 29 - lift, x + 116, 27 - lift);
        ellipse(c, x + 107, 42 - lift, 3, 2);
        sparkle(c, x + 26, 9 + lift);
    }
}
}  // namespace c3
