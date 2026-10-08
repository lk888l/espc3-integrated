#pragma once
#include "c3/core.hpp"
#include "esp_err.h"
namespace c3::network
{
esp_err_t initialize();
bool deinitialize();
void set_enabled(bool enabled);
bool enabled_requested();
bool set_credentials(const char* ssid, const char* password);
void sync_time();
Telemetry snapshot();
}  // namespace c3::network
