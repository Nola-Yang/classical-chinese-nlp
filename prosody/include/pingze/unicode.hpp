// UTF-8 <-> Unicode codepoint helpers, plus CJK detection.
//
// Classical Chinese verse is, mercifully, one logical character per syllable,
// so once we are in codepoint space the prosody logic is index-arithmetic.
#pragma once

#include <string>
#include <vector>

namespace pingze {

// One source character together with where it came from, so diagnostics can
// point back at a line:column the way a compiler does.
struct SourceChar {
    char32_t cp;    // Unicode codepoint
    int line;       // 1-based line in the input
    int col;        // 1-based column (counted in codepoints, not bytes)
    int byte_off;   // byte offset into the original buffer
};

// Decode a UTF-8 buffer into codepoints, tracking line/column. Invalid bytes
// are passed through as U+FFFD so we never crash on dirty input.
std::vector<SourceChar> decode_utf8(const std::string& bytes);

// Encode a single codepoint back to UTF-8 (for printing one character).
std::string encode_utf8(char32_t cp);

// Is this codepoint a Han (CJK) character we should scan for tone? Covers the
// main block, Ext-A, and the compatibility ideographs; everything else
// (punctuation, latin, whitespace) is treated as a separator.
bool is_han(char32_t cp);

}  // namespace pingze
