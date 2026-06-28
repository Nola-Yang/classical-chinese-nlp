// stylo — run the stylometry suite over a corpus and emit reviewable CSVs.
//
// Design rule borrowed from the companion project: the program COMPUTES, it does
// not CONCLUDE. Output is a set of numeric tables; interpretation is left to a
// human (and to each case study's write-up).
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "stylo/stylo.hpp"

namespace fs = std::filesystem;
using namespace stylo;

namespace {

struct Opts {
    std::string corpus;
    std::string out = "out";
    std::string target;
    std::string group_field = "类别";
    std::string funcwords;
    int loo_min = 2;
};

std::string num(double x, int nd = 4) {
    if (std::isinf(x)) return "inf";
    std::ostringstream ss;
    ss.setf(std::ios::fixed);
    ss.precision(nd);
    ss << x;
    return ss.str();
}

struct Csv {
    std::ofstream f;
    explicit Csv(const std::string& path) : f(path) {}
    void row(const std::vector<std::string>& cells) {
        for (size_t i = 0; i < cells.size(); ++i) { if (i) f << ","; f << cells[i]; }
        f << "\n";
    }
};

// Relative-frequency vectors for a set of docs concatenated (a group centroid),
// in three "views".
std::vector<double> cen_uni(const Corpus& c, const std::vector<int>& idx) {
    std::vector<char32_t> all;
    for (int i : idx) all.insert(all.end(), c.docs[i].chars.begin(), c.docs[i].chars.end());
    return relfreq(char_counts(all), c.vocab_uni);
}
std::vector<double> cen_bi(const Corpus& c, const std::vector<int>& idx) {
    std::vector<char32_t> all;
    for (int i : idx) all.insert(all.end(), c.docs[i].chars.begin(), c.docs[i].chars.end());
    return relfreq(bigram_counts(all), c.vocab_bi);
}
std::vector<double> cen_fw(const Corpus& c, const std::vector<int>& idx) {
    std::vector<char32_t> all;
    for (int i : idx) all.insert(all.end(), c.docs[i].chars.begin(), c.docs[i].chars.end());
    return relfreq(char_counts(all), c.func_words);
}

enum View { FW, UNI, BI };
std::vector<double> doc_vec(const Corpus& c, int i, View v) {
    if (v == BI) return relfreq(bigram_counts(c.docs[i].chars), c.vocab_bi);
    return relfreq(char_counts(c.docs[i].chars), v == FW ? c.func_words : c.vocab_uni);
}
std::vector<double> centroid(const Corpus& c, const std::vector<int>& idx, View v) {
    if (v == BI) return cen_bi(c, idx);
    if (v == FW) return cen_fw(c, idx);
    return cen_uni(c, idx);
}

}  // namespace

int main(int argc, char** argv) {
    Opts o;
    std::vector<std::string> pos;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto need = [&]() { return std::string(argv[++i]); };
        if (a == "--out") o.out = need();
        else if (a == "--target") o.target = need();
        else if (a == "--group-field") o.group_field = need();
        else if (a == "--funcwords") o.funcwords = need();
        else if (a == "--loo-min") o.loo_min = std::stoi(need());
        else if (a == "-h" || a == "--help") {
            std::cout << "usage: stylo <corpus_dir> [--out dir] [--target group] "
                         "[--group-field 类别] [--funcwords file] [--loo-min N]\n";
            return 0;
        } else pos.push_back(a);
    }
    if (pos.empty()) { std::cerr << "stylo: need a corpus directory (see --help)\n"; return 2; }
    o.corpus = pos.front();

    std::vector<char32_t> fw = o.funcwords.empty() ? default_function_words()
                                                   : load_function_words(o.funcwords);
    Corpus c = Corpus::build(load_corpus(o.corpus, o.group_field), fw);
    if (c.docs.empty()) { std::cerr << "stylo: no documents found in " << o.corpus << "\n"; return 2; }
    fw = c.func_words;  // restrict to the function words present in this corpus
    fs::create_directories(o.out);

    // Stable, sorted group list.
    std::vector<std::string> gnames;
    for (auto& kv : c.groups) gnames.push_back(kv.first);
    std::sort(gnames.begin(), gnames.end());

    // ---- 1. per-document features ----
    {
        Csv csv(o.out + "/01_features.csv");
        csv.row({"title", "author", "group", "genre", "n_chars", "n_types", "TTR",
                 "hapax_ratio", "shannon_bits", "yule_K", "simpson_D",
                 "mean_clause_len", "n_clauses", "func_word_rate"});
        for (auto& d : c.docs) {
            Features f = compute_features(d, fw);
            csv.row({d.title, d.author, d.group, d.genre, std::to_string(f.n_chars),
                     std::to_string(f.n_types), num(f.ttr), num(f.hapax_ratio),
                     num(f.shannon_bits), num(f.yule_k, 2), num(f.simpson_d),
                     num(f.mean_clause_len, 2), std::to_string(f.n_clauses),
                     num(f.func_word_rate)});
        }
    }

    DeltaModel dm = fit_delta(c.docs, fw);

    // ---- 2. group x group centroid distances ----
    {
        Csv csv(o.out + "/02_group_distances.csv");
        csv.row({"group_a", "group_b", "cos_func", "cos_uni", "cos_bi",
                 "burrows_delta_func", "jsd_uni", "jsd_bi"});
        for (size_t i = 0; i < gnames.size(); ++i)
            for (size_t j = i + 1; j < gnames.size(); ++j) {
                const auto& A = c.groups[gnames[i]];
                const auto& B = c.groups[gnames[j]];
                auto fa = cen_fw(c, A), fb = cen_fw(c, B);
                auto ua = cen_uni(c, A), ub = cen_uni(c, B);
                auto ba = cen_bi(c, A), bb = cen_bi(c, B);
                csv.row({gnames[i], gnames[j], num(cosine(fa, fb)), num(cosine(ua, ub)),
                         num(cosine(ba, bb)),
                         num(delta(dm, dm.zscore(fa), dm.zscore(fb))),
                         num(jsd(ua, ub)), num(jsd(ba, bb))});
            }
    }

    // ---- 3. document distance matrices ----
    for (int which = 0; which < 2; ++which) {
        Csv csv(o.out + (which == 0 ? "/03a_distmatrix_bigram_cosine.csv"
                                    : "/03b_distmatrix_unigram_jsd.csv"));
        std::vector<std::string> head = {""};
        for (auto& d : c.docs) head.push_back(d.group + "|" + d.title);
        csv.row(head);
        for (size_t i = 0; i < c.docs.size(); ++i) {
            std::vector<std::string> r = {c.docs[i].group + "|" + c.docs[i].title};
            auto vi = which == 0 ? doc_vec(c, (int)i, BI) : doc_vec(c, (int)i, UNI);
            for (size_t j = 0; j < c.docs.size(); ++j) {
                auto vj = which == 0 ? doc_vec(c, (int)j, BI) : doc_vec(c, (int)j, UNI);
                r.push_back(num(which == 0 ? cosine(vi, vj) : jsd(vi, vj)));
            }
            csv.row(r);
        }
    }

    // Classes for LOO: groups with >= loo_min docs, excluding the target.
    std::vector<std::string> classes;
    for (auto& g : gnames)
        if ((int)c.groups[g].size() >= o.loo_min && g != o.target) classes.push_back(g);

    // ---- 4. leave-one-out nearest-centroid accuracy ----
    {
        Csv csv(o.out + "/04_loo_accuracy.csv");
        csv.row({"view", "n_classes", "n_docs", "loo_accuracy"});
        const std::vector<std::pair<std::string, View>> views =
            {{"func", FW}, {"uni", UNI}, {"bi", BI}};
        Csv detail(o.out + "/04b_loo_detail.csv");
        detail.row({"view", "true_group", "title", "predicted"});
        for (auto& [vlabel, v] : views) {
            int correct = 0, total = 0;
            for (auto& cls : classes) {
                for (int d : c.groups[cls]) {
                    auto vec = doc_vec(c, d, v);
                    std::string best;
                    double bestsim = -1e300;
                    for (auto& other : classes) {
                        std::vector<int> idx;
                        for (int x : c.groups[other]) if (x != d) idx.push_back(x);
                        if (idx.empty()) continue;
                        const double s = cosine(vec, centroid(c, idx, v));
                        if (s > bestsim) { bestsim = s; best = other; }
                    }
                    correct += (best == cls);
                    ++total;
                    detail.row({vlabel, cls, c.docs[d].title, best});
                }
            }
            csv.row({vlabel, std::to_string(classes.size()), std::to_string(total),
                     total ? num((double)correct / total) : "0"});
        }
    }

    // ---- 5 & 6. target-centric tables (the authorship question) ----
    if (!o.target.empty() && c.groups.count(o.target)) {
        const auto& T = c.groups[o.target];
        // 5. nearest group to the target, per view
        Csv csv(o.out + "/05_target_nearest_group.csv");
        csv.row({"view", "nearest_group", "cosine", "ranking(group=cos; smaller_rank=closer)"});
        const std::vector<std::pair<std::string, View>> views =
            {{"func", FW}, {"uni", UNI}, {"bi", BI}};
        for (auto& [vlabel, v] : views) {
            auto tv = centroid(c, T, v);
            std::vector<std::pair<double, std::string>> ranked;
            for (auto& g : gnames) {
                if (g == o.target) continue;
                ranked.push_back({cosine(tv, centroid(c, c.groups[g], v)), g});
            }
            std::sort(ranked.begin(), ranked.end(), [](auto& a, auto& b) { return a.first > b.first; });
            std::string rk;
            for (size_t i = 0; i < ranked.size(); ++i)
                rk += (i ? " > " : "") + ranked[i].second + "(" + num(ranked[i].first, 3) + ")";
            csv.row({vlabel, ranked.empty() ? "" : ranked.front().second,
                     ranked.empty() ? "" : num(ranked.front().first), rk});
        }
        // Burrows Delta to each group (smaller = closer).
        Csv dcsv(o.out + "/05b_target_burrows_delta.csv");
        dcsv.row({"group", "burrows_delta_to_target(smaller=closer)"});
        auto tz = dm.zscore(cen_fw(c, T));
        std::vector<std::pair<double, std::string>> dl;
        for (auto& g : gnames) {
            if (g == o.target) continue;
            dl.push_back({delta(dm, tz, dm.zscore(cen_fw(c, c.groups[g]))), g});
        }
        std::sort(dl.begin(), dl.end());
        for (auto& [d, g] : dl) dcsv.row({g, num(d)});

        // 6. bigram-LM perplexity: train on each group, score the target.
        Csv lcsv(o.out + "/06_target_lm_perplexity.csv");
        lcsv.row({"train_group", "train_chars", "ppl_full", "ce_bits_full",
                  "train_chars_trunc", "ppl_equal_len", "ce_bits_equal_len"});
        auto tchars = c.group_chars(o.target);
        size_t minlen = SIZE_MAX;
        for (auto& g : gnames) if (g != o.target) minlen = std::min(minlen, c.group_chars(g).size());
        const int V = (int)c.vocab_uni.size();
        for (auto& g : gnames) {
            if (g == o.target) continue;
            auto gc = c.group_chars(g);
            auto [ppl, ce] = BigramLM(gc, V).evaluate(tchars);
            std::vector<char32_t> trunc(gc.begin(), gc.begin() + std::min(gc.size(), minlen));
            auto [pplt, cet] = BigramLM(trunc, V).evaluate(tchars);
            lcsv.row({g, std::to_string(gc.size()), num(ppl, 2), num(ce),
                      std::to_string(trunc.size()), num(pplt, 2), num(cet)});
        }
    }

    // ---- console summary ----
    std::cerr << "corpus: " << c.docs.size() << " docs in " << gnames.size() << " groups | ";
    for (auto& g : gnames)
        std::cerr << g << "=" << c.groups[g].size() << "/" << c.group_chars(g).size() << "字 ";
    std::cerr << "\nvocab: uni=" << c.vocab_uni.size() << " bi=" << c.vocab_bi.size()
              << " func=" << c.func_words.size() << "\nwrote tables to " << o.out << "/\n";
    return 0;
}
