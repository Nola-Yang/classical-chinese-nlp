#include "pingze/phonology.hpp"

#include <fstream>
#include <stdexcept>

#include "pingze/unicode.hpp"

namespace pingze {

namespace {
std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) { out.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    out.push_back(cur);
    return out;
}
}  // namespace

int Phonology::intern(const std::string& name) {
    auto it = pool_index_.find(name);
    if (it != pool_index_.end()) return it->second;
    const int id = static_cast<int>(pool_.size());
    pool_.push_back(name);
    pool_index_.emplace(name, id);
    return id;
}

static void add_unique(std::vector<int>& v, int id) {
    for (int x : v) if (x == id) return;
    v.push_back(id);
}

void Phonology::merge_row(const std::string& line) {
    if (line.empty() || line[0] == '#') return;
    auto cols = split(line, '\t');
    if (cols.size() < 2 || cols[0].empty()) return;

    auto chars = decode_utf8(cols[0]);
    if (chars.empty()) return;
    const char32_t cp = chars[0].cp;
    CharInfo& info = map_[cp];

    // Column 1: tones (a run of 平/上/去/入).
    for (auto& sc : decode_utf8(cols[1])) {
        switch (sc.cp) {
            case U'平': info.tones |= TONE_PING;  break;
            case U'上': info.tones |= TONE_SHANG; break;
            case U'去': info.tones |= TONE_QU;    break;
            case U'入': info.tones |= TONE_RU;    break;
            default: break;
        }
    }
    // Column 2: 平水韵 groups as TAG:NAME, comma separated.
    if (cols.size() > 2) {
        for (auto& entry : split(cols[2], ',')) {
            if (entry.empty()) continue;
            add_unique(info.display_groups, intern(entry));
            const auto colon = entry.find(':');
            if (colon != std::string::npos) {
                const std::string tag = entry.substr(0, colon);
                const std::string name = entry.substr(colon + 1);
                if (tag == "P") add_unique(info.ping_groups, intern(name));
            }
        }
    }
    // Column 3: 词林正韵 部, comma separated.
    if (cols.size() > 3) {
        for (auto& bu : split(cols[3], ',')) {
            if (!bu.empty()) add_unique(info.cilin, intern(bu));
        }
    }
}

Phonology Phonology::load(const std::string& phonology_tsv,
                          const std::string& corrections_tsv) {
    Phonology p;
    std::ifstream f(phonology_tsv);
    if (!f) throw std::runtime_error("cannot open phonology table: " + phonology_tsv);
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        p.merge_row(line);
    }
    if (!corrections_tsv.empty()) {
        std::ifstream g(corrections_tsv);
        while (std::getline(g, line)) {  // silently optional
            if (!line.empty() && line.back() == '\r') line.pop_back();
            p.merge_row(line);
        }
    }
    return p;
}

const CharInfo* Phonology::lookup(char32_t cp) const {
    auto it = map_.find(cp);
    return it == map_.end() ? nullptr : &it->second;
}

std::string Phonology::tone_label(char32_t cp) const {
    const CharInfo* ci = lookup(cp);
    if (!ci) return "?";
    if (ci->ambiguous()) return "平/仄";
    if (ci->can_ping()) return "平";
    if (ci->can_ze()) return "仄";
    return "?";
}

}  // namespace pingze
