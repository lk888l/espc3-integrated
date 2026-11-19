#pragma once
#include <charconv>
#include <cstdint>
#include <string_view>

namespace c3
{
class Json
{
   public:
    explicit Json(std::string_view input) : input_(input) {}
    void space()
    {
        while (at_ < input_.size() && (input_[at_] == ' ' || input_[at_] == '\r' ||
               input_[at_] == '\n' || input_[at_] == '\t')) ++at_;
    }
    bool take(char expected)
    {
        space();
        if (at_ == input_.size() || input_[at_] != expected) return false;
        ++at_;
        return true;
    }
    bool done() { space(); return at_ == input_.size(); }
    bool literal(std::string_view value)
    {
        space();
        if (input_.substr(at_, value.size()) != value) return false;
        at_ += value.size();
        return true;
    }
    bool null()
    {
        return literal("null");
    }
    bool number(std::string_view& token)
    {
        space();
        const auto begin = at_;
        if (at_ < input_.size() && input_[at_] == '-') ++at_;
        if (at_ == input_.size()) return false;
        if (input_[at_] == '0') ++at_;
        else
        {
            if (input_[at_] < '1' || input_[at_] > '9') return false;
            while (digit()) ++at_;
        }
        if (at_ < input_.size() && input_[at_] == '.')
        {
            ++at_;
            if (!digit()) return false;
            while (digit()) ++at_;
        }
        if (at_ < input_.size() && (input_[at_] == 'e' || input_[at_] == 'E'))
        {
            ++at_;
            if (at_ < input_.size() && (input_[at_] == '-' || input_[at_] == '+')) ++at_;
            if (!digit()) return false;
            while (digit()) ++at_;
        }
        token = input_.substr(begin, at_ - begin);
        return true;
    }
    bool string(char* output, std::size_t capacity)
    {
        if (!output || !capacity || !take('"')) return false;
        std::size_t used = 0;
        const auto append = [&](uint8_t byte)
        {
            if (used + 1 >= capacity || byte < 0x20 || byte == 0x7f) return false;
            output[used++] = char(byte);
            return true;
        };
        while (at_ < input_.size())
        {
            uint32_t byte = uint8_t(input_[at_++]);
            if (byte == '"') { output[used] = 0; return true; }
            if (byte == '\\')
            {
                if (at_ == input_.size()) return false;
                byte = uint8_t(input_[at_++]);
                if (byte == '"' || byte == '\\' || byte == '/')
                {
                    if (!append(uint8_t(byte))) return false;
                    continue;
                }
                if (byte != 'u' || !hex4(byte)) return false;
                if (byte >= 0xd800 && byte <= 0xdbff)
                {
                    uint32_t low;
                    if (at_ + 2 > input_.size() || input_[at_++] != '\\' ||
                        input_[at_++] != 'u' || !hex4(low) || low < 0xdc00 || low > 0xdfff)
                        return false;
                    byte = 0x10000 + ((byte - 0xd800) << 10) + low - 0xdc00;
                }
                else if (byte >= 0xdc00 && byte <= 0xdfff) return false;
                if (byte < 0x80) { if (!append(uint8_t(byte))) return false; }
                else if (byte < 0x800)
                {
                    if (!append(uint8_t(0xc0 | (byte >> 6))) ||
                        !append(uint8_t(0x80 | (byte & 63)))) return false;
                }
                else if (byte < 0x10000)
                {
                    if (!append(uint8_t(0xe0 | (byte >> 12))) ||
                        !append(uint8_t(0x80 | ((byte >> 6) & 63))) ||
                        !append(uint8_t(0x80 | (byte & 63)))) return false;
                }
                else
                {
                    if (!append(uint8_t(0xf0 | (byte >> 18))) ||
                        !append(uint8_t(0x80 | ((byte >> 12) & 63))) ||
                        !append(uint8_t(0x80 | ((byte >> 6) & 63))) ||
                        !append(uint8_t(0x80 | (byte & 63)))) return false;
                }
                continue;
            }
            if (byte < 0x80) { if (!append(uint8_t(byte))) return false; continue; }
            // Validate raw UTF-8 as well as escaped Unicode.
            unsigned extra;
            uint32_t codepoint;
            if (byte >= 0xc2 && byte <= 0xdf) { extra = 1; codepoint = byte & 31; }
            else if (byte >= 0xe0 && byte <= 0xef) { extra = 2; codepoint = byte & 15; }
            else if (byte >= 0xf0 && byte <= 0xf4) { extra = 3; codepoint = byte & 7; }
            else return false;
            if (!append(uint8_t(byte))) return false;
            for (unsigned i = 0; i < extra; ++i)
            {
                if (at_ == input_.size()) return false;
                const auto continuation = uint8_t(input_[at_++]);
                if ((continuation & 0xc0) != 0x80 || !append(continuation)) return false;
                codepoint = (codepoint << 6) | (continuation & 63);
            }
            if ((extra == 1 && codepoint < 0x80) || (extra == 2 && codepoint < 0x800) ||
                (extra == 3 && codepoint < 0x10000) || codepoint > 0x10ffff ||
                (codepoint >= 0xd800 && codepoint <= 0xdfff)) return false;
        }
        return false;
    }

   private:
    std::string_view input_;
    std::size_t at_ = 0;
    bool digit() const { return at_ < input_.size() && input_[at_] >= '0' && input_[at_] <= '9'; }
    bool hex4(uint32_t& result)
    {
        result = 0;
        for (unsigned i = 0; i < 4; ++i)
        {
            if (at_ == input_.size()) return false;
            const char c = input_[at_++];
            const int value = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
                              : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (value < 0) return false;
            result = (result << 4) | unsigned(value);
        }
        return true;
    }
};

inline bool integer(Json& json, uint64_t& output)
{
    std::string_view token;
    if (!json.number(token) || token.empty()) return false;
    for (const auto c : token) if (c < '0' || c > '9') return false;
    const auto parsed = std::from_chars(token.data(), token.data() + token.size(), output);
    return parsed.ec == std::errc{} && parsed.ptr == token.data() + token.size();
}
}  // namespace c3
