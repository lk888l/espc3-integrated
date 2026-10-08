#include "c3/probe.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>

#include "app_task.hpp"
#include "c3/board.hpp"
#include "c3/dap_protocol.hpp"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "lwip/tcp.h"
extern "C"
{
#include "DAP.h"
#include "DAP_config.h"
}
namespace c3::dap
{
namespace
{
constexpr char tag[] = "wifi_dap";
portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
Snapshot state;
std::atomic<bool> wanted{false};
std::atomic<uint32_t> generation{0}, limit{100000};
uint32_t command_generation = 0, requested_clock = 100000;
int64_t deadline = 0, last_poll = 0;
int abort_socket = -1;
char serial[13]{};
bool initialized = false;
template <class F>
void update(F fn)
{
    portENTER_CRITICAL(&state_lock);
    fn(state);
    portEXIT_CRITICAL(&state_lock);
}
void reset_session()
{
    DAP_Setup();
    update(
        [](Snapshot& s)
        {
            s.connected = false;
            s.swd = false;
        });
}
Packet execute(const Packet& request)
{
    Packet response;
    if (!wanted || command_generation != generation) return response;
    deadline = esp_timer_get_time() + 200000;
    last_poll = 0;
    if (!valid_request(request.data, request.size))
    {
        response.data[0] = 0xff;
        response.size = 1;
        update([](Snapshot& s) { ++s.errors; });
    }
    else if (request.data[0] == 0x07)
    {
        DAP_TransferAbort = 1;
    }
    else if (request.data[0] == 0x11 && read32(request.data + 1) < 1000)
    {
        response.data[0] = 0x11;
        response.data[1] = 0xff;
        response.size = 2;
    }
    else
    {
        const uint8_t id = request.data[0];
        const bool touches_target =
            id == 5 || id == 6 || id == 8 || id == 0x0a || id == 0x10 || id == 0x12 || id == 0x1d;
        if (touches_target && DAP_Data.debug_port != DAP_PORT_SWD)
        {
            response.data[0] = 0xff;
            response.size = 1;
        }
        else
        {
            uint8_t padded[packet_size]{};
            std::memcpy(padded, request.data, request.size);
            if (id == 0x10) write32(padded + 3, std::min<uint32_t>(read32(padded + 3), 100000));
            DAP_Data.clock_delay = probe_set_clock(requested_clock);
            response.size = DAP_ProcessCommand(padded, response.data) & 0xffff;
            if ((id == 0x12 || id == 0x1d) && probe_cancelled())
            {
                response.data[1] = 0xff;
                response.size = 2;
            }
            if (response.size > packet_size)
            {
                response = {};
                response.data[0] = 0xff;
                response.size = 1;
            }
        }
    }
    deadline = 0;
    update(
        [&](Snapshot& s)
        {
            ++s.packets;
            s.swd = DAP_Data.debug_port == DAP_PORT_SWD;
            if (response.size >= 3 && response.data[0] == 5) s.last_ack = response.data[2];
            if (response.size >= 4 && response.data[0] == 6) s.last_ack = response.data[3];
            if ((request.data[0] == 5 || request.data[0] == 6) && s.last_ack != 1) ++s.errors;
        });
    return response;
}
class Worker final : public AppTask
{
   public:
    Worker() : AppTask("wifi_dap", 6144, 2, 0) {}

   private:
    int server = -1, discovery = -1, client = -1;
    uint32_t session = 0;
    uint8_t header[8]{}, outgoing[8 + packet_size]{};
    std::size_t header_used = 0, body_used = 0, outgoing_size = 0, outgoing_sent = 0;
    Packet input{};
    int64_t partial_since = 0, sending_since = 0, next_retry = 0;
    void close_client()
    {
        abort_socket = -1;
        if (client >= 0)
        {
            shutdown(client, SHUT_RDWR);
            close(client);
            client = -1;
        }
        header_used = body_used = outgoing_size = outgoing_sent = 0;
        partial_since = sending_since = 0;
        input = {};
        reset_session();
    }
    void close_all()
    {
        close_client();
        if (server >= 0)
        {
            close(server);
            server = -1;
        }
        if (discovery >= 0)
        {
            close(discovery);
            discovery = -1;
        }
        update(
            [](Snapshot& s)
            {
                s.ready = false;
                s.enabled = false;
            });
    }
    esp_err_t listen_socket(int& fd, int type, uint16_t port)
    {
        fd = socket(AF_INET, type, type == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP);
        if (fd < 0) return ESP_FAIL;
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) ||
            (type == SOCK_STREAM && listen(fd, 1)) || fcntl(fd, F_SETFL, O_NONBLOCK) < 0)
        {
            close(fd);
            fd = -1;
            return ESP_FAIL;
        }
        return ESP_OK;
    }
    void start_transport()
    {
        reset_session();
        esp_err_t result = listen_socket(server, SOCK_STREAM, tcp_port);
        if (result == ESP_OK) result = listen_socket(discovery, SOCK_DGRAM, discovery_port);
        if (result != ESP_OK) close_all();
        update(
            [&](Snapshot& s)
            {
                s.enabled = true;
                s.ready = result == ESP_OK;
                s.error = result;
            });
        if (result == ESP_OK)
            ESP_LOGI(tag, "CMSIS-DAP TCP listening on %u", tcp_port);
        else
            ESP_LOGE(tag, "could not start transport");
        next_retry = esp_timer_get_time() + 1000000;
    }
    void poll_discovery()
    {
        uint8_t query[sizeof(discovery_request)];
        sockaddr_in peer{};
        socklen_t length = sizeof(peer);
        const int n = recvfrom(discovery, query, sizeof(query), 0,
                               reinterpret_cast<sockaddr*>(&peer), &length);
        if (n < 0 || !discovery_query(query, std::size_t(n))) return;
        const auto reply = discovery_reply(serial);
        sendto(discovery, reply.data(), reply.size(), 0, reinterpret_cast<sockaddr*>(&peer),
               length);
    }
    void poll_tcp()
    {
        const int peer = accept(server, nullptr, nullptr);
        if (peer >= 0)
        {
            if (client >= 0)
                close(peer);
            else
            {
                client = peer;
                abort_socket = peer;
                if (fcntl(peer, F_SETFL, O_NONBLOCK) < 0)
                {
                    close_client();
                    return;
                }
                int one = 1;
                setsockopt(peer, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
                setsockopt(peer, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
                int idle = 30, interval = 5, count = 3;
                setsockopt(peer, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
                setsockopt(peer, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval));
                setsockopt(peer, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(count));
                reset_session();
                update([](Snapshot& s) { s.connected = true; });
            }
        }
        if (client < 0) return;
        const auto now = esp_timer_get_time();
        if ((partial_since && now - partial_since > 2000000) ||
            (sending_since && now - sending_since > 2000000))
        {
            close_client();
            return;
        }
        if (outgoing_size)
        {
            const int n = send(client, outgoing + outgoing_sent, outgoing_size - outgoing_sent, 0);
            if (n > 0)
                outgoing_sent += n;
            else if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
            {
                close_client();
                return;
            }
            if (outgoing_sent == outgoing_size)
            {
                outgoing_size = outgoing_sent = 0;
                sending_since = 0;
            }
            return;
        }
        uint8_t* target = header_used < 8 ? header + header_used : input.data + body_used;
        const std::size_t capacity = header_used < 8 ? 8 - header_used : input.size - body_used;
        const int n = recv(client, target, capacity, 0);
        if (n == 0)
        {
            close_client();
            return;
        }
        if (n < 0)
        {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) close_client();
            return;
        }
        if (!partial_since) partial_since = now;
        if (header_used < 8)
        {
            header_used += n;
            if (header_used == 8 && !tcp_length(header, input.size))
            {
                update([](Snapshot& s) { ++s.errors; });
                close_client();
                return;
            }
        }
        else
            body_used += n;
        if (header_used == 8 && body_used == input.size)
        {
            command_generation = session;
            const auto reply = execute(input);
            header_used = body_used = 0;
            partial_since = 0;
            input = {};
            if (reply.size && wanted && session == generation)
            {
                tcp_header(outgoing, reply.size);
                std::memcpy(outgoing + 8, reply.data, reply.size);
                outgoing_size = 8 + reply.size;
                outgoing_sent = 0;
                sending_since = now;
            }
        }
    }
    void main() override
    {
        reset_session();
        while (!should_exit())
        {
            if (session != generation.load())
            {
                close_all();
                session = generation.load();
                next_retry = 0;
            }
            if (!wanted)
            {
                if (server >= 0) close_all();
            }
            else
            {
                if (server < 0 && esp_timer_get_time() >= next_retry) start_transport();
                if (server >= 0)
                {
                    poll_discovery();
                    poll_tcp();
                }
            }
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        close_all();
    }
};
Worker worker;
}  // namespace
esp_err_t initialize()
{
    if (initialized) return ESP_ERR_INVALID_STATE;
    uint8_t mac[6];
    auto result = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (result != ESP_OK) return result;
    std::snprintf(serial, sizeof(serial), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2],
                  mac[3], mac[4], mac[5]);
    wanted = false;
    if (!worker.start()) return ESP_ERR_NO_MEM;
    initialized = true;
    return ESP_OK;
}
bool deinitialize()
{
    enable(false);
    if (!worker.stop()) return false;
    initialized = false;
    return true;
}
void enable(bool enabled)
{
    if (wanted.exchange(enabled) != enabled) generation.fetch_add(1);
}
void set_clock_limit(uint32_t hz)
{
    limit = std::clamp<uint32_t>(hz, 1000, 2000000);
    update([](Snapshot& s) { s.limit_hz = limit.load(); });
}
Snapshot snapshot()
{
    Snapshot copy;
    portENTER_CRITICAL(&state_lock);
    copy = state;
    portEXIT_CRITICAL(&state_lock);
    return copy;
}
}  // namespace c3::dap

extern "C" uint32_t probe_set_clock(uint32_t hz)
{
    using namespace c3::dap;
    requested_clock = hz;
    const auto configured = std::clamp<uint32_t>(hz, 1000, limit.load());
    update(
        [&](Snapshot& s)
        {
            s.requested_hz = hz;
            s.configured_hz = configured;
            s.limit_hz = limit.load();
        });
    return CPU_CLOCK / (2 * configured);
}
extern "C" void probe_port_off()
{
    gpio_reset_pin(gpio_num_t(CONFIG_C3_SWD_CLK));
    gpio_set_pull_mode(gpio_num_t(CONFIG_C3_SWD_CLK), GPIO_FLOATING);
    gpio_reset_pin(gpio_num_t(CONFIG_C3_SWD_IO));
    gpio_set_pull_mode(gpio_num_t(CONFIG_C3_SWD_IO), GPIO_FLOATING);
#if CONFIG_C3_SWD_RESET >= 0
    gpio_reset_pin(gpio_num_t(CONFIG_C3_SWD_RESET));
    gpio_set_pull_mode(gpio_num_t(CONFIG_C3_SWD_RESET), GPIO_FLOATING);
#endif
}
extern "C" void probe_port_swd()
{
    gpio_set_level(gpio_num_t(CONFIG_C3_SWD_CLK), 1);
    gpio_set_level(gpio_num_t(CONFIG_C3_SWD_IO), 1);
    gpio_config_t pins{};
    pins.pin_bit_mask = (1ULL << CONFIG_C3_SWD_CLK) | (1ULL << CONFIG_C3_SWD_IO);
    pins.mode = GPIO_MODE_INPUT_OUTPUT;
    gpio_config(&pins);
#if CONFIG_C3_SWD_RESET >= 0
    gpio_set_level(gpio_num_t(CONFIG_C3_SWD_RESET), 1);
    gpio_set_direction(gpio_num_t(CONFIG_C3_SWD_RESET), GPIO_MODE_INPUT_OUTPUT_OD);
#endif
}
extern "C" uint8_t probe_serial(char* text)
{
    std::memcpy(text, c3::dap::serial, 13);
    return 13;
}
extern "C" uint8_t probe_reset()
{
#if CONFIG_C3_SWD_RESET >= 0
    PIN_nRESET_OUT(0);
    vTaskDelay(pdMS_TO_TICKS(2));
    PIN_nRESET_OUT(1);
    return 1;
#else
    return 0;
#endif
}
extern "C" void probe_host_status(unsigned, unsigned) {}
extern "C" int probe_cancelled()
{
    using namespace c3::dap;
    const int64_t now = esp_timer_get_time();
    if (!wanted || command_generation != generation || (deadline && now >= deadline)) return 1;
    if (abort_socket >= 0 && now - last_poll >= 1000)
    {
        last_poll = now;
        uint8_t bytes[9];
        const int n = recv(abort_socket, bytes, sizeof(bytes), MSG_PEEK | MSG_DONTWAIT);
        if (n == 0) return 1;
        uint16_t length = 0;
        if (n == 9 && tcp_length(bytes, length) && length == 1 && bytes[8] == 7)
        {
            recv(abort_socket, bytes, sizeof(bytes), MSG_DONTWAIT);
            DAP_TransferAbort = 1;
            return 1;
        }
    }
    return 0;
}
