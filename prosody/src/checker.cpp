#include "pingze/checker.hpp"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>

namespace pingze {

std::vector<SourceChar> han_only(const std::vector<SourceChar>& chars) {
    std::vector<SourceChar> out;
    for (const auto& c : chars)
        if (is_han(c.cp)) out.push_back(c);
    return out;
}

namespace {

std::string tune_word(SlotTune t) {
    switch (t) {
        case SLOT_PING: return "平 (level)";
        case SLOT_ZE:   return "仄 (oblique)";
        default:        return "中 (either)";
    }
}

// Describe what a character actually is, e.g. "仄 (入声 entering)".
std::string actual_word(const CharInfo* ci) {
    if (!ci) return "unknown";
    if (ci->ambiguous()) return "平/仄 (heteronym)";
    if (ci->can_ping()) return "平 (level)";
    if (ci->can_ru()) return "仄 (入声 entering)";
    if (ci->can_ze()) return "仄 (oblique)";
    return "unknown";
}

// Align characters against a tone template; append diagnostics, count hard
// 平仄 errors into result.prosody_violations.
void check_prosody(const Phonology& phon, const std::vector<Slot>& slots,
                   const std::vector<SourceChar>& han, CheckResult& r) {
    const size_t n = std::min(slots.size(), han.size());
    for (size_t k = 0; k < n; ++k) {
        const Slot& s = slots[k];
        const SourceChar& sc = han[k];
        const CharInfo* ci = phon.lookup(sc.cp);

        if (s.mark == MARK_DIE) {
            // 叠字 (声声慢/钗头凤: 错错错) repeats the previous char; 叠句 (如梦令:
            // 知否知否) repeats two back. Accept either.
            const bool rep = (k >= 1 && han[k].cp == han[k - 1].cp) ||
                             (k >= 2 && han[k].cp == han[k - 2].cp);
            if (!rep)
                r.diags.push_back({Diagnostic::INFO, sc.line, sc.col, sc.cp,
                                   "叠 position: expected a repeated character (叠字/叠句)"});
        }
        if (s.tune == SLOT_ANY) continue;
        if (!ci) {
            r.diags.push_back({Diagnostic::WARNING, sc.line, sc.col, sc.cp,
                               "character not in phonology table; tone not checked"});
            continue;
        }
        const bool want_ping = (s.tune == SLOT_PING);
        const bool satisfied = want_ping ? ci->can_ping() : ci->can_ze();
        if (!satisfied) {
            ++r.prosody_violations;
            r.diags.push_back({Diagnostic::ERROR, sc.line, sc.col, sc.cp,
                               "expected " + tune_word(s.tune) + ", but this character is " +
                                   actual_word(ci)});
        }
    }
}

SlotTune required_rhyme_tone(const Slot& s, const CharInfo* ci) {
    if (s.tune == SLOT_PING) return SLOT_PING;
    if (s.tune == SLOT_ZE) return SLOT_ZE;
    if (ci && ci->can_ping() && !ci->can_ze()) return SLOT_PING;
    return SLOT_ZE;
}

// 词 rhyme: rhymes split into runs whenever the required tone flips (换韵);
// within a run every rhyme word must share a 词林正韵 部.
void check_ci_rhyme(const Phonology& phon, const std::vector<Slot>& slots,
                    const std::vector<SourceChar>& han, CheckResult& r) {
    const size_t n = std::min(slots.size(), han.size());
    std::vector<std::vector<size_t>> runs;
    int prev_tone = -1;
    for (size_t k = 0; k < n; ++k) {
        if (slots[k].mark != MARK_RHYME) continue;
        const CharInfo* ci = phon.lookup(han[k].cp);
        const int tone = required_rhyme_tone(slots[k], ci);
        if (runs.empty() || tone != prev_tone) runs.push_back({});
        runs.back().push_back(k);
        prev_tone = tone;
    }

    std::vector<std::string> notes;
    for (const auto& run : runs) {
        // Intersect the 部 sets of every rhyme word in this run.
        std::set<int> common;
        bool first = true;
        for (size_t k : run) {
            const CharInfo* ci = phon.lookup(han[k].cp);
            std::set<int> bu;
            if (ci) bu.insert(ci->cilin.begin(), ci->cilin.end());
            if (first) { common = bu; first = false; }
            else {
                std::set<int> next;
                std::set_intersection(common.begin(), common.end(), bu.begin(), bu.end(),
                                      std::inserter(next, next.begin()));
                common = next;
            }
        }
        if (common.empty() && run.size() > 1) {
            // Report which rhyme words fail to share the majority 部.
            std::map<int, int> tally;
            for (size_t k : run) {
                const CharInfo* ci = phon.lookup(han[k].cp);
                if (ci) for (int b : ci->cilin) ++tally[b];
            }
            int best = -1, bestn = -1;
            for (auto& kv : tally) if (kv.second > bestn) { bestn = kv.second; best = kv.first; }
            for (size_t k : run) {
                const CharInfo* ci = phon.lookup(han[k].cp);
                const bool inb = ci && std::find(ci->cilin.begin(), ci->cilin.end(), best) != ci->cilin.end();
                if (!inb) {
                    ++r.rhyme_problems;
                    r.diags.push_back({Diagnostic::ERROR, han[k].line, han[k].col, han[k].cp,
                                       "off-rhyme (出韵): does not share a 词林正韵 部 with the other rhyme words"});
                }
            }
            if (best >= 0) notes.push_back(phon.group_name(best) + "(+off-rhyme)");
        } else if (!common.empty()) {
            const int b = *common.begin();
            std::string name = phon.group_name(b);
            // Mark 入声 部 (15–19) which 满江红 etc. conventionally use.
            bool all_ru = true;
            for (size_t k : run) {
                const CharInfo* ci = phon.lookup(han[k].cp);
                if (!ci || !ci->can_ru()) { all_ru = false; break; }
            }
            if (all_ru) name += "(入声)";
            notes.push_back(name);
        }
    }
    if (!notes.empty()) {
        r.rhyme_note = "词林正韵 ";
        for (size_t i = 0; i < notes.size(); ++i) {
            if (i) r.rhyme_note += " → ";
            r.rhyme_note += notes[i];
        }
    }
}

// 诗 rhyme: all 韵脚 are level-tone and must share one 平水韵 group.
void check_shi_rhyme(const Phonology& phon, const std::vector<Slot>& slots,
                     const std::vector<SourceChar>& han, CheckResult& r) {
    std::vector<size_t> feet;
    const size_t n = std::min(slots.size(), han.size());
    for (size_t k = 0; k < n; ++k)
        if (slots[k].mark == MARK_RHYME) feet.push_back(k);
    if (feet.empty()) return;

    std::set<int> common;
    bool first = true;
    for (size_t k : feet) {
        const CharInfo* ci = phon.lookup(han[k].cp);
        if (ci && !ci->can_ping()) {
            ++r.rhyme_problems;
            r.diags.push_back({Diagnostic::ERROR, han[k].line, han[k].col, han[k].cp,
                               "rhyme word must be 平声 (level), but this character is " + actual_word(ci)});
        }
        std::set<int> g;
        if (ci) g.insert(ci->ping_groups.begin(), ci->ping_groups.end());
        if (first) { common = g; first = false; }
        else {
            std::set<int> next;
            std::set_intersection(common.begin(), common.end(), g.begin(), g.end(),
                                  std::inserter(next, next.begin()));
            common = next;
        }
    }
    if (common.empty()) {
        // Flag every foot not in the majority 平水韵 group.
        std::map<int, int> tally;
        for (size_t k : feet) {
            const CharInfo* ci = phon.lookup(han[k].cp);
            if (ci) for (int g : ci->ping_groups) ++tally[g];
        }
        int best = -1, bestn = -1;
        for (auto& kv : tally) if (kv.second > bestn) { bestn = kv.second; best = kv.first; }
        for (size_t k : feet) {
            const CharInfo* ci = phon.lookup(han[k].cp);
            const bool inb = ci && std::find(ci->ping_groups.begin(), ci->ping_groups.end(), best) != ci->ping_groups.end();
            if (!inb) {
                ++r.rhyme_problems;
                r.diags.push_back({Diagnostic::ERROR, han[k].line, han[k].col, han[k].cp,
                                   "off-rhyme (出韵): not in the same 平水韵 rhyme group as the other rhyme words"});
            }
        }
        if (best >= 0) r.rhyme_note = "平水韵 " + phon.group_name(best) + "(+off-rhyme)";
    } else {
        r.rhyme_note = "平水韵 " + phon.group_name(*common.begin());
    }
}

// Count hard 平仄 errors for a candidate template (for picking the best 体/variant).
int count_violations(const Phonology& phon, const std::vector<Slot>& slots,
                     const std::vector<SourceChar>& han) {
    CheckResult tmp;
    check_prosody(phon, slots, han, tmp);
    return tmp.prosody_violations;
}

}  // namespace

CheckResult check_ci(const Phonology& phon, const CipuTable& cipu,
                     const std::string& cipai, const std::string& text,
                     std::optional<int> format_index) {
    CheckResult r;
    const auto chars = decode_utf8(text);
    const auto han = han_only(chars);
    r.han_count = static_cast<int>(han.size());

    const std::vector<CiFormat>* formats = cipu.find(cipai);
    if (!formats || formats->empty()) {
        r.diags.push_back({Diagnostic::ERROR, 0, 0, 0, "unknown 词牌: " + cipai});
        return r;
    }

    const CiFormat* chosen = nullptr;
    if (format_index) {
        if (*format_index < 0 || *format_index >= static_cast<int>(formats->size())) {
            r.diags.push_back({Diagnostic::ERROR, 0, 0, 0, "format index out of range"});
            return r;
        }
        chosen = &(*formats)[*format_index];
    } else {
        // Prefer an exact length match with the fewest violations; otherwise the
        // 体 whose length is closest.
        int best_score = -1;
        const CiFormat* best_len = nullptr;
        int best_len_diff = 1 << 30;
        for (const auto& f : *formats) {
            const int diff = std::abs(f.length() - r.han_count);
            if (diff < best_len_diff) { best_len_diff = diff; best_len = &f; }
            if (f.length() == r.han_count) {
                const int v = count_violations(phon, f.slots, han);
                const int score = -v;  // fewer violations = higher score
                if (score > best_score) { best_score = score; chosen = &f; }
            }
        }
        if (!chosen) { chosen = best_len; r.length_matched = false; }
    }

    r.form_label = cipai + "·" + chosen->author + "  「" + chosen->sketch + "」";
    r.template_len = chosen->length();
    if (chosen->length() != r.han_count) {
        r.length_matched = false;
        r.diags.push_back({Diagnostic::WARNING, 0, 0, 0,
                           "length mismatch: poem has " + std::to_string(r.han_count) +
                               " characters, this 体 expects " + std::to_string(chosen->length())});
    }
    check_prosody(phon, chosen->slots, han, r);
    check_ci_rhyme(phon, chosen->slots, han, r);
    return r;
}

CheckResult check_shi(const Phonology& phon, const std::string& text,
                      std::optional<ShiSpec> spec_in) {
    CheckResult r;
    const auto chars = decode_utf8(text);
    const auto han = han_only(chars);
    r.han_count = static_cast<int>(han.size());

    ShiSpec spec;
    if (spec_in) {
        spec = *spec_in;
    } else {
        // Auto-detect line length and count by splitting on punctuation/newline.
        std::vector<int> line_lens;
        int cur = 0;
        for (const auto& c : chars) {
            if (is_han(c.cp)) { ++cur; }
            else if (c.cp == U'，' || c.cp == U'。' || c.cp == U'！' || c.cp == U'？' ||
                     c.cp == U'；' || c.cp == U'、' || c.cp == U'\n' || c.cp == U',' ||
                     c.cp == U'.' || c.cp == U'!' || c.cp == U'?' || c.cp == U';') {
                if (cur > 0) { line_lens.push_back(cur); cur = 0; }
            }
        }
        if (cur > 0) line_lens.push_back(cur);
        // Modal line length -> 言; number of lines -> 句数.
        std::map<int, int> freq;
        for (int l : line_lens) ++freq[l];
        int yan = 7, bestn = -1;
        for (auto& kv : freq) if (kv.second > bestn) { bestn = kv.second; yan = kv.first; }
        spec.yan = (yan == 5 ? 5 : 7);
        spec.lines = (static_cast<int>(line_lens.size()) <= 4) ? 4 : 8;
    }

    // Try the 起式 × 入韵 combinations and keep the best-scoring one.
    ShiSpec best = spec;
    int best_v = 1 << 30;
    if (!spec_in.has_value()) {
        for (bool ze_qi : {false, true})
            for (bool fr : {false, true}) {
                ShiSpec s = spec;
                s.ze_qi = ze_qi;
                s.first_rhymes = fr;
                const auto slots = make_shi_template(s);
                const int v = count_violations(phon, slots, han);
                if (v < best_v) { best_v = v; best = s; }
            }
    }

    const auto slots = make_shi_template(best);
    r.form_label = shi_form_name(best);
    r.template_len = static_cast<int>(slots.size());
    if (r.template_len != r.han_count) {
        r.length_matched = false;
        r.diags.push_back({Diagnostic::WARNING, 0, 0, 0,
                           "length mismatch: poem has " + std::to_string(r.han_count) +
                               " characters, this form expects " + std::to_string(r.template_len) +
                               " — auto-detection may be off; try --shi"});
    }
    check_prosody(phon, slots, han, r);
    check_shi_rhyme(phon, slots, han, r);
    return r;
}

}  // namespace pingze
