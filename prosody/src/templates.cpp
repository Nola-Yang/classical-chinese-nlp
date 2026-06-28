#include "pingze/templates.hpp"

#include <fstream>
#include <stdexcept>

#include "pingze/unicode.hpp"

namespace pingze {

// ---- 词谱 loading ------------------------------------------------------------

static SlotTune tune_of(char c) {
    switch (c) {
        case 'P': return SLOT_PING;
        case 'Z': return SLOT_ZE;
        default:  return SLOT_ANY;  // 'X'
    }
}
static SlotMark mark_of(char c) {
    switch (c) {
        case '*': return MARK_RHYME;
        case '/': return MARK_JU;
        case ',': return MARK_DOU;
        case '=': return MARK_DIE;
        default:  return MARK_NONE;  // '-'
    }
}

CipuTable CipuTable::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("cannot open 词谱 data: " + path);
    CipuTable t;
    std::string line;
    std::string cur;  // current 词牌 name
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line[0] == '@') {
            cur = line.substr(1);
            t.map_.try_emplace(cur);
        } else if (line[0] == '#' && !cur.empty()) {
            // #author|sketch|TOKENS  where TOKENS is pairs of (tune)(mark)
            const std::string body = line.substr(1);
            const auto p1 = body.find('|');
            const auto p2 = body.find('|', p1 == std::string::npos ? 0 : p1 + 1);
            if (p1 == std::string::npos || p2 == std::string::npos) continue;
            CiFormat fmt;
            fmt.author = body.substr(0, p1);
            fmt.sketch = body.substr(p1 + 1, p2 - p1 - 1);
            const std::string toks = body.substr(p2 + 1);
            for (size_t i = 0; i + 1 < toks.size(); i += 2) {
                fmt.slots.push_back(Slot{tune_of(toks[i]), mark_of(toks[i + 1])});
            }
            t.map_[cur].push_back(std::move(fmt));
        }
    }
    return t;
}

const std::vector<CiFormat>* CipuTable::find(const std::string& cipai) const {
    auto it = map_.find(cipai);
    return it == map_.end() ? nullptr : &it->second;
}

std::vector<std::string> CipuTable::names() const {
    std::vector<std::string> v;
    v.reserve(map_.size());
    for (auto& kv : map_) v.push_back(kv.first);
    return v;
}

size_t CipuTable::format_count() const {
    size_t n = 0;
    for (auto& kv : map_) n += kv.second.size();
    return n;
}

// ---- 近体诗 template generation ---------------------------------------------
//
// There are exactly four base line shapes per line length. We encode them with
// 中 (SLOT_ANY) where 一三(五)不论 applies, but keep a position fixed wherever
// freeing it would create 孤平 or 三平/三仄尾. Lines are then assembled by the
// 粘 (adhesion) and 对 (opposition) rules, which fully determine every line's
// shape once we know line 1's 起 (opening tone) and whether it rhymes.

namespace {

// Index a base shape by (level-opening?, level-closing?).
// 起 = tone of the 2nd character; 收 = tone of the last character.
const std::vector<SlotTune>& base_line(int yan, bool ping_qi, bool ping_shou) {
    // 五言
    static const std::vector<SlotTune> w_pq_ps =  // 平起平收  平平仄仄平 (孤平: pos1 fixed)
        {SLOT_PING, SLOT_PING, SLOT_ANY, SLOT_ZE, SLOT_PING};
    static const std::vector<SlotTune> w_pq_zs =  // 平起仄收  平平平仄仄
        {SLOT_ANY, SLOT_PING, SLOT_PING, SLOT_ZE, SLOT_ZE};
    static const std::vector<SlotTune> w_zq_ps =  // 仄起平收  仄仄仄平平 (三平尾: pos3 fixed)
        {SLOT_ANY, SLOT_ZE, SLOT_ZE, SLOT_PING, SLOT_PING};
    static const std::vector<SlotTune> w_zq_zs =  // 仄起仄收  仄仄平平仄
        {SLOT_ANY, SLOT_ZE, SLOT_ANY, SLOT_PING, SLOT_ZE};
    // 七言 (prepend an opposite-tone pair to the 五言 shape)
    static const std::vector<SlotTune> q_pq_ps =  // 平起平收  平平仄仄仄平平
        {SLOT_ANY, SLOT_PING, SLOT_ANY, SLOT_ZE, SLOT_ZE, SLOT_PING, SLOT_PING};
    static const std::vector<SlotTune> q_pq_zs =  // 平起仄收  平平仄仄平平仄
        {SLOT_ANY, SLOT_PING, SLOT_ANY, SLOT_ZE, SLOT_PING, SLOT_PING, SLOT_ZE};
    static const std::vector<SlotTune> q_zq_ps =  // 仄起平收  仄仄平平仄仄平 (孤平: pos3 fixed)
        {SLOT_ANY, SLOT_ZE, SLOT_PING, SLOT_PING, SLOT_ANY, SLOT_ZE, SLOT_PING};
    static const std::vector<SlotTune> q_zq_zs =  // 仄起仄收  仄仄平平平仄仄
        {SLOT_ANY, SLOT_ZE, SLOT_ANY, SLOT_PING, SLOT_PING, SLOT_ZE, SLOT_ZE};

    if (yan == 5) {
        if (ping_qi) return ping_shou ? w_pq_ps : w_pq_zs;
        return ping_shou ? w_zq_ps : w_zq_zs;
    }
    if (ping_qi) return ping_shou ? q_pq_ps : q_pq_zs;
    return ping_shou ? q_zq_ps : q_zq_zs;
}

}  // namespace

std::vector<Slot> make_shi_template(const ShiSpec& spec) {
    std::vector<Slot> out;
    const bool first_ping_qi = !spec.ze_qi;
    for (int i = 1; i <= spec.lines; ++i) {
        // 起 of line i: line 1 is the given 起; 对/粘 make the pattern
        // F, ¬F, ¬F, F, F, ¬F, ¬F, F, ... which is "same as line 1" iff i mod 4
        // is 0 or 1.
        const bool same_as_first = (i % 4 == 1) || (i % 4 == 0);
        const bool ping_qi = same_as_first ? first_ping_qi : !first_ping_qi;
        // 收: even lines rhyme (平收); odd lines are 仄收, except line 1 may rhyme.
        const bool rhymes = (i % 2 == 0) || (i == 1 && spec.first_rhymes);
        const bool ping_shou = rhymes;
        const std::vector<SlotTune>& shape = base_line(spec.yan, ping_qi, ping_shou);
        for (size_t k = 0; k < shape.size(); ++k) {
            Slot s;
            s.tune = shape[k];
            if (k + 1 == shape.size()) s.mark = rhymes ? MARK_RHYME : MARK_JU;
            out.push_back(s);
        }
    }
    return out;
}

std::string shi_form_name(const ShiSpec& spec) {
    std::string s = (spec.yan == 5 ? "五言" : "七言");
    s += (spec.lines == 4 ? "绝句" : "律诗");
    s += "·";
    s += (spec.ze_qi ? "仄起" : "平起");
    s += (spec.first_rhymes ? "首句入韵" : "首句不入韵");
    return s;
}

}  // namespace pingze
