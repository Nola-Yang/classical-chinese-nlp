#include "stylo/stylo.hpp"

#include <cmath>
#include <fstream>
#include <set>
#include <sstream>

namespace stylo {

Counter char_counts(const std::vector<char32_t>& chars) {
    Counter c;
    for (char32_t x : chars) ++c[x];
    return c;
}

StrCounter bigram_counts(const std::vector<char32_t>& chars) {
    StrCounter c;
    for (size_t i = 0; i + 1 < chars.size(); ++i)
        ++c[encode_utf8(chars[i]) + encode_utf8(chars[i + 1])];
    return c;
}

std::vector<double> relfreq(const Counter& c, const std::vector<char32_t>& vocab) {
    long tot = 0;
    for (auto& kv : c) tot += kv.second;
    if (tot == 0) tot = 1;
    std::vector<double> v(vocab.size());
    for (size_t i = 0; i < vocab.size(); ++i) {
        auto it = c.find(vocab[i]);
        v[i] = it == c.end() ? 0.0 : static_cast<double>(it->second) / tot;
    }
    return v;
}

std::vector<double> relfreq(const StrCounter& c, const std::vector<std::string>& vocab) {
    long tot = 0;
    for (auto& kv : c) tot += kv.second;
    if (tot == 0) tot = 1;
    std::vector<double> v(vocab.size());
    for (size_t i = 0; i < vocab.size(); ++i) {
        auto it = c.find(vocab[i]);
        v[i] = it == c.end() ? 0.0 : static_cast<double>(it->second) / tot;
    }
    return v;
}

Features compute_features(const Document& d, const std::vector<char32_t>& func_words) {
    Features f;
    const auto& ch = d.chars;
    f.n_chars = static_cast<int>(ch.size());
    const int n = f.n_chars ? f.n_chars : 1;
    Counter c = char_counts(ch);
    f.n_types = static_cast<int>(c.size());
    f.ttr = static_cast<double>(f.n_types) / n;

    long hapax = 0, m2 = 0, pairs = 0;
    double H = 0;
    for (auto& kv : c) {
        const long v = kv.second;
        if (v == 1) ++hapax;
        m2 += v * v;
        pairs += v * (v - 1);
        const double p = static_cast<double>(v) / n;
        H -= p * std::log2(p);
    }
    f.hapax_ratio = f.n_types ? static_cast<double>(hapax) / f.n_types : 0;
    f.shannon_bits = H;
    f.yule_k = 1e4 * (static_cast<double>(m2) - n) / (static_cast<double>(n) * n);
    f.simpson_d = f.n_chars >= 2
        ? static_cast<double>(pairs) / (static_cast<double>(n) * (n - 1)) : 0.0;

    std::set<char32_t> fset(func_words.begin(), func_words.end());
    long fw = 0;
    for (char32_t x : ch) if (fset.count(x)) ++fw;
    f.func_word_rate = static_cast<double>(fw) / n;

    // Clauses: split on CJK punctuation / whitespace; measure mean length.
    long clause_total = 0;
    int clause_count = 0, cur = 0;
    for (char32_t cp : decode_utf8(d.body)) {
        const bool brk = cp == U'，' || cp == U'。' || cp == U'、' || cp == U'；' ||
                         cp == U'：' || cp == U'？' || cp == U'！' || cp == U' ' ||
                         cp == U'\n' || cp == U'\r' || cp == U'\t' || cp == 0x3000;
        if (brk) { if (cur) { clause_total += cur; ++clause_count; cur = 0; } }
        else ++cur;
    }
    if (cur) { clause_total += cur; ++clause_count; }
    f.n_clauses = clause_count;
    f.mean_clause_len = clause_count ? static_cast<double>(clause_total) / clause_count : 0;
    return f;
}

// ---- distances -------------------------------------------------------------

double cosine(const std::vector<double>& a, const std::vector<double>& b) {
    double dot = 0, na = 0, nb = 0;
    for (size_t i = 0; i < a.size(); ++i) { dot += a[i] * b[i]; na += a[i] * a[i]; nb += b[i] * b[i]; }
    return (na && nb) ? dot / (std::sqrt(na) * std::sqrt(nb)) : 0.0;
}
double manhattan(const std::vector<double>& a, const std::vector<double>& b) {
    double s = 0; for (size_t i = 0; i < a.size(); ++i) s += std::fabs(a[i] - b[i]); return s;
}
double euclid(const std::vector<double>& a, const std::vector<double>& b) {
    double s = 0; for (size_t i = 0; i < a.size(); ++i) s += (a[i] - b[i]) * (a[i] - b[i]);
    return std::sqrt(s);
}
double jsd(const std::vector<double>& p, const std::vector<double>& q) {
    auto kl = [](const std::vector<double>& a, const std::vector<double>& b) {
        double s = 0;
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i] > 0 && b[i] > 0) s += a[i] * std::log2(a[i] / b[i]);
        return s;
    };
    std::vector<double> m(p.size());
    for (size_t i = 0; i < p.size(); ++i) m[i] = (p[i] + q[i]) / 2;
    return 0.5 * kl(p, m) + 0.5 * kl(q, m);
}
double kl_smooth(const std::vector<double>& p, const std::vector<double>& q, double eps) {
    double s = 0;
    for (size_t i = 0; i < p.size(); ++i)
        if (p[i] > 0) s += p[i] * std::log2(p[i] / (q[i] + eps));
    return s;
}

// ---- Burrows's Delta -------------------------------------------------------

std::vector<double> DeltaModel::zscore(const std::vector<double>& v) const {
    std::vector<double> z(v.size());
    for (size_t i = 0; i < v.size(); ++i) z[i] = (v[i] - mean[i]) / sd[i];
    return z;
}

DeltaModel fit_delta(const std::vector<Document>& docs,
                     const std::vector<char32_t>& func_words) {
    DeltaModel m;
    m.vocab = func_words;
    const size_t V = func_words.size();
    std::vector<std::vector<double>> mat;
    mat.reserve(docs.size());
    for (const auto& d : docs)
        mat.push_back(relfreq(char_counts(d.chars), func_words));
    m.mean.assign(V, 0); m.sd.assign(V, 1e-9);
    if (mat.empty()) return m;
    for (size_t j = 0; j < V; ++j) {
        double s = 0; for (auto& row : mat) s += row[j];
        m.mean[j] = s / mat.size();
    }
    for (size_t j = 0; j < V; ++j) {
        double s = 0; for (auto& row : mat) { const double d = row[j] - m.mean[j]; s += d * d; }
        const double sd = std::sqrt(s / mat.size());
        m.sd[j] = sd > 0 ? sd : 1e-9;
    }
    return m;
}

double delta(const DeltaModel& m, const std::vector<double>& za,
             const std::vector<double>& zb) {
    (void)m;  // za / zb are already z-scored; m is kept for API symmetry
    if (za.empty()) return 0;
    double s = 0; for (size_t i = 0; i < za.size(); ++i) s += std::fabs(za[i] - zb[i]);
    return s / za.size();
}

// ---- bigram language model -------------------------------------------------

BigramLM::BigramLM(const std::vector<char32_t>& train, int vocab_size, double k)
    : V_(vocab_size), k_(k) {
    char32_t prev = 0;  // sentinel "start"
    for (char32_t c : train) { ++bi_[{prev, c}]; ++uni_[prev]; prev = c; }
}

std::pair<double, double> BigramLM::evaluate(const std::vector<char32_t>& test) const {
    if (test.empty()) return {1e300, 1e300};
    char32_t prev = 0;
    double tot = 0;
    for (char32_t c : test) {
        auto bit = bi_.find({prev, c});
        auto uit = uni_.find(prev);
        const double num = (bit == bi_.end() ? 0 : bit->second) + k_;
        const double den = (uit == uni_.end() ? 0 : uit->second) + k_ * V_;
        tot += std::log(num / den);
        prev = c;
    }
    const double avg = tot / test.size();
    return {std::exp(-avg), -avg / std::log(2.0)};
}

// ---- corpus assembly -------------------------------------------------------

Corpus Corpus::build(std::vector<Document> docs, std::vector<char32_t> func_words) {
    Corpus c;
    c.docs = std::move(docs);
    std::set<char32_t> uni;
    std::set<std::string> bi;
    for (size_t i = 0; i < c.docs.size(); ++i) {
        const auto& d = c.docs[i];
        for (char32_t x : d.chars) uni.insert(x);
        for (auto& kv : bigram_counts(d.chars)) bi.insert(kv.first);
        c.groups[d.group].push_back(static_cast<int>(i));
    }
    c.vocab_uni.assign(uni.begin(), uni.end());
    c.vocab_bi.assign(bi.begin(), bi.end());
    // Keep only function words that actually occur in the corpus. Burrows's Delta
    // divides by the feature count, so carrying absent words would deflate it;
    // standard practice (and the reference Python implementation) restricts to
    // the present set. Cosine is unaffected (absent words are zero dimensions).
    for (char32_t w : func_words)
        if (uni.count(w)) c.func_words.push_back(w);
    return c;
}

std::vector<char32_t> Corpus::group_chars(const std::string& g) const {
    std::vector<char32_t> out;
    auto it = groups.find(g);
    if (it == groups.end()) return out;
    for (int idx : it->second)
        out.insert(out.end(), docs[idx].chars.begin(), docs[idx].chars.end());
    return out;
}

// ---- function words --------------------------------------------------------

std::vector<char32_t> default_function_words() {
    static const std::string s =
        "之乎者也矣焉哉耳兮而以于於其则乃且因由从向与及至故若如苟虽纵但"
        "不非弗未莫勿无亦又犹尚已即既必当应须将欲能可得方更最甚颇略"
        "何谁孰安岂宁是此彼斯夫盖凡所自相为"
        "处却待还空漫只惟唯独都尽满复向归来去上下中前后"
        "余予我吾臣君卿尔汝子";
    std::vector<char32_t> out;
    std::set<char32_t> seen;
    for (char32_t cp : decode_utf8(s))
        if (is_cjk(cp) && seen.insert(cp).second) out.push_back(cp);
    return out;
}

std::vector<char32_t> load_function_words(const std::string& path) {
    std::ifstream f(path);
    if (!f) return default_function_words();
    std::ostringstream ss; ss << f.rdbuf();
    std::vector<char32_t> out;
    std::set<char32_t> seen;
    for (char32_t cp : decode_utf8(ss.str()))
        if (is_cjk(cp) && seen.insert(cp).second) out.push_back(cp);
    return out.empty() ? default_function_words() : out;
}

}  // namespace stylo
