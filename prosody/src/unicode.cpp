#include "pingze/unicode.hpp"

namespace pingze {

std::vector<SourceChar> decode_utf8(const std::string& bytes) {
    std::vector<SourceChar> out;
    int line = 1, col = 1;
    const size_t n = bytes.size();
    size_t i = 0;
    while (i < n) {
        const unsigned char b0 = static_cast<unsigned char>(bytes[i]);
        char32_t cp = 0;
        size_t len = 1;
        if (b0 < 0x80) {
            cp = b0;
            len = 1;
        } else if ((b0 & 0xE0) == 0xC0) {
            cp = b0 & 0x1F;
            len = 2;
        } else if ((b0 & 0xF0) == 0xE0) {
            cp = b0 & 0x0F;
            len = 3;
        } else if ((b0 & 0xF8) == 0xF0) {
            cp = b0 & 0x07;
            len = 4;
        } else {
            cp = 0xFFFD;  // stray continuation / invalid lead byte
            len = 1;
        }
        // Pull in continuation bytes; bail to U+FFFD if the sequence is short.
        bool ok = true;
        if (i + len > n) {
            ok = false;
        } else {
            for (size_t k = 1; k < len; ++k) {
                const unsigned char bk = static_cast<unsigned char>(bytes[i + k]);
                if ((bk & 0xC0) != 0x80) { ok = false; break; }
                cp = (cp << 6) | (bk & 0x3F);
            }
        }
        if (!ok) { cp = 0xFFFD; len = 1; }

        const int byte_off = static_cast<int>(i);
        out.push_back(SourceChar{cp, line, col, byte_off});
        if (cp == U'\n') {
            ++line;
            col = 1;
        } else {
            ++col;
        }
        i += len;
    }
    return out;
}

std::string encode_utf8(char32_t cp) {
    std::string s;
    if (cp < 0x80) {
        s.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        s.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        s.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        s.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return s;
}

bool is_han(char32_t cp) {
    return (cp >= 0x4E00 && cp <= 0x9FFF)    // CJK Unified Ideographs
        || (cp >= 0x3400 && cp <= 0x4DBF)    // Extension A
        || (cp >= 0xF900 && cp <= 0xFAFF)    // CJK Compatibility Ideographs
        || (cp >= 0x20000 && cp <= 0x2A6DF); // Extension B
}

}  // namespace pingze
