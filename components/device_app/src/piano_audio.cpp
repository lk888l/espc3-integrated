#include "c3/piano_audio.hpp"

#include <array>
#include "c3/board.hpp"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"

namespace c3::device
{
bool PianoAudio::initialize()
{
    if (CONFIG_C3_BUZZER_GPIO < 0) return true;
    ledc_timer_config_t timer{};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution = LEDC_TIMER_10_BIT;
    timer.timer_num = LEDC_TIMER_0;
    timer.freq_hz = 262;
    timer.clk_cfg = LEDC_AUTO_CLK;
    if (ledc_timer_config(&timer) != ESP_OK) return false;
    ledc_channel_config_t channel{};
    channel.gpio_num = CONFIG_C3_BUZZER_GPIO;
    channel.speed_mode = LEDC_LOW_SPEED_MODE;
    channel.channel = LEDC_CHANNEL_0;
    channel.timer_sel = LEDC_TIMER_0;
    if (ledc_channel_config(&channel) != ESP_OK) return false;
    initialized_ = true;
    ESP_LOGI("piano", "Passive buzzer enabled on GPIO%d", CONFIG_C3_BUZZER_GPIO);
    return true;
}
void PianoAudio::stop()
{
    if (initialized_ && sounding_) ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    sounding_ = false;
}
void PianoAudio::update(bool playing, uint8_t note, uint32_t sequence, uint32_t now)
{
    if (!initialized_) return;
    if (!playing)
    {
        sequence_ = sequence;
        stop();
        return;
    }
    if (sequence != sequence_)
    {
        sequence_ = sequence;
        constexpr std::array<uint32_t, 8> hz{262, 294, 330, 349, 392, 440, 494, 523};
        if (note >= hz.size()) return;
        if (ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, hz[note]) != ESP_OK ||
            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 256) != ESP_OK ||
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0) != ESP_OK)
        {
            stop();
            return;
        }
        sounding_ = true;
        started_ = now;
    }
    if (sounding_ && now - started_ >= 180) stop();
}
void PianoAudio::deinitialize()
{
    stop();
    if (initialized_) gpio_reset_pin(gpio_num_t(CONFIG_C3_BUZZER_GPIO));
    initialized_ = false;
}
}  // namespace c3::device
