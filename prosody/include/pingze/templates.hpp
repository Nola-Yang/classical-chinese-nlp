// Metrical templates: 词谱 loaded from data, 诗律 generated from the rules.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace pingze {

// What a single position in a template demands.
enum SlotTune : uint8_t { SLOT_PING, SLOT_ZE, SLOT_ANY };  // 平 / 仄 / 中
enum SlotMark : uint8_t {
    MARK_NONE,   // mid-line
    MARK_RHYME,  // 韵: a rhyme falls here
    MARK_JU,     // 句: end of a (non-rhyming) line
    MARK_DOU,    // 读: a minor pause (comma) inside a line
    MARK_DIE,    // 叠: this position repeats the previous character
};

struct Slot {
    SlotTune tune = SLOT_ANY;
    SlotMark mark = MARK_NONE;
};

// One realised pattern of a 词牌 (a 词牌 may have several 体/variants).
struct CiFormat {
    std::string author;  // the exemplar author 钦定词谱 keys the pattern to
    std::string sketch;  // e.g. 双调九十三字，前段八句四仄韵...
    std::vector<Slot> slots;
    int length() const { return static_cast<int>(slots.size()); }
};

// All 词牌 patterns, parsed from data/cipu.dat (compact 钦定词谱 + 龙榆生).
class CipuTable {
public:
    static CipuTable load(const std::string& path);
    const std::vector<CiFormat>* find(const std::string& cipai) const;
    std::vector<std::string> names() const;
    size_t cipai_count() const { return map_.size(); }
    size_t format_count() const;

private:
    std::unordered_map<std::string, std::vector<CiFormat>> map_;
};

// ---- 近体诗 (regulated verse) -------------------------------------------------

struct ShiSpec {
    int yan = 7;            // 5 or 7 characters per line
    int lines = 8;          // 4 (绝句) or 8 (律诗)
    bool ze_qi = false;     // does line 1 open oblique (仄起) or level (平起)?
    bool first_rhymes = true;  // 首句入韵?
};

// Build the slot template for a regulated-verse form via 粘 / 对 rules.
std::vector<Slot> make_shi_template(const ShiSpec& spec);

// "七言律诗·仄起首句入韵" style description.
std::string shi_form_name(const ShiSpec& spec);

}  // namespace pingze
