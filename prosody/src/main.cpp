// pingze — a prosody linter for classical Chinese verse.
//
// Reads a poem, looks up every character's historical tone (平/仄, including the
// 入声 that modern Mandarin lost), and checks it against the metrical template of
// a 词牌 or a 近体诗 form, printing compiler-style diagnostics.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "pingze/checker.hpp"
#include "pingze/phonology.hpp"
#include "pingze/templates.hpp"
#include "pingze/unicode.hpp"

namespace fs = std::filesystem;
using namespace pingze;

namespace {

struct Options {
    std::string input;            // file path, or "-" for stdin
    std::string data_dir;         // override
    std::optional<std::string> ci;       // 词牌 name
    bool shi = false;             // check as 近体诗
    std::optional<ShiSpec> shi_spec;     // manual 诗 form
    std::optional<int> format_index;
    bool json = false;
    bool explain = false;
    bool color = true;
    std::string list_ci_prefix;
    bool do_list_ci = false;
    std::string ci_formats;       // list 体 of this 词牌
    bool do_ci_formats = false;
};

[[noreturn]] void usage(int code) {
    std::cout <<
R"(pingze — a prosody linter for classical Chinese verse (诗 / 词)

USAGE:
  pingze --ci <词牌> [options] <file|->
  pingze --shi [--yan N --lines N --ze-qi --first-rhyme] [options] <file|->
  pingze --list-ci [prefix]
  pingze --ci-formats <词牌>

CHECK OPTIONS:
  --ci <词牌>        check the poem as a 词 against this 词牌 (e.g. 满江红)
  --format <n>      force the n-th 体 (variant) of the 词牌 (default: best fit)
  --shi             check the poem as a 近体诗 (auto-detect 5/7-言, 绝/律)
  --yan <5|7>       (with --shi) characters per line
  --lines <4|8>     (with --shi) number of lines
  --ze-qi           (with --shi) line 1 opens oblique (仄起); default 平起
  --first-rhyme     (with --shi) line 1 rhymes (首句入韵)

OUTPUT:
  -e, --explain     print every character with its tone and rhyme group
  --json            machine-readable output
  --no-color        disable ANSI colors
  --data <dir>      directory holding phonology.tsv / cipu.dat / corrections.tsv

INFO:
  --list-ci [pfx]   list known 词牌 (optionally filtered by prefix)
  --ci-formats <c>  list the 体 (variants) of one 词牌
  -h, --help        this help

Lines beginning with '#' and separator lines (=====) in the input are ignored,
so the tool reads the corpus format used in the companion projects directly.
)";
    std::exit(code);
}

std::string slurp(const std::string& path) {
    if (path == "-") {
        std::ostringstream ss;
        ss << std::cin.rdbuf();
        return ss.str();
    }
    std::ifstream f(path, std::ios::binary);
    if (!f) { std::cerr << "pingze: cannot open " << path << "\n"; std::exit(2); }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Drop comment (#...) and separator (=====, ~~~~~) lines so corpus files work.
std::string strip_meta(const std::string& raw) {
    std::istringstream in(raw);
    std::string line, out;
    while (std::getline(in, line)) {
        std::string t = line;
        size_t a = t.find_first_not_of(" \t\r");
        if (a == std::string::npos) { out += "\n"; continue; }
        if (t[a] == '#') continue;
        bool sep = true;
        for (size_t i = a; i < t.size(); ++i)
            if (t[i] != '=' && t[i] != '~' && t[i] != '-' && t[i] != '\r') { sep = false; break; }
        if (sep) continue;
        out += line;
        out += "\n";
    }
    return out;
}

fs::path exe_dir(const char* argv0) {
    std::error_code ec;
    fs::path p = fs::weakly_canonical(fs::path(argv0), ec);
    if (ec || p.empty()) p = fs::path(argv0);
    return p.parent_path();
}

std::string find_data_dir(const Options& opt, const char* argv0) {
    std::vector<fs::path> cands;
    if (!opt.data_dir.empty()) cands.emplace_back(opt.data_dir);
    if (const char* e = std::getenv("PINGZE_DATA")) cands.emplace_back(e);
    const fs::path ed = exe_dir(argv0);
    cands.push_back(ed / ".." / "share" / "pingze");
    cands.push_back(ed / ".." / "data");
    cands.push_back(ed / "data");
    cands.push_back("data");
    cands.push_back("../data");
    cands.push_back("../../data");
#ifdef PINGZE_SOURCE_DATA_DIR
    cands.emplace_back(PINGZE_SOURCE_DATA_DIR);  // in-tree dev fallback
#endif
    for (auto& c : cands) {
        std::error_code ec;
        if (fs::exists(c / "phonology.tsv", ec)) return c.string();
    }
    std::cerr << "pingze: could not locate data/phonology.tsv; pass --data <dir>\n";
    std::exit(2);
}

const char* C_RESET = "\033[0m";
const char* C_RED = "\033[31m";
const char* C_YEL = "\033[33m";
const char* C_GRN = "\033[32m";
const char* C_DIM = "\033[2m";
const char* C_BOLD = "\033[1m";
void no_colors() { C_RESET = C_RED = C_YEL = C_GRN = C_DIM = C_BOLD = ""; }

void print_explain(const Phonology& phon, const std::string& text) {
    for (auto& sc : han_only(decode_utf8(text))) {
        const CharInfo* ci = phon.lookup(sc.cp);
        std::cout << encode_utf8(sc.cp) << "  " << phon.tone_label(sc.cp);
        if (ci) {
            std::cout << "  平水韵[";
            for (size_t i = 0; i < ci->display_groups.size(); ++i)
                std::cout << (i ? "," : "") << phon.group_name(ci->display_groups[i]);
            std::cout << "]  词林正韵[";
            for (size_t i = 0; i < ci->cilin.size(); ++i)
                std::cout << (i ? "," : "") << phon.group_name(ci->cilin[i]);
            std::cout << "]";
        }
        std::cout << "\n";
    }
}

std::string json_escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            default: o += c;
        }
    }
    return o;
}

int report(const CheckResult& r, const std::string& name, const Options& opt) {
    if (opt.json) {
        std::cout << "{\n  \"form\": \"" << json_escape(r.form_label) << "\",\n";
        std::cout << "  \"characters\": " << r.han_count << ",\n";
        std::cout << "  \"template_length\": " << r.template_len << ",\n";
        std::cout << "  \"prosody_violations\": " << r.prosody_violations << ",\n";
        std::cout << "  \"rhyme_problems\": " << r.rhyme_problems << ",\n";
        std::cout << "  \"rhyme\": \"" << json_escape(r.rhyme_note) << "\",\n";
        std::cout << "  \"conforms\": " << (r.clean() ? "true" : "false") << ",\n";
        std::cout << "  \"diagnostics\": [\n";
        for (size_t i = 0; i < r.diags.size(); ++i) {
            const auto& d = r.diags[i];
            const char* lvl = d.level == Diagnostic::ERROR ? "error"
                            : d.level == Diagnostic::WARNING ? "warning" : "info";
            std::cout << "    {\"level\":\"" << lvl << "\",\"line\":" << d.line
                      << ",\"col\":" << d.col << ",\"char\":\""
                      << (d.cp ? json_escape(encode_utf8(d.cp)) : "") << "\",\"message\":\""
                      << json_escape(d.message) << "\"}" << (i + 1 < r.diags.size() ? "," : "") << "\n";
        }
        std::cout << "  ]\n}\n";
        return r.clean() ? 0 : 1;
    }

    std::cout << C_BOLD << "form:  " << C_RESET << r.form_label << "\n";
    std::cout << C_DIM << "chars: " << r.han_count << " / template " << r.template_len << C_RESET << "\n";
    for (const auto& d : r.diags) {
        const char* col = d.level == Diagnostic::ERROR ? C_RED
                        : d.level == Diagnostic::WARNING ? C_YEL : C_DIM;
        const char* lvl = d.level == Diagnostic::ERROR ? "error"
                        : d.level == Diagnostic::WARNING ? "warning" : "note";
        std::cout << name << ":";
        if (d.line) std::cout << d.line << ":" << d.col << ":";
        std::cout << " " << col << lvl << C_RESET << ": ";
        if (d.cp) std::cout << "'" << encode_utf8(d.cp) << "' ";
        std::cout << d.message << "\n";
    }
    if (!r.rhyme_note.empty())
        std::cout << C_DIM << "rhyme: " << C_RESET << r.rhyme_note << "\n";
    if (r.clean())
        std::cout << C_GRN << "✓ conforms" << C_RESET << " — 0 prosody violations, 0 rhyme problems\n";
    else
        std::cout << C_RED << "✗ " << r.prosody_violations << " prosody violation(s), "
                  << r.rhyme_problems << " rhyme problem(s)" << C_RESET << "\n";
    return r.clean() ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    ShiSpec manual;
    bool manual_shi = false;
    std::vector<std::string> pos;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto need = [&](const char* what) -> std::string {
            if (i + 1 >= argc) { std::cerr << "pingze: " << what << " needs an argument\n"; std::exit(2); }
            return argv[++i];
        };
        if (a == "-h" || a == "--help") usage(0);
        else if (a == "--ci") opt.ci = need("--ci");
        else if (a == "--shi") opt.shi = true;
        else if (a == "--format") opt.format_index = std::stoi(need("--format"));
        else if (a == "--yan") { manual.yan = std::stoi(need("--yan")); manual_shi = true; }
        else if (a == "--lines") { manual.lines = std::stoi(need("--lines")); manual_shi = true; }
        else if (a == "--ze-qi") { manual.ze_qi = true; manual_shi = true; }
        else if (a == "--first-rhyme") { manual.first_rhymes = true; manual_shi = true; }
        else if (a == "-e" || a == "--explain") opt.explain = true;
        else if (a == "--json") opt.json = true;
        else if (a == "--no-color") opt.color = false;
        else if (a == "--data") opt.data_dir = need("--data");
        else if (a == "--list-ci") { opt.do_list_ci = true; if (i + 1 < argc && argv[i + 1][0] != '-') opt.list_ci_prefix = argv[++i]; }
        else if (a == "--ci-formats") { opt.do_ci_formats = true; opt.ci_formats = need("--ci-formats"); }
        else if (!a.empty() && a[0] == '-' && a != "-") { std::cerr << "pingze: unknown option " << a << "\n"; std::exit(2); }
        else pos.push_back(a);
    }
    if (!opt.color) no_colors();
    if (manual_shi) { opt.shi = true; opt.shi_spec = manual; }

    const std::string data = find_data_dir(opt, argv[0]);
    Phonology phon = Phonology::load(data + "/phonology.tsv", data + "/corrections.tsv");

    if (opt.do_list_ci || opt.do_ci_formats) {
        CipuTable cipu = CipuTable::load(data + "/cipu.dat");
        if (opt.do_ci_formats) {
            const auto* fs = cipu.find(opt.ci_formats);
            if (!fs) { std::cerr << "pingze: unknown 词牌 " << opt.ci_formats << "\n"; return 2; }
            std::cout << opt.ci_formats << " — " << fs->size() << " 体:\n";
            for (size_t i = 0; i < fs->size(); ++i)
                std::cout << "  [" << i << "] " << (*fs)[i].author << "  「" << (*fs)[i].sketch
                          << "」  (" << (*fs)[i].length() << " chars)\n";
            return 0;
        }
        auto names = cipu.names();
        std::sort(names.begin(), names.end());
        int n = 0;
        for (auto& nm : names)
            if (opt.list_ci_prefix.empty() || nm.rfind(opt.list_ci_prefix, 0) == 0) { std::cout << nm << "\n"; ++n; }
        std::cerr << n << " 词牌 (" << cipu.cipai_count() << " total, "
                  << cipu.format_count() << " 体)\n";
        return 0;
    }

    if (pos.empty()) { std::cerr << "pingze: no input file (use '-' for stdin, or --help)\n"; return 2; }
    opt.input = pos.front();
    const std::string raw = slurp(opt.input);
    const std::string text = strip_meta(raw);

    if (opt.explain) { print_explain(phon, text); if (!opt.ci && !opt.shi) return 0; }

    if (opt.ci) {
        CipuTable cipu = CipuTable::load(data + "/cipu.dat");
        CheckResult r = check_ci(phon, cipu, *opt.ci, text, opt.format_index);
        return report(r, opt.input, opt);
    }
    if (opt.shi || true) {  // default to 诗 if nothing else specified
        CheckResult r = check_shi(phon, text, opt.shi_spec);
        return report(r, opt.input, opt);
    }
}
