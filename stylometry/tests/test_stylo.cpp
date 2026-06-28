// Framework-free correctness checks for the stylometry primitives.
#include <cmath>
#include <cstdio>
#include <vector>

#include "stylo/stylo.hpp"

using namespace stylo;

static int fails = 0;
#define CHECK(cond, msg)                                                  \
    do {                                                                  \
        if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++fails; }       \
        else std::printf("  ok:   %s\n", msg);                            \
    } while (0)

static bool close(double a, double b, double eps = 1e-9) { return std::fabs(a - b) < eps; }

int main() {
    std::printf("[distances]\n");
    std::vector<double> a = {0.2, 0.3, 0.5}, b = {0.2, 0.3, 0.5}, d = {0.5, 0.3, 0.2};
    CHECK(close(cosine(a, b), 1.0), "cosine of identical vectors == 1");
    CHECK(close(jsd(a, b), 0.0), "JSD of identical distributions == 0");
    CHECK(jsd(a, d) > 0, "JSD of different distributions > 0");
    CHECK(close(manhattan(a, b), 0.0), "manhattan of identical == 0");
    CHECK(jsd(a, d) <= 1.0 + 1e-12, "JSD is bounded by 1 bit");

    std::printf("[features]\n");
    Document doc;
    doc.body = "之乎者也，之乎者也。";
    for (char32_t cp : decode_utf8(doc.body)) if (is_cjk(cp)) doc.chars.push_back(cp);
    Features f = compute_features(doc, default_function_words());
    CHECK(f.n_chars == 8, "char count excludes punctuation");
    CHECK(f.n_types == 4, "type count = 4 distinct chars");
    CHECK(close(f.ttr, 0.5), "TTR = 4/8");
    CHECK(f.func_word_rate > 0.99, "all-function-word text has rate ~1");
    CHECK(f.n_clauses == 2, "two clauses split on ，。");

    std::printf("[bigram LM]\n");
    std::vector<char32_t> train = doc.chars, train2;
    for (int i = 0; i < 20; ++i) train2.insert(train2.end(), train.begin(), train.end());
    auto [ppl_self, ce_self] = BigramLM(train2, 10).evaluate(doc.chars);
    std::vector<char32_t> alien;
    for (char32_t cp : decode_utf8("山河草木风云日月")) alien.push_back(cp);
    auto [ppl_alien, ce_alien] = BigramLM(train2, 10).evaluate(alien);
    std::printf("  self ppl=%.2f  alien ppl=%.2f\n", ppl_self, ppl_alien);
    CHECK(ppl_self < ppl_alien, "model predicts in-style text better than alien text");

    std::printf("[Burrows delta]\n");
    Document e1, e2, e3;
    auto fill = [](Document& x, const char* s) {
        x.body = s;
        for (char32_t cp : decode_utf8(s)) if (is_cjk(cp)) x.chars.push_back(cp);
    };
    fill(e1, "之乎者也之乎者也之乎");
    fill(e2, "之乎者也之乎者也之乎");
    fill(e3, "山河日月山河日月山河");
    std::vector<Document> corp = {e1, e2, e3};
    DeltaModel dm = fit_delta(corp, default_function_words());
    auto z1 = dm.zscore(relfreq(char_counts(e1.chars), default_function_words()));
    auto z2 = dm.zscore(relfreq(char_counts(e2.chars), default_function_words()));
    auto z3 = dm.zscore(relfreq(char_counts(e3.chars), default_function_words()));
    CHECK(delta(dm, z1, z2) < delta(dm, z1, z3),
          "delta(twin, twin) < delta(twin, different style)");

    std::printf("\n%s\n", fails ? "TESTS FAILED" : "ALL TESTS PASSED");
    return fails ? 1 : 0;
}
