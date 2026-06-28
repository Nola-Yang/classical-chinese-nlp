# pingze · 平仄

**A prosody linter / type-checker for classical Chinese verse, in C++20.**

Think of it as a compiler front-end for poems. `pingze` reads a 诗 or 词, looks
up every character's *historical* tone, and checks it against the metrical
template of a regulated-verse form or a 词牌 — then prints the violations the way
a compiler reports type errors, with `line:column` and an explanation.

```text
$ pingze examples/broken-demo.txt
form:  五言绝句·平起首句不入韵
chars: 20 / template 20
broken-demo.txt:1:3: error: '隐' expected 平 (level), but this character is 仄 (oblique)
broken-demo.txt:1:11: error: '白' expected 平 (level), but this character is 仄 (入声 entering)
broken-demo.txt:2:11: error: '雪' expected 平 (level), but this character is 仄 (入声 entering)
broken-demo.txt:1:11: error: '白' rhyme word must be 平声 (level), but this character is 仄 (入声 entering)
broken-demo.txt:1:11: error: '白' off-rhyme (出韵): not in the same 平水韵 rhyme group as the other rhyme words
...
✗ 6 prosody violation(s), 4 rhyme problem(s)
```

```text
$ pingze --ci 满江红 examples/manjianghong-yuefei.txt
form:  满江红·柳永  「定格」
chars: 93 / template 93
rhyme: 词林正韵 第十八部(入声)
✓ conforms — 0 prosody violations, 0 rhyme problems
```

## Why this is harder than it looks: the 入声 problem

Classical Chinese prosody is binary at heart — every syllable is either **平**
(*píng*, "level") or **仄** (*zè*, "oblique") — and the rules say which slots
want which. The catch is that **you cannot read 平/仄 off modern pinyin.**

Middle Chinese had four tones; one of them, **入声** (*rùshēng*, the "entering
tone": syllables ending in a hard *-p/-t/-k* stop), counted as *oblique*. That
tone **disappeared from Mandarin**, its syllables scattering into the modern
first/second tones. So characters like 白 *bái*, 月 *yuè*, 一 *yī*, 竹 *zhú*,
雪 *xuě*, 发 *fā*, 节 *jié* sound perfectly *level* today, but are **仄** in
verse. Get this wrong and your scansion is silently broken — which is exactly
the mistake the `broken-demo` above makes.

Recovering 入声 needs a historical rime table, not a phonetic guess. `pingze`
ships one (the **平水韵** / *Píngshuǐ rimes*) and treats it as the source of
truth, which is what lets it flag `'白' ... is 仄 (入声 entering)`.

## What it checks

| | Against | Notes |
|---|---|---|
| **平仄** (tonal pattern) | 近体诗 templates (generated) · 词谱 (《钦定词谱》 + 龙榆生) | per-character, respecting 中 (either-allowed) slots |
| **押韵** (rhyme) | 平水韵 (诗) · 词林正韵 (词) | rhyme words must share a rhyme group; 近体诗 feet must be 平声 |
| **入声** | 平水韵 | the lost entering tone, the whole point above |
| **换韵** (rhyme change) | tone of each rhyme slot | rhyme runs are re-segmented when a 词 switches 平⇄仄 |
| **叠字 / 叠句** | 词谱 | e.g. 如梦令「知否，知否」, 声声慢「寻寻觅觅」 |
| heteronyms (多音字) | 平水韵 | a character with both readings (看, 望, 思…) satisfies either slot |

A 词 is validated against **all** recorded 体 (variants) of its 词牌 and reported
against the best-fitting one. A 近体诗's form (5/7-character, 绝/律, 起式, 入韵)
is auto-detected; `孤平` and `三平/三仄尾` are headed off because the generated
templates pin the guard positions.

## Build

No third-party dependencies — just a C++20 compiler and CMake.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build           # runs the test suite
./build/pingze --ci 满江红 examples/manjianghong-yuefei.txt
```

## Usage

```text
pingze --ci <词牌> [options] <file|->     # check as a 词
pingze --shi [form options] <file|->     # check as a 近体诗 (auto-detected)
pingze --list-ci [prefix]                # list the known 词牌
pingze --ci-formats <词牌>                # list a 词牌's 体 (variants)

  -e, --explain     print every character with its tone and rhyme groups
  --json            machine-readable output
  --format <n>      force the n-th 体 of a 词牌
  --yan/--lines/--ze-qi/--first-rhyme   pin a 诗 form instead of auto-detecting
  --data <dir>      override the data directory
```

`--explain` is handy on its own — it is a tone dictionary lookup:

```text
$ echo 国破山河在 | pingze -e -
国  仄  平水韵[R:一屋,R:十三职]  词林正韵[第十五部,第十七部]
破  仄  平水韵[Q:二十一个]      词林正韵[第九部]
山  平  平水韵[P:十五删]        词林正韵[第七部]
河  平  平水韵[P:五歌]          词林正韵[第九部]
在  仄  平水韵[S:十贿,Q:十一队]  词林正韵[第五部]
```

(`P/S/Q/R` tag each 平水韵 group as 平/上/去/入.)

The exit code is `0` when the poem conforms and `1` otherwise, so `pingze` drops
into scripts and CI like any other linter.

## How it works

```
UTF-8 bytes ─▶ codepoints (+line/col)  src/unicode.cpp
            ─▶ tone & rhyme lookup       src/phonology.cpp   (data/phonology.tsv)
            ─▶ metrical template         src/templates.cpp
                  · 词: parsed from 钦定词谱/龙榆生   (data/cipu.dat)
                  · 诗: generated from the 粘 / 对 rules
            ─▶ align & diagnose          src/checker.cpp
            ─▶ report (text / JSON)       src/main.cpp
```

The interesting bit is template *generation* for regulated verse. There are only
four base line shapes per line length; once you fix line 1's opening tone and
whether it rhymes, the **粘 (adhesion)** and **对 (opposition)** rules determine
every remaining line. `pingze` encodes the four shapes with 中 (either) at the
一三五不论 positions — except where freeing a position would create 孤平 or a
三平/三仄 tail — and assembles the whole poem from them.

For 词, the templates come straight from a digitised **《钦定词谱》** (the imperial
prosody manual) augmented with 龙榆生's *定格/变格*, so a 词牌 like 满江红 carries
**16 体** and the checker picks whichever your poem actually follows.

## Data & provenance

* `data/phonology.tsv` — 8,329 characters → tone class (平/上/去/入), 平水韵 rime
  group, and 词林正韵 部. Derived from the open
  [`charlesix59/chinese_word_rhyme`](https://github.com/charlesix59/chinese_word_rhyme)
  dataset (平水韵 / 词林正韵 / 钦定词谱).
* `data/cipu.dat` — 818 词牌 / 2,475 体, compacted from the same 钦定词谱 + 龙榆生
  source into one token per character (`tune` + structural marker).
* `data/corrections.tsv` — a small overlay of fixes found while validating
  against canonical rhyming poems (e.g. upstream was missing the level reading of
  先 and the character 樽). The loader merges it over the base table, so
  corrections stay auditable and separate from upstream data.

Every example in this README is checked by the test suite, and the famous
poems used as fixtures (满江红·怒发冲冠, 杜甫 春望/登高, 王之涣 登鹳雀楼,
李清照 如梦令) all return **0 violations** — the empirical sanity check that the
phonology table and templates are right.

## Limitations

* **拗救** (deliberate, "rescued" tonal substitutions) is not modelled; `pingze`
  judges against the standard templates, so a rescued line may show as a
  violation. This is noted rather than hidden.
* Heteronyms are handled leniently: any valid reading satisfies a slot, so a
  genuinely mis-toned 多音字 can slip through.
* The long tail of rare 词牌 inherits whatever noise is in the upstream 钦定词谱
  digitisation; the common 词牌 used here were checked by hand.

## Layout

```
include/pingze/   unicode · phonology · templates · checker  (public headers)
src/              the implementations + the CLI
data/             phonology.tsv · cipu.dat · corrections.tsv
examples/         annotated 诗 / 词, including a deliberately broken one
tests/            framework-free test runner (ctest)
```

## License

MIT (code) — see [`LICENSE`](LICENSE). The bundled rime/词谱 data is derived from
the third-party sources credited above; see [`NOTICE`](NOTICE).
