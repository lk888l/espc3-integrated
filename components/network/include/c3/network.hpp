#pragma once
#include "c3/core.hpp"
#include "esp_err.h"
namespace c3::network
{
esp_err_t initialize();
bool deinitialize();
void set_enabled(bool enabled);
bool enabled_requested();
bool radio_running();
bool set_credentials(const char* ssid, const char* password);
enum class SubmitResult { queued, invalid, busy, unconfigured };
SubmitResult try_credentials(const char* ssid, const char* password);
SubmitResult reconnect();
void sync_time();
Telemetry snapshot();
}  // namespace c3::network
