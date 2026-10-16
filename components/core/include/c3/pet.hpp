#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace c3
{
class Canvas;
struct KeyEvent;

enum class PetAction : uint8_t
{
    idle,
    blink,
    happy,
    eat,
    sleep,
    surprised,
    sad,
    dance,
    count
};

std::string_view pet_action_name(PetAction action);
std::string_view pet_style_name(uint8_t style);

// PET1 keeps up to 24 monochrome 64 x 32 frames. Keep this object in static
// storage or on the heap; PetSession only borrows it and never copies its data.
class PetPack
{
   public:
    static constexpr std::size_t header_bytes = 64;
    static constexpr std::size_t frame_bytes = 256;
    static constexpr std::size_t max_frames = 24;
    static constexpr std::size_t max_bytes = header_bytes + max_frames * frame_bytes;

    // A failed decode preserves the previously installed valid pack.
    bool decode(std::span<const uint8_t> bytes);
    bool valid() const { return size_ >= header_bytes + frame_bytes; }
    std::size_t size() const { return size_; }
    std::span<const uint8_t> bytes() const { return {raw_.data(), size_}; }
    std::string_view name() const;
    uint8_t frame_count(PetAction action) const;
    uint16_t interval_ms(PetAction action) const;
    std::span<const uint8_t> frame(PetAction action, uint32_t index) const;

   private:
    std::array<uint8_t, max_bytes> raw_{};
    uint16_t size_ = 0;
};

class PetSession
{
   public:
    static constexpr uint8_t builtin_styles = 4, custom_style = 4, style_count = 5;
    void tick(uint32_t now);
    void handle(KeyEvent event);
    bool select(uint8_t style, uint32_t now);
    void act(PetAction action, uint32_t now);
    void set_custom(const PetPack* pack);
    uint8_t style() const { return style_; }
    PetAction action() const { return action_; }
    uint32_t sequence() const { return sequence_; }
    bool has_custom() const { return custom_ && custom_->valid(); }
    std::string_view name() const;
    void draw(Canvas& canvas, uint32_t now, int x = 0) const;

   private:
    uint32_t duration() const;
    const PetPack* custom_ = nullptr;
    PetAction action_ = PetAction::idle;
    uint8_t style_ = 0;
    int8_t look_ = 0;
    bool initialized_ = false, enter_armed_ = false, waking_enter_ = false;
    uint32_t now_ = 0, action_at_ = 0, last_input_ = 0, next_blink_ = 0, look_at_ = 0;
    uint32_t sequence_ = 0;
};
}  // namespace c3
