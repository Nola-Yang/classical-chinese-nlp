#include "stylo/stylo.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace stylo {

std::vector<char32_t> decode_utf8(const std::string& bytes) {
    std::vector<char32_t> out;
    const size_t n = bytes.size();
    size_t i = 0;
    while (i < n) {
        const unsigned char b0 = static_cast<unsigned char>(bytes[i]);
        char32_t cp = 0;
        size_t len = 1;
        if (b0 < 0x80) { cp = b0; len = 1; }
        else if ((b0 & 0xE0) == 0xC0) { cp = b0 & 0x1F; len = 2; }
        else if ((b0 & 0xF0) == 0xE0) { cp = b0 & 0x0F; len = 3; }
        else if ((b0 & 0xF8) == 0xF0) { cp = b0 & 0x07; len = 4; }
        else { cp = 0xFFFD; len = 1; }
        if (i + len > n) { cp = 0xFFFD; len = 1; }
        else for (size_t k = 1; k < len; ++k) {
            const unsigned char bk = static_cast<unsigned char>(bytes[i + k]);
            if ((bk & 0xC0) != 0x80) { cp = 0xFFFD; len = 1; break; }
            cp = (cp << 6) | (bk & 0x3F);
        }
        out.push_back(cp);
        i += len;
    }
    return out;
}

std::string encode_utf8(char32_t cp) {
    std::string s;
    if (cp < 0x80) s.push_back(static_cast<char>(cp));
    else if (cp < 0x800) {
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

bool is_cjk(char32_t cp) {
    return (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
           (cp >= 0xF900 && cp <= 0xFAFF);
}

bool is_punct(char32_t cp) {
    static const std::u32string p =
        U"，。、；：？！「」『』“”‘’（）()《》〈〉…—－·　 \n\r\t,.;:?!\"'-[]{}";
    return !is_cjk(cp) && (p.find(cp) != std::u32string::npos || cp < 0x80 || cp == 0x3000);
}

bool is_strong_stop(char32_t cp) {
    return cp == U'。' || cp == U'；' || cp == U'！' || cp == U'？';
}

namespace {

const std::map<char32_t, char32_t>& variant_map() {
    static const std::map<char32_t, char32_t> v = {
        {U'閒', U'闲'}, {U'鍊', U'炼'}, {U'閤', U'阁'}, {U'捲', U'卷'},
        {U'噉', U'啖'}, {U'馀', U'余'}, {U'裏', U'里'}, {U'衞', U'卫'},
        {U'凈', U'净'}, {U'緑', U'绿'}, {U'喫', U'吃'}, {U'棊', U'棋'},
        {U'陞', U'升'}, {U'脩', U'修'},
    };
    return v;
}

std::string normalize(const std::string& s) {
    const auto& vm = variant_map();
    std::string out;
    for (char32_t cp : decode_utf8(s)) {
        auto it = vm.find(cp);
        out += encode_utf8(it == vm.end() ? cp : it->second);
    }
    return out;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Parse one document chunk (header lines, a ===== separator, then body).
bool parse_doc(const std::string& chunk, const std::string& fname,
               const std::string& group_field, Document& out) {
    std::istringstream in(chunk);
    std::string line;
    std::map<std::string, std::string> meta;
    std::string body;
    bool in_body = false;
    while (std::getline(in, line)) {
        const std::string t = trim(line);
        if (!in_body && t.rfind("=====", 0) == 0) { in_body = true; continue; }
        if (!in_body && !t.empty() && t[0] == '#') {
            const std::string kv = trim(t.substr(1));
            size_t sep = kv.find_first_of(":：");
            // ":：" — find the byte offset of either colon
            size_t pos = std::string::npos;
            for (size_t i = 0; i < kv.size(); ++i) {
                if (kv[i] == ':') { pos = i; break; }
                if (i + 3 <= kv.size() && kv.compare(i, 3, "：") == 0) { pos = i; break; }
            }
            (void)sep;
            if (pos != std::string::npos) {
                std::string key = trim(kv.substr(0, pos));
                size_t vstart = (kv[pos] == ':') ? pos + 1 : pos + 3;
                std::string val = trim(kv.substr(vstart));
                meta[key] = val;
            }
        } else if (in_body) {
            body += line;
            body += "\n";
        }
    }
    out.file = fname;
    out.title = meta.count("标题") ? meta["标题"] : "?";
    out.author = meta.count("作者") ? meta["作者"] : "?";
    out.genre = meta.count("体裁") ? meta["体裁"] : "?";
    // group: requested field, then 作者, then "?"
    if (meta.count(group_field)) out.group = meta[group_field];
    else if (meta.count("作者")) out.group = meta["作者"];
    else out.group = "?";
    out.body = trim(normalize(body));
    out.chars.clear();
    for (char32_t cp : decode_utf8(out.body))
        if (is_cjk(cp)) out.chars.push_back(cp);
    return !out.chars.empty();
}

}  // namespace

std::vector<Document> load_corpus(const std::string& dir, const std::string& group_field) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec))
        throw std::runtime_error("not a directory: " + dir);
    std::vector<std::string> files;
    for (auto& e : fs::directory_iterator(dir))
        if (e.is_regular_file() && e.path().extension() == ".txt")
            files.push_back(e.path().string());
    std::sort(files.begin(), files.end());

    std::vector<Document> docs;
    for (const auto& path : files) {
        std::ifstream f(path, std::ios::binary);
        std::ostringstream ss; ss << f.rdbuf();
        const std::string raw = ss.str();
        const std::string fname = fs::path(path).filename().string();
        // Split on lines of ~~~~~ (multiple docs per file).
        std::vector<std::string> chunks;
        std::istringstream in(raw);
        std::string line, cur;
        while (std::getline(in, line)) {
            if (trim(line).rfind("~~~~~", 0) == 0) { chunks.push_back(cur); cur.clear(); }
            else { cur += line; cur += "\n"; }
        }
        chunks.push_back(cur);
        for (const auto& ch : chunks) {
            if (ch.find("=====") == std::string::npos) continue;
            Document d;
            if (parse_doc(ch, fname, group_field, d)) docs.push_back(std::move(d));
        }
    }
    return docs;
}

}  // namespace stylo
