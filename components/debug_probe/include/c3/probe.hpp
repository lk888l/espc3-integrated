#pragma once
#include <cstdint>

#include "esp_err.h"
namespace c3::dap
{
struct Snapshot
{
    bool enabled = false, ready = false, connected = false, swd = false;
    uint32_t packets = 0, errors = 0, configured_hz = 100000, requested_hz = 100000,
             limit_hz = 100000;
    uint8_t last_ack = 0;
    esp_err_t error = ESP_OK;
};
esp_err_t initialize();
bool deinitialize();
void enable(bool enabled);
void set_clock_limit(uint32_t hz);
Snapshot snapshot();
}  // namespace c3::dap
