// stylo-advanced — extra discrimination methods, kept separate from the
// validated core so the reference numbers in `stylo` never move.
//
// Adds, on top of the engine's cosine / Burrows-Δ / bigram-LM:
//   * top-N most-frequent-character features (the canonical Burrows feature set)
//   * Cosine Delta (Eder/Smith–Aldridge) — more robust on tiny samples
//   * tf-idf-weighted cosine
//   * 1-nearest-neighbour leave-one-out (per poem, not per centroid)
//   * prosodic features: 平/仄/入声 rates + tonal-contour bigrams, using the
//     prosody subproject's 平水韵 table (an orthogonal, domain-specific signal)
//   * a label-permutation test for whether the target's nearest group is real
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "stylo/stylo.hpp"

namespace fs = std::filesystem;
using namespace stylo;

namespace {

std::string numf(double x, int nd = 4) {
    if (std::isinf(x)) return "inf";
    std::ostringstream ss; ss.setf(std::ios::fixed); ss.precision(nd); ss << x; return ss.str();
}
struct Csv {
    std::ofstream f; explicit Csv(const std::string& p) : f(p) {}
    void row(const std::vector<std::string>& c) {
        for (size_t i = 0; i < c.size(); ++i) { if (i) f << ","; f << c[i]; } f << "\n";
    }
};

// ---- feature views ---------------------------------------------------------

// Top-N most frequent characters across the whole corpus.
std::vector<char32_t> top_mfw(const Corpus& c, int n) {
    std::map<char32_t, long> tot;
    for (auto& d : c.docs) for (char32_t ch : d.chars) ++tot[ch];
    std::vector<std::pair<long, char32_t>> v;
    for (auto& kv : tot) v.push_back({kv.second, kv.first});
    std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first > b.first; });
    std::vector<char32_t> out;
    for (int i = 0; i < n && i < (int)v.size(); ++i) out.push_back(v[i].second);
    return out;
}

// Inverse document frequency per vocab entry (smoothed).
std::vector<double> idf_for(const Corpus& c, const std::vector<char32_t>& vocab) {
    const int N = (int)c.docs.size();
    std::vector<double> idf(vocab.size(), 0);
    std::map<char32_t, int> df;
    for (auto& d : c.docs) {
        std::set<char32_t> seen(d.chars.begin(), d.chars.end());
        for (char32_t ch : seen) ++df[ch];
    }
    for (size_t i = 0; i < vocab.size(); ++i) {
        auto it = df.find(vocab[i]);
        const int d = it == df.end() ? 0 : it->second;
        idf[i] = std::log((double)(N + 1) / (d + 1)) + 1.0;
    }
    return idf;
}
std::vector<double> tfidf(const Counter& cnt, const std::vector<char32_t>& vocab,
                          const std::vector<double>& idf) {
    auto tf = relfreq(cnt, vocab);
    for (size_t i = 0; i < tf.size(); ++i) tf[i] *= idf[i];
    return tf;
}

// ---- prosodic features (uses the prosody table) ----------------------------

struct Phon {
    std::map<char32_t, int> tone;  // 1=平 only, 2=仄 only, 0=ambiguous/unknown
    std::map<char32_t, bool> ru;
};
Phon load_phon(const std::string& path) {
    Phon p;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        // char \t tones(平上去入) \t ... ; split on tab
        auto t1 = line.find('\t');
        if (t1 == std::string::npos) continue;
        auto t2 = line.find('\t', t1 + 1);
        std::string cs = line.substr(0, t1);
        std::string tones = line.substr(t1 + 1, (t2 == std::string::npos ? line.size() : t2) - t1 - 1);
        auto cps = decode_utf8(cs);
        if (cps.empty()) continue;
        const char32_t cp = cps[0];
        bool ping = false, ze = false, ru = false;
        for (char32_t tc : decode_utf8(tones)) {
            if (tc == U'平') ping = true;
            else if (tc == U'上' || tc == U'去') ze = true;
            else if (tc == U'入') { ze = true; ru = true; }
        }
        p.tone[cp] = (ping && !ze) ? 1 : (ze && !ping) ? 2 : 0;
        p.ru[cp] = ru;
    }
    return p;
}
// 7-dim prosodic vector: [平率, 仄率, 入率, PP, PZ, ZP, ZZ] (contour bigrams).
std::vector<double> prosodic_vec(const Document& d, const Phon& ph) {
    long nping = 0, nze = 0, nru = 0, ncls = 0;
    long bg[4] = {0, 0, 0, 0};
    int prev = 0;
    for (char32_t ch : d.chars) {
        auto it = ph.tone.find(ch);
        const int t = it == ph.tone.end() ? 0 : it->second;  // 1 平, 2 仄, 0 skip
        if (t == 1) { ++nping; ++ncls; }
        else if (t == 2) { ++nze; ++ncls; auto r = ph.ru.find(ch); if (r != ph.ru.end() && r->second) ++nru; }
        if (t && prev) { bg[(prev - 1) * 2 + (t - 1)]++; }
        prev = t;
    }
    const double tot = ncls ? ncls : 1;
    const double nb = bg[0] + bg[1] + bg[2] + bg[3];
    const double nbd = nb ? nb : 1;
    return {nping / tot, nze / tot, nru / tot,
            bg[0] / nbd, bg[1] / nbd, bg[2] / nbd, bg[3] / nbd};
}

// ---- generic helpers -------------------------------------------------------

std::vector<int> class_groups(const Corpus& c, const std::vector<std::string>& gnames,
                              const std::string& target, int loo_min,
                              std::vector<std::string>& classes) {
    std::vector<int> dummy;
    for (auto& g : gnames)
        if ((int)c.groups.at(g).size() >= loo_min && g != target) classes.push_back(g);
    return dummy;
}

}  // namespace

int main(int argc, char** argv) {
    std::string corpus, out = "out_adv", target, group_field = "类别", phon_path;
    int mfw_n = 100, loo_min = 5, perm = 5000;
    std::vector<std::string> pos;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto need = [&]() { return std::string(argv[++i]); };
        if (a == "--out") out = need();
        else if (a == "--target") target = need();
        else if (a == "--group-field") group_field = need();
        else if (a == "--phonology") phon_path = need();
        else if (a == "--mfw") mfw_n = std::stoi(need());
        else if (a == "--loo-min") loo_min = std::stoi(need());
        else if (a == "--perm") perm = std::stoi(need());
        else if (a == "-h" || a == "--help") {
            std::cout << "usage: stylo-advanced <corpus> --target G [--phonology f.tsv] "
                         "[--mfw 100] [--loo-min 5] [--perm 5000] [--out dir]\n";
            return 0;
        } else pos.push_back(a);
    }
    if (pos.empty()) { std::cerr << "stylo-advanced: need a corpus dir\n"; return 2; }
    corpus = pos.front();

    Corpus c = Corpus::build(load_corpus(corpus, group_field), default_function_words());
    if (c.docs.empty()) { std::cerr << "no docs\n"; return 2; }
    fs::create_directories(out);
    std::vector<std::string> gnames;
    for (auto& kv : c.groups) gnames.push_back(kv.first);
    std::sort(gnames.begin(), gnames.end());

    const auto mfw = top_mfw(c, mfw_n);
    const auto idf = idf_for(c, c.vocab_uni);
    DeltaModel dm_fw = fit_delta(c.docs, c.func_words);
    DeltaModel dm_mfw = fit_delta(c.docs, mfw);
    Phon ph;
    bool have_phon = !phon_path.empty();
    if (have_phon) ph = load_phon(phon_path);

    // A view = a function doc-index -> feature vector.
    auto vec_uni  = [&](int i) { return relfreq(char_counts(c.docs[i].chars), c.vocab_uni); };
    auto vec_bi   = [&](int i) { return relfreq(bigram_counts(c.docs[i].chars), c.vocab_bi); };
    auto vec_fw   = [&](int i) { return relfreq(char_counts(c.docs[i].chars), c.func_words); };
    auto vec_mfw  = [&](int i) { return relfreq(char_counts(c.docs[i].chars), mfw); };
    auto vec_tfidf= [&](int i) { return tfidf(char_counts(c.docs[i].chars), c.vocab_uni, idf); };
    auto vec_pros = [&](int i) { return prosodic_vec(c.docs[i], ph); };

    struct View { std::string name; std::function<std::vector<double>(int)> f; };
    std::vector<View> views = {
        {"func", vec_fw}, {"uni", vec_uni}, {"bigram", vec_bi},
        {"mfw" + std::to_string(mfw_n), vec_mfw}, {"tfidf-uni", vec_tfidf}};
    if (have_phon) views.push_back({"prosodic", vec_pros});

    std::vector<std::string> classes;
    class_groups(c, gnames, target, loo_min, classes);

    // ---- 1) 1-NN leave-one-out accuracy (per poem) ----
    {
        Csv csv(out + "/adv_1nn_loo.csv");
        csv.row({"view", "n_classes", "n_docs", "knn1_loo_accuracy", "centroid_loo_accuracy"});
        for (auto& v : views) {
            // precompute vectors
            std::map<int, std::vector<double>> V;
            std::vector<int> all;
            for (auto& cls : classes) for (int d : c.groups[cls]) { V[d] = v.f(d); all.push_back(d); }
            int knn_ok = 0, cen_ok = 0, tot = 0;
            for (int d : all) {
                // 1-NN: nearest other doc
                double best = -1e300; std::string pred;
                for (int e : all) if (e != d) {
                    double s = cosine(V[d], V[e]);
                    if (s > best) { best = s; pred = c.docs[e].group; }
                }
                knn_ok += (pred == c.docs[d].group);
                // centroid: nearest class centroid (excluding d)
                double cb = -1e300; std::string cp;
                for (auto& cls : classes) {
                    std::vector<char32_t> all_ch; int cnt = 0;
                    for (int e : c.groups[cls]) if (e != d) { all_ch.insert(all_ch.end(), c.docs[e].chars.begin(), c.docs[e].chars.end()); ++cnt; }
                    if (!cnt) continue;
                    // build centroid in same view by faking a doc
                    Document tmp; tmp.chars = all_ch;
                    std::vector<double> cv;
                    if (v.name == "prosodic" && have_phon) cv = prosodic_vec(tmp, ph);
                    else if (v.name == "bigram") cv = relfreq(bigram_counts(all_ch), c.vocab_bi);
                    else if (v.name == "func") cv = relfreq(char_counts(all_ch), c.func_words);
                    else if (v.name.rfind("mfw", 0) == 0) cv = relfreq(char_counts(all_ch), mfw);
                    else if (v.name == "tfidf-uni") cv = tfidf(char_counts(all_ch), c.vocab_uni, idf);
                    else cv = relfreq(char_counts(all_ch), c.vocab_uni);
                    double s = cosine(V[d], cv);
                    if (s > cb) { cb = s; cp = cls; }
                }
                cen_ok += (cp == c.docs[d].group);
                ++tot;
            }
            csv.row({v.name, std::to_string(classes.size()), std::to_string(tot),
                     tot ? numf((double)knn_ok / tot) : "0", tot ? numf((double)cen_ok / tot) : "0"});
        }
    }

    if (target.empty() || !c.groups.count(target)) {
        std::cerr << "(no --target group; skipped target-centric tables)\n";
        return 0;
    }
    const auto& T = c.groups[target];
    auto group_centroid_view = [&](const std::string& g, const View& v) {
        std::vector<char32_t> all;
        for (int e : c.groups[g]) all.insert(all.end(), c.docs[e].chars.begin(), c.docs[e].chars.end());
        Document tmp; tmp.chars = all;
        if (v.name == "prosodic" && have_phon) return prosodic_vec(tmp, ph);
        if (v.name == "bigram") return relfreq(bigram_counts(all), c.vocab_bi);
        if (v.name == "func") return relfreq(char_counts(all), c.func_words);
        if (v.name.rfind("mfw", 0) == 0) return relfreq(char_counts(all), mfw);
        if (v.name == "tfidf-uni") return tfidf(char_counts(all), c.vocab_uni, idf);
        return relfreq(char_counts(all), c.vocab_uni);
    };

    // ---- 2) target nearest group across all methods ----
    {
        Csv csv(out + "/adv_target_nearest.csv");
        csv.row({"method", "nearest_group", "score", "ranking(closer=left)"});
        for (auto& v : views) {
            auto tv = group_centroid_view(target, v);
            std::vector<std::pair<double, std::string>> r;
            for (auto& g : gnames) if (g != target) r.push_back({cosine(tv, group_centroid_view(g, v)), g});
            std::sort(r.begin(), r.end(), [](auto& a, auto& b) { return a.first > b.first; });
            std::string rk;
            for (size_t i = 0; i < r.size(); ++i) rk += (i ? " > " : "") + r[i].second + "(" + numf(r[i].first, 3) + ")";
            csv.row({"cos:" + v.name, r.empty() ? "" : r.front().second, r.empty() ? "" : numf(r.front().first, 3), rk});
        }
        // Cosine-Delta and classic Delta on the MFW feature set (smaller = closer)
        std::vector<char32_t> tch;
        for (int e : T) tch.insert(tch.end(), c.docs[e].chars.begin(), c.docs[e].chars.end());
        auto tz = dm_mfw.zscore(relfreq(char_counts(tch), mfw));
        for (int mode = 0; mode < 2; ++mode) {  // 0 = classic Δ, 1 = cosine-Δ
            std::vector<std::pair<double, std::string>> r;
            for (auto& g : gnames) {
                if (g == target) continue;
                std::vector<char32_t> all;
                for (int e : c.groups[g]) all.insert(all.end(), c.docs[e].chars.begin(), c.docs[e].chars.end());
                auto gz = dm_mfw.zscore(relfreq(char_counts(all), mfw));
                double d = mode == 0 ? delta(dm_mfw, tz, gz) : (1.0 - cosine(tz, gz));
                r.push_back({d, g});
            }
            std::sort(r.begin(), r.end());
            std::string rk;
            for (size_t i = 0; i < r.size(); ++i) rk += (i ? " < " : "") + r[i].second + "(" + numf(r[i].first, 3) + ")";
            csv.row({mode == 0 ? "burrowsDelta:mfw" : "cosineDelta:mfw",
                     r.empty() ? "" : r.front().second, r.empty() ? "" : numf(r.front().first, 3), rk});
        }
    }

    // ---- 3) each target poem's single nearest control poem ----
    {
        Csv csv(out + "/adv_target_1nn_perdoc.csv");
        csv.row({"target_poem", "view", "nearest_poem", "nearest_group", "cosine"});
        for (int d : T) {
            for (auto& v : views) {
                auto dv = v.f(d);
                double best = -1e300; int who = -1;
                for (auto& g : gnames) if (g != target) for (int e : c.groups[g]) {
                    double s = cosine(dv, v.f(e));
                    if (s > best) { best = s; who = e; }
                }
                if (who >= 0)
                    csv.row({c.docs[d].title, v.name, c.docs[who].title, c.docs[who].group, numf(best, 3)});
            }
        }
    }

    // ---- 4) permutation test: is the target's nearest group real? ----
    {
        Csv csv(out + "/adv_permutation.csv");
        csv.row({"view", "observed_max_cos", "nearest_group", "perm_p_value", "n_perm"});
        std::mt19937 rng(12345);
        // non-target docs and their group sizes
        std::vector<int> nd;
        std::vector<std::string> ng;
        for (auto& g : gnames) if (g != target) for (int e : c.groups[g]) { nd.push_back(e); ng.push_back(g); }
        for (auto& v : views) {
            auto tv = group_centroid_view(target, v);
            // observed
            double obs = -1e300; std::string near;
            for (auto& g : gnames) if (g != target) {
                double s = cosine(tv, group_centroid_view(g, v));
                if (s > obs) { obs = s; near = g; }
            }
            // permute labels
            int ge = 0;
            std::vector<std::string> labels = ng;
            for (int p = 0; p < perm; ++p) {
                std::shuffle(labels.begin(), labels.end(), rng);
                std::map<std::string, std::vector<char32_t>> bag;
                for (size_t i = 0; i < nd.size(); ++i)
                    bag[labels[i]].insert(bag[labels[i]].end(), c.docs[nd[i]].chars.begin(), c.docs[nd[i]].chars.end());
                double pm = -1e300;
                for (auto& kv : bag) {
                    Document tmp; tmp.chars = kv.second;
                    std::vector<double> cv;
                    if (v.name == "prosodic" && have_phon) cv = prosodic_vec(tmp, ph);
                    else if (v.name == "bigram") cv = relfreq(bigram_counts(kv.second), c.vocab_bi);
                    else if (v.name == "func") cv = relfreq(char_counts(kv.second), c.func_words);
                    else if (v.name.rfind("mfw", 0) == 0) cv = relfreq(char_counts(kv.second), mfw);
                    else if (v.name == "tfidf-uni") cv = tfidf(char_counts(kv.second), c.vocab_uni, idf);
                    else cv = relfreq(char_counts(kv.second), c.vocab_uni);
                    double s = cosine(tv, cv);
                    if (s > pm) pm = s;
                }
                if (pm >= obs) ++ge;
            }
            const double pval = (double)(ge + 1) / (perm + 1);
            csv.row({v.name, numf(obs, 3), near, numf(pval, 4), std::to_string(perm)});
        }
    }

    std::cerr << "stylo-advanced: " << c.docs.size() << " docs, target=" << target
              << ", mfw=" << mfw_n << (have_phon ? ", +prosodic" : "")
              << " -> " << out << "/\n";
    return 0;
}
