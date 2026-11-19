#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <strings.h>
#include <unistd.h>

#include "c3/device_app.hpp"
#include "c3/network.hpp"
#include "c3/pairing.hpp"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/sockets.h"

namespace c3::device
{
namespace
{
constexpr char tag[] = "local_api";
constexpr size_t max_body = 640;
constexpr int discovery_port = 4444;
extern const char page_html[] asm("_binary_control_html_start");
extern const char page_gzip[] asm("_binary_control_html_gz_start");
extern const char page_gzip_end[] asm("_binary_control_html_gz_end");
extern const char page_policy[] asm("_binary_control_csp_start");

esp_err_t json(httpd_req_t* req, const char* body)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, body);
}
esp_err_t error(httpd_req_t* req, const char* status, const char* reason)
{
    char body[96];
    std::snprintf(body, sizeof(body), "{\"error\":\"%s\"}", reason);
    httpd_resp_set_status(req, status);
    httpd_resp_set_hdr(req, "Connection", "close");
    json(req, body);
    return ESP_FAIL;  // close even if the request contained unread body bytes
}
esp_err_t asset(httpd_req_t* req)
{
    char accept[96]{};
    bool gzip = false;
    if (httpd_req_get_hdr_value_str(req, "Accept-Encoding", accept, sizeof(accept)) == ESP_OK)
    {
        char* cursor = nullptr;
        for (char* part = strtok_r(accept, ", ", &cursor); part; part = strtok_r(nullptr, ", ", &cursor))
            if (!strcasecmp(part, "gzip")) gzip = true;
    }
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Vary", "Accept-Encoding");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "Content-Security-Policy", page_policy);
    if (gzip)
    {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
        return httpd_resp_send(req, page_gzip, page_gzip_end - page_gzip);
    }
    return httpd_resp_sendstr(req, page_html);
}
esp_err_t read_body(httpd_req_t* req, char (&body)[max_body])
{
    char type[80]{};
    if (httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type)) == ESP_OK)
    {
        if (auto* params = std::strchr(type, ';')) *params = 0;
        for (size_t n = std::strlen(type); n && type[n - 1] == ' '; --n) type[n - 1] = 0;
    }
    if (strcasecmp(type, "application/json"))
        return error(req, "415 Unsupported Media Type", "json_required");
    if (req->content_len > sizeof(body))
        return error(req, "413 Content Too Large", "body_too_large");
    if (!req->content_len)
        return error(req, "400 Bad Request", "empty_body");
    const int64_t until = esp_timer_get_time() + 2000000;
    for (size_t used = 0; used < req->content_len;)
    {
        const int n = httpd_req_recv(req, body + used, req->content_len - used);
        if (n <= 0 || esp_timer_get_time() >= until)
            return error(req, "408 Request Timeout", "incomplete_body");
        used += size_t(n);
    }
    return ESP_OK;
}
esp_err_t status(httpd_req_t* req, Runtime& runtime)
{
    const auto ui = runtime.ui_snapshot();
    const auto net = network::snapshot();
    constexpr const char* states[] = {"idle", "trying", "saved", "failed", "storage_error"};
    const auto boolean = [](bool value) { return value ? "true" : "false"; };
    char body[512];
    std::snprintf(body, sizeof(body),
        "{\"v\":1,\"id\":\"%s\",\"view\":\"%s\","
        "\"pet\":{\"active\":%s,\"style\":%u,\"action\":\"%s\",\"sequence\":%lu,\"custom\":%s},"
        "\"display\":{\"brightness\":%u,\"motion\":%u,\"header\":%u},"
        "\"wifi\":{\"ap_ip\":\"%s\",\"sta_ip\":\"%s\",\"connected\":%s,\"configured\":%s,\"rssi\":%d,\"provision\":\"%s\"}}",
        net.ap_ssid, title(ui.view).data(), boolean(ui.view == View::pet), ui.pet_style,
        pet_action_name(PetAction(ui.pet_action)).data(), (unsigned long)ui.pet_sequence,
        boolean(ui.pet_bytes != 0), ui.settings.brightness, unsigned(ui.settings.motion),
        unsigned(ui.settings.header), net.ap_ip, net.sta_ip, boolean(net.station_connected),
        boolean(net.station_configured), net.station_connected ? net.rssi : 0, states[unsigned(net.provisioning)]);
    return json(req, body);
}
esp_err_t info(httpd_req_t* req)
{
    const auto net = network::snapshot();
    char body[180];
    std::snprintf(body, sizeof(body),
        "{\"v\":1,\"id\":\"%s\",\"model\":\"ESP32-C3 GameBox\",\"auth\":\"pair-code\","
        "\"capabilities\":[\"pet\",\"pet.frame\",\"display\",\"wifi.configure\"]}", net.ap_ssid);
    return json(req, body);
}
esp_err_t pair(httpd_req_t* req)
{
    char body[max_body];
    if (read_body(req, body) != ESP_OK) return ESP_FAIL;
    uint32_t code;
    if (!control::read_code({body, req->content_len}, code))
        return error(req, "400 Bad Request", "invalid_code_format");
    pairing::Token token;
    const auto result = pairing::exchange(code, token);
    if (result == pairing::Exchange::full) return error(req, "409 Conflict", "sessions_full");
    if (result != pairing::Exchange::ok) return error(req, "401 Unauthorized", "invalid_code");
    char reply[88];
    std::snprintf(reply, sizeof(reply), "{\"token\":\"%s\",\"idle_seconds\":1800}", token.data());
    return json(req, reply);
}
std::string_view bearer(httpd_req_t* req, char (&header)[48])
{
    if (httpd_req_get_hdr_value_str(req, "Authorization", header, sizeof(header)) != ESP_OK)
        return {};
    std::string_view value(header);
    return value.starts_with("Bearer ") ? value.substr(7) : std::string_view{};
}
esp_err_t control_request(httpd_req_t* req)
{
    char auth[48];
    const auto token = bearer(req, auth);
    if (req->method == HTTP_DELETE)
    {
        if (!pairing::logout(token)) return error(req, "401 Unauthorized", "pair_required");
        return json(req, "{\"ok\":true}");
    }
    if (!pairing::authorize(token)) return error(req, "401 Unauthorized", "pair_required");
    auto& runtime = *static_cast<Runtime*>(req->user_ctx);
    std::string_view path(req->uri);
    path = path.substr(0, path.find('?'));
    if (path == "/api/v1/status") return status(req, runtime);
    if (path == "/api/v1/pet/frame")
    {
        Frame frame;
        if (!runtime.copy_pet_frame(frame)) return error(req, "409 Conflict", "frame_unavailable");
        httpd_resp_set_type(req, "application/octet-stream");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        return httpd_resp_send(req, reinterpret_cast<const char*>(frame.data()), frame.size());
    }
    char input[max_body];
    if (read_body(req, input) != ESP_OK) return ESP_FAIL;
    const std::string_view body(input, req->content_len);
    UiRequest command;
    if (path == "/api/v1/wifi")
    {
        control::Station station;
        if (!control::read_station(body, station)) return error(req, "400 Bad Request", "invalid_wifi");
        const auto result = station.reconnect ? network::reconnect()
                                              : network::try_credentials(station.ssid, station.password);
        if (result == network::SubmitResult::unconfigured) return error(req, "409 Conflict", "no_saved_wifi");
        if (result == network::SubmitResult::busy) return error(req, "409 Conflict", "wifi_busy");
        if (result != network::SubmitResult::queued) return error(req, "400 Bad Request", "invalid_wifi");
        httpd_resp_set_status(req, "202 Accepted");
        return json(req, "{\"provision\":\"trying\"}");
    }
    if (path == "/api/v1/display")
    {
        command.operation = UiOperation::settings;
        if (!control::read_display(body, command.display)) return error(req, "400 Bad Request", "invalid_display");
    }
    else
    {
        control::PetChange change;
        if (!control::read_pet(body, change)) return error(req, "400 Bad Request", "invalid_pet");
        if (change.close)
            command.operation = UiOperation::close_pet;
        else if (change.style >= 0)
        {
            command.operation = UiOperation::open_pet;
            command.value = uint8_t(change.style);
            command.action = change.action;
        }
        else
        {
            command.operation = UiOperation::pet_action;
            command.value = uint8_t(change.action);
        }
    }
    const auto result = apply_ui(runtime, command);
    if (result == ApplyResult::unavailable)
        return error(req, "409 Conflict", command.operation == UiOperation::open_pet
                                           ? "style_unavailable" : "pet_inactive");
    if (result == ApplyResult::busy) return error(req, "503 Service Unavailable", "ui_busy");
    if (result == ApplyResult::timeout) return error(req, "504 Gateway Timeout", "ui_timeout");
    return status(req, runtime);
}

class LocalApi final : public AppModule
{
   public:
    explicit LocalApi(Runtime& runtime) : runtime_(runtime) {}
    std::string_view name() const override { return "local_api"; }
    void process() override
    {
        if (!network::enabled_requested() || !network::radio_running())
        {
            stop();
            return;
        }
        const auto now = esp_timer_get_time();
        if (!server_ && now >= http_retry_)
        {
            http_retry_ = now + 1000000;
            if (!start_http()) ESP_LOGW(tag, "HTTP start failed; retrying");
        }
        if (!server_) return;
        if (discovery_ < 0 && now >= udp_retry_)
        {
            udp_retry_ = now + 1000000;
            open_discovery();
        }
        if (discovery_ >= 0) poll_discovery();
    }

   private:
    Runtime& runtime_;
    httpd_handle_t server_ = nullptr;
    int discovery_ = -1;
    int64_t http_retry_ = 0, udp_retry_ = 0;
    char announcement_[80]{};
    bool on_initialize() override
    {
        const auto net = network::snapshot();
        std::snprintf(announcement_, sizeof(announcement_), "{\"v\":1,\"id\":\"%s\",\"http\":80}", net.ap_ssid);
        return true;
    }
    bool on_deinitialize() override { return stop(); }
    bool stop()
    {
        if (discovery_ >= 0) { close(discovery_); discovery_ = -1; }
        if (server_ && httpd_stop(server_) != ESP_OK) return false;
        server_ = nullptr;
        return true;
    }
    bool start_http()
    {
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.stack_size = 6656;
        config.task_priority = 3;
        // httpd emits the status line, optional headers and body in separate writes.
        // Deliver short control replies without waiting for a delayed TCP ACK.
        config.open_fn = [](httpd_handle_t, int fd) -> esp_err_t {
            const int enabled = 1;
            return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled)) == 0
                       ? ESP_OK : ESP_FAIL;
        };
        // Reserve three httpd internals, three DAP sockets, PC Monitor and discovery.
        config.max_open_sockets = std::clamp(CONFIG_LWIP_MAX_SOCKETS - 8, 1, 4);
        config.lru_purge_enable = true;
        config.recv_wait_timeout = 2;
        config.send_wait_timeout = 5;
        config.max_req_hdr_len = 2048;
        const httpd_uri_t routes[] = {
            {"/", HTTP_GET, asset, nullptr},
            {"/api/v1/info", HTTP_GET, info, nullptr},
            {"/api/v1/session", HTTP_POST, pair, nullptr},
            {"/api/v1/session", HTTP_DELETE, control_request, &runtime_},
            {"/api/v1/status", HTTP_GET, control_request, &runtime_},
            {"/api/v1/pet", HTTP_POST, control_request, &runtime_},
            {"/api/v1/pet/frame", HTTP_GET, control_request, &runtime_},
            {"/api/v1/display", HTTP_POST, control_request, &runtime_},
            {"/api/v1/wifi", HTTP_POST, control_request, &runtime_},
        };
        config.max_uri_handlers = std::size(routes);
        if (httpd_start(&server_, &config) != ESP_OK) return false;
        for (const auto& route : routes)
            if (httpd_register_uri_handler(server_, &route) != ESP_OK) { stop(); return false; }
        ESP_LOGI(tag, "HTTP listening on port 80");
        return true;
    }
    void open_discovery()
    {
        const int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (fd < 0) return;
        sockaddr_in local{};
        local.sin_family = AF_INET;
        local.sin_port = htons(discovery_port);
        local.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(fd, reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0) { close(fd); return; }
        discovery_ = fd;
    }
    void poll_discovery()
    {
        constexpr std::string_view query = "C3GB_DISCOVER/1";
        for (unsigned i = 0; i < 4; ++i)
        {
            char body[query.size() + 1];
            sockaddr_in peer{};
            socklen_t len = sizeof(peer);
            const int n = recvfrom(discovery_, body, sizeof(body), MSG_DONTWAIT,
                                   reinterpret_cast<sockaddr*>(&peer), &len);
            if (n < 0)
            {
                if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                {
                    close(discovery_);
                    discovery_ = -1;
                }
                return;
            }
            if (size_t(n) == query.size() && !std::memcmp(query.data(), body, query.size()))
                sendto(discovery_, announcement_, std::strlen(announcement_), MSG_DONTWAIT,
                       reinterpret_cast<sockaddr*>(&peer), len);
        }
    }
};
}  // namespace
std::unique_ptr<AppModule> create_local_api(Runtime& r) { return std::make_unique<LocalApi>(r); }
}  // namespace c3::device
