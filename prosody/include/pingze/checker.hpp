// The validator: align a poem against a metrical template and report, in
// compiler style, every place the tones or rhymes break.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "pingze/phonology.hpp"
#include "pingze/templates.hpp"
#include "pingze/unicode.hpp"

namespace pingze {

struct Diagnostic {
    enum Level { ERROR, WARNING, INFO };
    Level level = ERROR;
    int line = 0;
    int col = 0;
    char32_t cp = 0;       // the character at fault (0 if not character-specific)
    std::string message;
};

struct CheckResult {
    std::string form_label;       // the template we matched against
    int han_count = 0;            // characters scanned
    int template_len = 0;
    int prosody_violations = 0;   // hard 平仄 errors
    int rhyme_problems = 0;       // 出韵 etc.
    std::string rhyme_note;       // e.g. 词林正韵第十八部(入声)
    bool length_matched = true;
    std::vector<Diagnostic> diags;

    bool clean() const { return prosody_violations == 0 && rhyme_problems == 0; }
};

// Pull the Han characters (with source positions) out of decoded input.
std::vector<SourceChar> han_only(const std::vector<SourceChar>& chars);

// Check a 词 against a named 词牌. If format_index is set, force that 体;
// otherwise the best-fitting 体 is chosen automatically.
CheckResult check_ci(const Phonology& phon, const CipuTable& cipu,
                     const std::string& cipai, const std::string& text,
                     std::optional<int> format_index = std::nullopt);

// Check a 近体诗. If spec is set it is used verbatim; otherwise the form is
// auto-detected (line length, line count, 起式, 入韵) and the best fit is used.
CheckResult check_shi(const Phonology& phon, const std::string& text,
                      std::optional<ShiSpec> spec = std::nullopt);

}  // namespace pingze
