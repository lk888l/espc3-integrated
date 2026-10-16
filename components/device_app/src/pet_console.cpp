#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

#include "c3/device_app.hpp"
#include "esp_timer.h"
#include "nvs.h"

namespace c3::device
{
namespace
{
constexpr const char* actions[] = {"idle", "blink", "happy", "eat", "sleep", "surprised", "sad", "dance"};
struct Upload
{
    std::unique_ptr<uint8_t[]> data;
    uint32_t size = 0, received = 0, crc = 0, touched = 0;
};
Upload upload;
uint32_t request_id = 0;
uint32_t now_ms() { return uint32_t(esp_timer_get_time() / 1000); }
int fail(const char* reason)
{
    std::printf("pet_error %s\n", reason);
    return 0;  // the protocol response is the error, without an extra console error line
}
bool number(const char* text, uint32_t& result, int base = 10)
{
    if (!text || !*text) return false;
    for (const char* p = text; *p; ++p)
        if (base == 10 ? (*p < '0' || *p > '9')
                       : !((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') ||
                           (*p >= 'A' && *p <= 'F')))
            return false;
    char* end = nullptr;
    errno = 0;
    const auto value = std::strtoul(text, &end, base);
    if (errno || *end || value > std::numeric_limits<uint32_t>::max()) return false;
    result = uint32_t(value);
    return true;
}
int nibble(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}
uint32_t crc32(std::span<const uint8_t> bytes)
{
    uint32_t crc = 0xffffffffU;
    for (auto byte : bytes)
    {
        crc ^= byte;
        for (unsigned i = 0; i < 8; ++i)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0);
    }
    return crc ^ 0xffffffffU;
}
bool apply(Runtime& runtime, PetOperation operation, uint8_t value = 0,
           std::unique_ptr<PetPack> pack = {})
{
    if (!runtime.pet_requests || !runtime.pet_results) return false;
    PetResult result;
    while (xQueueReceive(runtime.pet_results, &result, 0) == pdTRUE) {}
    const PetRequest request{operation, value, ++request_id, pack.get()};
    if (xQueueSend(runtime.pet_requests, &request, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    pack.release();
    const auto started = xTaskGetTickCount();
    const auto timeout = pdMS_TO_TICKS(1500);
    while (true)
    {
        const auto elapsed = xTaskGetTickCount() - started;
        if (elapsed >= timeout) break;
        const auto left = timeout - elapsed;
        if (xQueueReceive(runtime.pet_results, &result, left) != pdTRUE) break;
        if (result.id == request.id) return result.ok;
    }
    return false;
}
}  // namespace

int pet_command(Runtime& runtime, int argc, char** argv)
{
    if (upload.data && now_ms() - upload.touched > 30000) upload = {};
    if (argc == 2 && !std::strcmp(argv[1], "status"))
    {
        const auto ui = runtime.ui_snapshot();
        const auto action = ui.pet_action < std::size(actions) ? actions[ui.pet_action] : "idle";
        std::printf("pet_ok status active=%u style=%u action=%s sequence=%lu custom=%u bytes=%lu\n",
                    ui.view == View::pet, ui.pet_style, action, (unsigned long)ui.pet_sequence,
                    ui.pet_bytes != 0, (unsigned long)ui.pet_bytes);
        return 0;
    }
    if (argc == 3 && !std::strcmp(argv[1], "open"))
    {
        uint32_t style;
        if (!number(argv[2], style) || style > 4) return fail("invalid_style");
        if (!apply(runtime, PetOperation::open, uint8_t(style))) return fail("style_unavailable_or_busy");
        std::printf("pet_ok open style=%lu\n", (unsigned long)style);
        return 0;
    }
    if (argc == 3 && !std::strcmp(argv[1], "action"))
    {
        unsigned action = 0;
        while (action < std::size(actions) && std::strcmp(argv[2], actions[action])) ++action;
        if (action == std::size(actions)) return fail("invalid_action");
        if (!apply(runtime, PetOperation::action, uint8_t(action))) return fail("pet_inactive_or_busy");
        std::printf("pet_ok action %s\n", actions[action]);
        return 0;
    }
    if (argc == 2 && !std::strcmp(argv[1], "close"))
    {
        if (!apply(runtime, PetOperation::close)) return fail("busy");
        std::printf("pet_ok close\n");
        return 0;
    }
    if (argc == 2 && !std::strcmp(argv[1], "abort"))
    {
        upload = {};
        std::printf("pet_ok abort\n");
        return 0;
    }
    if (argc == 4 && !std::strcmp(argv[1], "begin"))
    {
        uint32_t size, crc;
        if (!number(argv[2], size) || size < 320 || size > PetPack::max_bytes ||
            (size - 64) % 256 || std::strlen(argv[3]) != 8 || !number(argv[3], crc, 16))
            return fail("invalid_size_or_crc");
        auto data = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[size]);
        if (!data) return fail("no_memory");
        upload = {std::move(data), size, 0, crc, now_ms()};
        std::printf("pet_ok begin bytes=%lu\n", (unsigned long)size);
        return 0;
    }
    if (argc == 4 && !std::strcmp(argv[1], "chunk"))
    {
        if (!upload.data) return fail("no_upload");
        uint32_t offset;
        const auto length = std::strlen(argv[3]);
        if (!number(argv[2], offset) || offset != upload.received || !length || length > 128 ||
            length % 2 || length / 2 > upload.size - upload.received)
            return fail("invalid_offset_or_chunk");
        for (std::size_t i = 0; i < length; ++i)
            if (nibble(argv[3][i]) < 0) return fail("invalid_hex");
        for (std::size_t i = 0; i < length / 2; ++i)
            upload.data[upload.received + i] = uint8_t(nibble(argv[3][2 * i]) * 16 + nibble(argv[3][2 * i + 1]));
        upload.received += length / 2;
        upload.touched = now_ms();
        std::printf("pet_ok chunk next=%lu\n", (unsigned long)upload.received);
        return 0;
    }
    if (argc == 2 && !std::strcmp(argv[1], "commit"))
    {
        if (!upload.data) return fail("no_upload");
        if (upload.received != upload.size) return fail("incomplete_upload");
        const auto bytes = std::span<const uint8_t>(upload.data.get(), upload.size);
        if (crc32(bytes) != upload.crc) return fail("crc_mismatch");
        auto pack = std::unique_ptr<PetPack>(new (std::nothrow) PetPack);
        if (!pack) return fail("no_memory");
        if (!pack->decode(bytes)) return fail("invalid_pack");
        nvs_handle_t handle;
        auto result = nvs_open("pet", NVS_READWRITE, &handle);
        if (result == ESP_OK)
        {
            result = nvs_set_blob(handle, "pack", bytes.data(), bytes.size());
            if (result == ESP_OK) result = nvs_commit(handle);
            nvs_close(handle);
        }
        if (result != ESP_OK) return fail("storage_failed");
        const auto size = upload.size, crc = upload.crc;
        if (!apply(runtime, PetOperation::install, 4, std::move(pack))) return fail("saved_apply_failed");
        upload = {};
        std::printf("pet_ok commit bytes=%lu crc=%08lx\n", (unsigned long)size, (unsigned long)crc);
        return 0;
    }
    return fail("usage_status_open_action_close_begin_chunk_commit_abort");
}
}  // namespace c3::device
