// Historical phonology lookup: codepoint -> tone class + rhyme groups.
//
// The whole point of this layer is that classical 平仄 is NOT modern Mandarin
// tone. The 入声 (entering tone) collapsed into the other tones in Mandarin, so
// characters like 一/白/月/竹/雪/发 read level (平) today but are oblique (仄) in
// verse. You cannot recover that from pinyin; you need a 平水韵 table, which is
// what we load here.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace pingze {

// 上 / 去 / 入 are all oblique (仄); only 平 is level.
enum Tone : uint8_t {
    TONE_PING  = 1,  // 平 level
    TONE_SHANG = 2,  // 上 rising
    TONE_QU    = 4,  // 去 departing
    TONE_RU    = 8,  // 入 entering
};

// Everything known about one character. A 多音字 (heteronym) can carry several
// tones at once, e.g. 看 is both 平 and 去; such a character satisfies either a
// 平 or a 仄 slot.
struct CharInfo {
    uint8_t tones = 0;                // bitmask of Tone
    std::vector<int> ping_groups;     // 平水韵 level-tone rhyme groups (诗 rhyme)
    std::vector<int> cilin;           // 词林正韵 部 (词 rhyme)
    std::vector<int> display_groups;  // all 平水韵 groups, tone-tagged, for -e

    bool can_ping() const { return tones & TONE_PING; }
    bool can_ze()   const { return tones & (TONE_SHANG | TONE_QU | TONE_RU); }
    bool can_ru()   const { return tones & TONE_RU; }
    bool ambiguous() const { return can_ping() && can_ze(); }
};

class Phonology {
public:
    // Load the upstream table and merge an optional corrections overlay. Throws
    // std::runtime_error if the main table cannot be read.
    static Phonology load(const std::string& phonology_tsv,
                          const std::string& corrections_tsv = "");

    const CharInfo* lookup(char32_t cp) const;
    bool known(char32_t cp) const { return lookup(cp) != nullptr; }

    // "平" / "仄" / "平/仄" / "?" — a compact human label for one character.
    std::string tone_label(char32_t cp) const;

    const std::string& group_name(int id) const { return pool_[id]; }
    size_t size() const { return map_.size(); }

private:
    int intern(const std::string& name);
    void merge_row(const std::string& line);

    std::unordered_map<char32_t, CharInfo> map_;
    std::vector<std::string> pool_;
    std::unordered_map<std::string, int> pool_index_;
};

}  // namespace pingze
