#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace c3::pairing
{
using Token = std::array<char, 33>;  // 128 random bits encoded as hexadecimal
enum class Exchange { ok, invalid, full };
uint32_t open();  // physical UI / USB only
uint32_t visible_code();
Exchange exchange(uint32_t code, Token& out);
bool authorize(std::string_view token);
bool logout(std::string_view token);
void revoke_all();
}  // namespace c3::pairing
