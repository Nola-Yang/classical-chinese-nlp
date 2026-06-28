// A from-scratch computational-stylometry / authorship-attribution engine for
// classical Chinese. Header-light, standard-library-only.
//
// The unit of analysis is a "document" (one poem / piece). Documents carry a
// group label (usually an author); the engine answers questions of the form
// "whose style does this document/group most resemble?" using lexical features,
// distributional distances, Burrows's Delta, a character-bigram language model,
// and leave-one-out nearest-centroid classification.
#pragma once

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace stylo {

// ---- text ------------------------------------------------------------------

// Decode UTF-8 to Unicode codepoints (Han characters become one element each).
std::vector<char32_t> decode_utf8(const std::string& bytes);
std::string encode_utf8(char32_t cp);
bool is_cjk(char32_t cp);
bool is_punct(char32_t cp);          // CJK + ASCII punctuation / whitespace
bool is_strong_stop(char32_t cp);    // 。！？; sentence-final

// One piece of writing.
struct Document {
    std::string file;
    std::string title;
    std::string author;
    std::string group;          // the label we attribute by
    std::string genre;
    std::string body;           // normalized text, punctuation kept
    std::vector<char32_t> chars;   // CJK characters only, in order
    int n() const { return static_cast<int>(chars.size()); }
};

// Parse a corpus directory. Files may hold several documents separated by a line
// of ~~~~~ ; each document's header lines ("# key: value") precede a ===== line,
// then the body. group_field picks which header becomes the group label (with
// fallbacks). Throws std::runtime_error if the directory can't be read.
std::vector<Document> load_corpus(const std::string& dir,
                                  const std::string& group_field = "类别");

// ---- counting primitives ---------------------------------------------------

using Counter = std::unordered_map<char32_t, long>;
using StrCounter = std::unordered_map<std::string, long>;

Counter char_counts(const std::vector<char32_t>& chars);
StrCounter bigram_counts(const std::vector<char32_t>& chars);

// Relative-frequency vector over a fixed vocabulary.
std::vector<double> relfreq(const Counter& c, const std::vector<char32_t>& vocab);
std::vector<double> relfreq(const StrCounter& c, const std::vector<std::string>& vocab);

// ---- per-document lexical features -----------------------------------------

struct Features {
    int n_chars = 0;
    int n_types = 0;
    double ttr = 0;            // type/token ratio
    double hapax_ratio = 0;    // share of once-only types
    double shannon_bits = 0;
    double yule_k = 0;
    double simpson_d = 0;
    double mean_clause_len = 0;
    int n_clauses = 0;
    double func_word_rate = 0;
};

Features compute_features(const Document& d, const std::vector<char32_t>& func_words);

// ---- distances / similarities ----------------------------------------------

double cosine(const std::vector<double>& a, const std::vector<double>& b);
double manhattan(const std::vector<double>& a, const std::vector<double>& b);
double euclid(const std::vector<double>& a, const std::vector<double>& b);
double jsd(const std::vector<double>& p, const std::vector<double>& q);  // Jensen-Shannon, bits
double kl_smooth(const std::vector<double>& p, const std::vector<double>& q, double eps = 1e-9);

template <typename SetA, typename SetB>
double jaccard(const SetA& a, const SetB& b);   // defined in header below

// ---- Burrows's Delta -------------------------------------------------------
//
// z-standardize each function word's relative frequency across the whole corpus,
// then take the mean absolute difference of z-scores. Smaller = stylistically
// closer.
struct DeltaModel {
    std::vector<char32_t> vocab;        // function words actually present
    std::vector<double> mean, sd;
    std::vector<double> zscore(const std::vector<double>& relfreq_vec) const;
};
DeltaModel fit_delta(const std::vector<Document>& docs,
                     const std::vector<char32_t>& func_words);
double delta(const DeltaModel& m, const std::vector<double>& za,
             const std::vector<double>& zb);

// ---- character-bigram language model ---------------------------------------
//
// Add-k smoothed P(c | prev). Used to score how well an author's corpus
// "predicts" another text (perplexity / cross-entropy in bits).
class BigramLM {
public:
    BigramLM(const std::vector<char32_t>& train, int vocab_size, double k = 0.5);
    // Returns {perplexity, cross_entropy_bits}.
    std::pair<double, double> evaluate(const std::vector<char32_t>& test) const;
private:
    std::map<std::pair<char32_t, char32_t>, long> bi_;
    std::map<char32_t, long> uni_;
    int V_;
    double k_;
};

// ---- analyses --------------------------------------------------------------

struct Corpus {
    std::vector<Document> docs;
    std::vector<char32_t> func_words;
    std::vector<char32_t> vocab_uni;
    std::vector<std::string> vocab_bi;
    std::map<std::string, std::vector<int>> groups;  // group -> doc indices

    static Corpus build(std::vector<Document> docs, std::vector<char32_t> func_words);
    std::vector<char32_t> group_chars(const std::string& g) const;
};

// Default 文言 function-word list (used if no data file is supplied).
std::vector<char32_t> default_function_words();

// Load function words from a file (all CJK characters in it), or the default.
std::vector<char32_t> load_function_words(const std::string& path);

// ---- template impl ---------------------------------------------------------
template <typename SetA, typename SetB>
double jaccard(const SetA& a, const SetB& b) {
    if (a.empty() && b.empty()) return 0.0;
    long inter = 0;
    for (const auto& x : a) if (b.count(x)) ++inter;
    const long uni = static_cast<long>(a.size() + b.size()) - inter;
    return uni ? static_cast<double>(inter) / uni : 0.0;
}

}  // namespace stylo
