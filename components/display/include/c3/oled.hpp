#pragma once
#include "c3/core.hpp"
#include "driver/spi_master.h"
#include "esp_err.h"
namespace c3
{
class Oled final
{
   public:
    esp_err_t initialize(uint8_t contrast);
    bool deinitialize();
    esp_err_t recover(uint8_t contrast);
    esp_err_t present(const Frame& frame, uint8_t contrast);
    uint32_t frames = 0, skipped = 0, errors = 0, last_us = 0;

   private:
    esp_err_t transfer(const uint8_t* bytes, std::size_t size, bool data);
    esp_err_t commands(std::span<const uint8_t> bytes);
    spi_device_handle_t device_ = nullptr;
    uint8_t* dma_ = nullptr;
    bool bus_ = false, valid_ = false;
    uint8_t contrast_ = 0;
    Frame previous_{};
};
}  // namespace c3
