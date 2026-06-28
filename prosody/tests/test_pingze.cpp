// Minimal self-contained test harness (no framework dependency).
#include <cstdio>
#include <string>

#include "pingze/checker.hpp"
#include "pingze/phonology.hpp"
#include "pingze/templates.hpp"

using namespace pingze;

static int g_failures = 0;
#define CHECK(cond, msg)                                              \
    do {                                                             \
        if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_failures; } \
        else std::printf("  ok:   %s\n", msg);                        \
    } while (0)

static const char* DATA = PINGZE_SOURCE_DATA_DIR;

int main() {
    Phonology phon = Phonology::load(std::string(DATA) + "/phonology.tsv",
                                     std::string(DATA) + "/corrections.tsv");
    CipuTable cipu = CipuTable::load(std::string(DATA) + "/cipu.dat");

    std::printf("[phonology]\n");
    auto is_ze = [&](char32_t c) { auto* i = phon.lookup(c); return i && i->can_ze(); };
    auto is_ping = [&](char32_t c) { auto* i = phon.lookup(c); return i && i->can_ping(); };
    CHECK(is_ze(U'月') && phon.lookup(U'月')->can_ru(), "月 is 仄 (入声)");
    CHECK(is_ze(U'白') && phon.lookup(U'白')->can_ru(), "白 is 仄 (入声)");
    CHECK(is_ze(U'一') && phon.lookup(U'一')->can_ru(), "一 is 仄 (入声)");
    CHECK(is_ping(U'天') && !is_ze(U'天'), "天 is purely 平");
    CHECK(phon.lookup(U'看')->ambiguous(), "看 is a heteronym (平/仄)");
    CHECK(is_ping(U'先'), "先 is 平 (corrections overlay applied)");

    std::printf("[词 满江红·怒发冲冠]\n");
    const std::string mjh =
        "怒发冲冠，凭栏处、潇潇雨歇。抬望眼，仰天长啸，壮怀激烈。"
        "三十功名尘与土，八千里路云和月。莫等闲、白了少年头，空悲切。"
        "靖康耻，犹未雪；臣子恨，何时灭！驾长车，踏破贺兰山缺。"
        "壮志饥餐胡虏肉，笑谈渴饮匈奴血。待从头、收拾旧山河，朝天阙。";
    CheckResult r1 = check_ci(phon, cipu, "满江红", mjh);
    std::printf("  -> %s | %d prosody, %d rhyme | %s\n", r1.form_label.c_str(),
                r1.prosody_violations, r1.rhyme_problems, r1.rhyme_note.c_str());
    CHECK(r1.han_count == 93, "怒发冲冠 has 93 characters");
    CHECK(r1.prosody_violations == 0, "怒发冲冠 has 0 prosody violations");
    CHECK(r1.rhyme_problems == 0, "怒发冲冠 has 0 rhyme problems");
    CHECK(r1.rhyme_note.find("第十八部") != std::string::npos, "rhymes in 词林正韵第十八部");
    CHECK(r1.rhyme_note.find("入声") != std::string::npos, "rhyme is 入声");

    std::printf("[诗 杜甫·春望 五律]\n");
    const std::string cw =
        "国破山河在，城春草木深。感时花溅泪，恨别鸟惊心。"
        "烽火连三月，家书抵万金。白头搔更短，浑欲不胜簪。";
    CheckResult r2 = check_shi(phon, cw);
    std::printf("  -> %s | %d prosody, %d rhyme | %s\n", r2.form_label.c_str(),
                r2.prosody_violations, r2.rhyme_problems, r2.rhyme_note.c_str());
    CHECK(r2.han_count == 40, "春望 has 40 characters");
    CHECK(r2.prosody_violations == 0, "春望 has 0 prosody violations");
    CHECK(r2.rhyme_problems == 0, "春望 rhymes cleanly");

    std::printf("[诗 杜甫·登高 七律, 首句入韵]\n");
    const std::string dg =
        "风急天高猿啸哀，渚清沙白鸟飞回。无边落木萧萧下，不尽长江滚滚来。"
        "万里悲秋常作客，百年多病独登台。艰难苦恨繁霜鬓，潦倒新停浊酒杯。";
    CheckResult r3 = check_shi(phon, dg);
    std::printf("  -> %s | %d prosody, %d rhyme | %s\n", r3.form_label.c_str(),
                r3.prosody_violations, r3.rhyme_problems, r3.rhyme_note.c_str());
    CHECK(r3.han_count == 56, "登高 has 56 characters");
    CHECK(r3.prosody_violations <= 1, "登高 scans (<=1 tolerated heteronym edge)");

    std::printf("[negative control: tones scrambled]\n");
    const std::string bad = "天天天天天，天天天天天。天天天天天，天天天天天。";
    CheckResult r4 = check_shi(phon, bad);
    std::printf("  -> %d prosody violations\n", r4.prosody_violations);
    CHECK(r4.prosody_violations > 0, "all-平 doggerel is flagged");

    std::printf("\n%s\n", g_failures ? "TESTS FAILED" : "ALL TESTS PASSED");
    return g_failures ? 1 : 0;
}
