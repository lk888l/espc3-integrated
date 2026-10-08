#include "c3/oled.hpp"

#include <cstring>

#include "c3/board.hpp"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
namespace c3
{
esp_err_t Oled::initialize(uint8_t contrast)
{
    gpio_config_t pins{};
    pins.pin_bit_mask = (1ULL << CONFIG_C3_OLED_DC) | (1ULL << CONFIG_C3_OLED_RESET);
    pins.mode = GPIO_MODE_OUTPUT;
    esp_err_t result = gpio_config(&pins);
    if (result != ESP_OK) return result;
    dma_ = static_cast<uint8_t*>(
        heap_caps_malloc(frame_bytes + 32, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (!dma_) return ESP_ERR_NO_MEM;
    spi_bus_config_t bus{};
    bus.sclk_io_num = CONFIG_C3_OLED_SCLK;
    bus.mosi_io_num = CONFIG_C3_OLED_MOSI;
    bus.miso_io_num = bus.quadwp_io_num = bus.quadhd_io_num = -1;
    bus.data4_io_num = bus.data5_io_num = bus.data6_io_num = bus.data7_io_num = -1;
    bus.max_transfer_sz = frame_bytes;
    bus.flags = SPICOMMON_BUSFLAG_MASTER;
    result = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (result != ESP_OK) return result;
    bus_ = true;
    spi_device_interface_config_t device{};
    device.clock_speed_hz = CONFIG_C3_OLED_CLOCK_MHZ * 1000000;
    device.mode = 0;
    device.spics_io_num = CONFIG_C3_OLED_CS;
    device.queue_size = 1;
    device.flags = SPI_DEVICE_HALFDUPLEX;
    result = spi_bus_add_device(SPI2_HOST, &device, &device_);
    if (result != ESP_OK) return result;
    return recover(contrast);
}
esp_err_t Oled::transfer(const uint8_t* bytes, std::size_t size, bool data)
{
    if (!device_) return ESP_ERR_INVALID_STATE;
    gpio_set_level(gpio_num_t(CONFIG_C3_OLED_DC), data);
    spi_transaction_t transaction{};
    transaction.length = size * 8;
    transaction.tx_buffer = bytes;
    // IRQ-backed DMA blocks only this owner; input and Wi-Fi remain runnable.
    return spi_device_transmit(device_, &transaction);
}
esp_err_t Oled::commands(std::span<const uint8_t> bytes)
{
    if (!dma_ || bytes.size() > 32) return ESP_ERR_INVALID_SIZE;
    std::memcpy(dma_ + frame_bytes, bytes.data(), bytes.size());
    return transfer(dma_ + frame_bytes, bytes.size(), false);
}
esp_err_t Oled::recover(uint8_t contrast)
{
    valid_ = false;
    gpio_set_level(gpio_num_t(CONFIG_C3_OLED_RESET), 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(gpio_num_t(CONFIG_C3_OLED_RESET), 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(gpio_num_t(CONFIG_C3_OLED_RESET), 1);
    vTaskDelay(pdMS_TO_TICKS(20));
    const uint8_t init[] = {0xae, 0xd5, 0xf0, 0xa8, 0x3f, 0xd3, 0x00, 0x40, 0x8d,
                            0x14, 0x20, 0x00, 0xa1, 0xc8, 0xda, 0x12, 0x81, contrast,
                            0xd9, 0xf1, 0xdb, 0x40, 0xa4, 0xa6, 0xaf};
    const auto result = commands(init);
    if (result == ESP_OK)
        contrast_ = contrast;
    else
        ++errors;
    return result;
}
esp_err_t Oled::present(const Frame& frame, uint8_t contrast)
{
    if (contrast != contrast_)
    {
        const uint8_t cmd[] = {0x81, contrast};
        const auto result = commands(cmd);
        if (result != ESP_OK)
        {
            ++errors;
            valid_ = false;
            return result;
        }
        contrast_ = contrast;
    }
    if (valid_ && frame == previous_)
    {
        ++skipped;
        return ESP_OK;
    }
    const int64_t started = esp_timer_get_time();
    const uint8_t range[] = {0x21, 0, 127, 0x22, 0, 7};
    auto result = commands(range);
    if (result == ESP_OK)
    {
        std::memcpy(dma_, frame.data(), frame.size());
        result = transfer(dma_, frame.size(), true);
    }
    last_us = uint32_t(esp_timer_get_time() - started);
    if (result == ESP_OK)
    {
        previous_ = frame;
        valid_ = true;
        ++frames;
    }
    else
    {
        valid_ = false;
        ++errors;
    }
    return result;
}
bool Oled::deinitialize()
{
    if (device_)
    {
        if (spi_bus_remove_device(device_) != ESP_OK) return false;
        device_ = nullptr;
    }
    if (bus_)
    {
        if (spi_bus_free(SPI2_HOST) != ESP_OK) return false;
        bus_ = false;
    }
    if (dma_)
    {
        heap_caps_free(dma_);
        dma_ = nullptr;
    }
    gpio_reset_pin(gpio_num_t(CONFIG_C3_OLED_DC));
    gpio_reset_pin(gpio_num_t(CONFIG_C3_OLED_RESET));
    valid_ = false;
    return true;
}
}  // namespace c3
