#include "c3/pairing.hpp"

#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

namespace c3::pairing
{
namespace
{
struct Session { Token token{}; int64_t expires = 0; };
std::array<Session, 4> sessions;
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
uint32_t code = 0;
int64_t code_expires = 0;
unsigned attempts = 0;
constexpr int64_t session_us = 30LL * 60 * 1000000;

Session* find(std::string_view token, int64_t now)
{
    if (token.size() != Token{}.size() - 1) return nullptr;
    for (auto& session : sessions)
        if (session.expires > now && token == session.token.data()) return &session;
    return nullptr;
}
}  // namespace
uint32_t open()
{
    const auto fresh = 100000 + esp_random() % 900000;
    portENTER_CRITICAL(&lock);
    code = fresh;
    code_expires = esp_timer_get_time() + 120000000;
    attempts = 5;
    portEXIT_CRITICAL(&lock);
    return fresh;
}
uint32_t visible_code()
{
    portENTER_CRITICAL(&lock);
    const auto visible = esp_timer_get_time() < code_expires ? code : 0;
    portEXIT_CRITICAL(&lock);
    return visible;
}
Exchange exchange(uint32_t supplied, Token& out)
{
    uint8_t bytes[16];
    esp_fill_random(bytes, sizeof(bytes));
    constexpr char hex[] = "0123456789abcdef";
    Token fresh{};
    for (unsigned i = 0; i < sizeof(bytes); ++i)
    {
        fresh[2 * i] = hex[bytes[i] >> 4];
        fresh[2 * i + 1] = hex[bytes[i] & 15];
    }
    Exchange result = Exchange::invalid;
    portENTER_CRITICAL(&lock);
    const auto now = esp_timer_get_time();
    if (code && now < code_expires)
    {
        if (code == supplied)
        {
            result = Exchange::full;
            for (auto& session : sessions)
                if (session.expires <= now)
                {
                    session = {fresh, now + session_us};
                    out = fresh;
                    code = 0;
                    result = Exchange::ok;
                    break;
                }
        }
        else if (!--attempts) code = 0;
    }
    portEXIT_CRITICAL(&lock);
    return result;
}
bool authorize(std::string_view token)
{
    portENTER_CRITICAL(&lock);
    const auto now = esp_timer_get_time();
    auto* session = find(token, now);
    if (session) session->expires = now + session_us;
    portEXIT_CRITICAL(&lock);
    return session != nullptr;
}
bool logout(std::string_view token)
{
    portENTER_CRITICAL(&lock);
    auto* session = find(token, esp_timer_get_time());
    if (session) *session = {};
    portEXIT_CRITICAL(&lock);
    return session != nullptr;
}
void revoke_all()
{
    portENTER_CRITICAL(&lock);
    code = 0;
    sessions = {};
    portEXIT_CRITICAL(&lock);
}
}  // namespace c3::pairing
