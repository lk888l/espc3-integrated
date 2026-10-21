#include "c3/pc_monitor_service.hpp"

#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

#include "app_task.hpp"
#include "c3/network.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/sockets.h"

namespace c3::pc
{
namespace
{
constexpr char tag[] = "pc_monitor";
portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
Snapshot state;
uint32_t received_at = 0;
bool initialized = false;
uint32_t now_ms() { return uint32_t(esp_timer_get_time() / 1000); }
template <class F> void update(F fn)
{
    portENTER_CRITICAL(&state_lock);
    fn(state);
    portEXIT_CRITICAL(&state_lock);
}

class Receiver final : public AppTask
{
   public:
    Receiver() : AppTask("pc_monitor", 6144, 2, 0) {}

   private:
    int socket_ = -1;
    uint32_t retry_at_ = 0;
    LatestSample latest_;
    char packet_[max_packet_bytes]{};
    void close_transport()
    {
        if (socket_ >= 0) close(socket_);
        socket_ = -1;
        latest_.reset();
        update([](Snapshot& s) { s.listening = false; s.received = false; s.stale = true; s.stats = {}; });
    }
    void failed(int error)
    {
        close_transport();
        retry_at_ = now_ms() + 1000;
        update([&](Snapshot& s) { s.error = error; });
        ESP_LOGW(tag, "UDP transport failed: errno=%d", error);
    }
    void open_transport()
    {
        socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socket_ < 0) { failed(errno); return; }
        int one = 1;
        setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in local{};
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = htonl(INADDR_ANY);
        local.sin_port = htons(udp_port);
        if (bind(socket_, reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0 ||
            fcntl(socket_, F_SETFL, O_NONBLOCK) < 0) { failed(errno); return; }
        update([](Snapshot& s) { s.listening = true; s.error = 0; });
        ESP_LOGI(tag, "PC monitor UDP listening on %u", udp_port);
    }
    void poll()
    {
        // Bound each turn so a packet flood cannot starve UI or other services.
        for (unsigned i = 0; i < 4 && !should_exit(); ++i)
        {
            sockaddr_in peer{};
            socklen_t peer_size = sizeof(peer);
            const int count = recvfrom(socket_, packet_, sizeof(packet_), MSG_DONTWAIT,
                                       reinterpret_cast<sockaddr*>(&peer), &peer_size);
            if (count < 0)
            {
                if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) failed(errno);
                return;
            }
            Stats sample;
            // A datagram filling this buffer may have been truncated. Valid v1
            // payloads are strictly smaller than the buffer, so reject it.
            if (count <= 0 || std::size_t(count) >= sizeof(packet_) ||
                !parse_sample(std::string_view(packet_, std::size_t(count)), sample))
            {
                update([](Snapshot& s) { ++s.rejected; });
                continue;
            }
            const auto now = now_ms();
            if (!latest_.accept(sample, now))
            {
                update([](Snapshot& s) { ++s.out_of_order; });
                continue;
            }
            portENTER_CRITICAL(&state_lock);
            state.stats = sample;
            state.received = true;
            state.stale = false;
            ++state.accepted;
            received_at = now;
            portEXIT_CRITICAL(&state_lock);
            char acknowledgement[112];
            const int length = std::snprintf(acknowledgement, sizeof(acknowledgement),
                "{\"v\":1,\"session\":\"%s\",\"seq\":%lu,\"ack\":true}",
                sample.session, static_cast<unsigned long>(sample.seq));
            const auto sent = sendto(socket_, acknowledgement, std::size_t(length), MSG_DONTWAIT,
                                     reinterpret_cast<const sockaddr*>(&peer), peer_size);
            const int error = sent == length ? 0 : errno;
            update([&](Snapshot& s) { s.error = error; });
        }
    }
    void main() override
    {
        retry_at_ = now_ms();
        latest_.reset();
        while (!should_exit())
        {
            if (!network::enabled_requested())
            {
                if (socket_ >= 0) close_transport();
                retry_at_ = now_ms();
            }
            else
            {
                if (socket_ < 0 && int32_t(now_ms() - retry_at_) >= 0) open_transport();
                if (socket_ >= 0) poll();
            }
            const auto free_stack = uint32_t(uxTaskGetStackHighWaterMark(nullptr));
            update([&](Snapshot& s) { s.stack_free = free_stack; });
            vTaskDelay(pdMS_TO_TICKS(25));
        }
        close_transport();
    }
};
Receiver receiver;
}  // namespace

esp_err_t initialize()
{
    if (initialized) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&state_lock);
    state = {};
    received_at = 0;
    portEXIT_CRITICAL(&state_lock);
    if (!receiver.start()) return ESP_ERR_NO_MEM;
    initialized = true;
    return ESP_OK;
}
bool deinitialize()
{
    if (!initialized) return true;
    if (!receiver.stop()) return false;
    initialized = false;
    return true;
}
Snapshot snapshot()
{
    Snapshot result;
    uint32_t at;
    portENTER_CRITICAL(&state_lock);
    result = state;
    at = received_at;
    portEXIT_CRITICAL(&state_lock);
    result.age_ms = result.received ? now_ms() - at : 0;
    result.stale = !result.listening || !result.received || result.age_ms >= stale_after_ms;
    return result;
}
}  // namespace c3::pc
