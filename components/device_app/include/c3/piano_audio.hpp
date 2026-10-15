#pragma once
#include <cstdint>

namespace c3::device
{
// Optional passive buzzer; the UI task owns all PWM operations.
class PianoAudio
{
   public:
    bool initialize();
    void update(bool playing, uint8_t note, uint32_t sequence, uint32_t now);
    void stop();
    void deinitialize();

   private:
    bool initialized_ = false, sounding_ = false;
    uint32_t sequence_ = 0, started_ = 0;
};
}  // namespace c3::device
