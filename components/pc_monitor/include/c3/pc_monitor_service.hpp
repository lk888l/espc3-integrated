#pragma once

#include "c3/pc_monitor.hpp"
#include "esp_err.h"

namespace c3::pc
{
esp_err_t initialize();
bool deinitialize();
Snapshot snapshot();
}  // namespace c3::pc
