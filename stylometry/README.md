# classical-stylometry · 计量文体学

**A from-scratch computational-stylometry / authorship-attribution engine for
classical Chinese, in C++17 — with two real authorship case studies.**

Given a corpus of poems labelled by author, it answers *"whose style does this
text most resemble?"* with the standard quantitative toolkit: lexical features,
distributional distances, **Burrows's Delta**, a character-bigram language model,
and leave-one-out nearest-centroid classification. No third-party libraries.

> **Design rule:** the program **computes**, it does not **conclude**. It emits a
> set of reviewable CSV tables; interpretation lives in each case study's
> write-up, clearly separated from the math. (Inherited from the companion
> Python project this engine re-implements and validates against.)

## Why a C++ rewrite

This started as a [pure-Python stylometry](../manjianghong) of the
disputed 《满江红》. Rewriting the engine in C++ made it (a) fast and reusable on
any corpus, (b) a clean library with a CLI, and (c) — by reproducing the Python
project's published numbers **to the digit** — its own regression test. UTF-8 is
decoded to codepoints by hand; everything downstream is index arithmetic.

## What it computes

| group | metrics |
|---|---|
| per-document | char/type counts, TTR, hapax ratio, Shannon entropy, Yule's K, Simpson's D, mean clause length, function-word rate |
| distributional | cosine · Jensen–Shannon divergence · KL · Manhattan · Euclidean (over function-word / single-char / bigram views) |
| authorship | **Burrows's Delta** (z-scored function-word profile); character-bigram **LM perplexity / cross-entropy** (with equal-length truncation); **leave-one-out** nearest-centroid accuracy + confusion |

The bias toward *function words* and *character bigrams* is deliberate: they are
topic-independent, so they fingerprint an author's habits rather than a poem's
subject.

## Build & run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build                       # unit tests for the primitives

./build/stylo cases/heshuangqing/corpus \
    --target 贺双卿 --group-field 类别 --loo-min 5 --out out
```

The corpus is a directory of `.txt` files; one file may hold many poems separated
by `~~~~~`, each with a small header (`# 作者:`, `# 类别:`, …) and a `=====`
before its body. `--group-field` chooses which header becomes the author label;
`--target` marks the group whose attribution you care about.

### Going further — `stylo-advanced`

When the core methods leave a case unattributable, `stylo-advanced` adds a
stronger and more orthogonal battery (kept in a separate binary so the validated
`stylo` numbers never move): **top-N most-frequent-character** features,
**Cosine Delta**, **tf-idf**-weighted cosine, **1-nearest-neighbour** LOO,
**prosodic** features (平/仄/入声 rates + tonal-contour bigrams, reusing the
prosody subproject's 平水韵 table), and a **label-permutation test** that asks
whether a "nearest author" result is real or could arise from random labelling.

```bash
cmake --build build           # also builds ./build/stylo-advanced
./build/stylo-advanced cases/heshuangqing/corpus --target 贺双卿 \
    --phonology ../prosody/data/phonology.tsv --mfw 100 --perm 5000 --out out_adv
```

On the 贺双卿 case this confirms the negative result is robust (every method's
permutation p > 0.18), while showing tf-idf is the most discriminative view and
that her lexis leans, weakly, toward a *male* poet of her register. See each
case's `out_advanced/` tables and write-up.

## Case studies

### A. 满江红·怒发冲冠 — *validation* ([`cases/manjianghong`](cases/manjianghong))

Re-running the companion project's 40-text corpus reproduces its published
numbers exactly, which is how I know the port is correct:

| metric | Python reference | this engine |
|---|---|---|
| function-word cosine, 待考↔王越 | 0.299 | **0.299** |
| Burrows's Δ, 待考↔王越 / ↔岳飞 | 0.496 / 0.752 | **0.496 / 0.752** |
| LOO accuracy (岳飞 vs 王越), func / uni | 0.64 / 0.81 | **0.64 / 0.81** |

The finding is itself a *clean negative*: the verdict flips with the feature view,
so stylometry cannot settle the authorship — exactly as the original study found.

### B. 贺双卿 — peasant poet, or a male scholar's invention? ([`cases/heshuangqing`](cases/heshuangqing))

Did the "first woman *ci* poet of the Qing" exist, or did 史震林 — the only source
for her life and work — invent her? Against genuine Qing women poets (顾太清, 吴藻)
and male literati (纳兰性德, 项鸿祚):

* 双卿's 词 sit **inside** the elite-词 cloud, not as an outlier — nearest
  neighbour is 吴藻 (woman, cosine 0.746) by one view and 项鸿祚 (man, Δ 0.304) by
  another: **tied across the gender line.**
* The corpus **isn't organised by gender** at all — the most-similar pair is
  项鸿祚 (man) ↔ 顾太清 (woman). So 男子作闺音 vs woman-author leaves no detectable
  fingerprint here.
* The 史震林 theory is **not testable as posed**: only two of his 诗 are readily
  attested (wrong genre, 80 chars).
* And the honest caveat: function-word LOO among the controls is **0.15** (below
  4-class chance) — small samples, weak signals.

> Verdict: stylometry can't convict or acquit 史震林, but it shows 双卿's verse is
> indistinguishable from polished literati 词 — consistent with the skeptics'
> unease, short of proof. A well-characterised *inconclusive*.

## Validation & tests

`ctest` checks the primitives (cosine/JSD bounds, feature counts, that the LM
prefers in-style text, that Delta ranks a stylistic twin above a different
style). The 满江红 case is the integration test: byte-for-byte agreement with an
independent Python implementation.

## Limitations

* **Small samples.** Classical poets leave little text; ~10 poems/author gives
  low statistical power. The engine quantifies this honestly (LOO accuracy,
  near-uniform LM perplexity) rather than hiding it.
* **Genre confounds authorship.** Comparing 词 against 诗 mixes form with voice;
  case write-ups flag this wherever it applies.
* Character-bigram LMs over a few hundred characters approach a uniform
  distribution — their rankings are weak signals, used only as corroboration.

## Layout

```
include/stylo/   the public API (one header)
src/             text (UTF-8/parsing) · metrics (features/distances/Delta/LM) · main (CLI)
data/            function_words.txt (the stylistic-fingerprint feature set)
cases/           manjianghong (validation) · heshuangqing (the new study)
tests/           framework-free primitive tests (ctest)
```

## License

MIT (code) — see [`LICENSE`](LICENSE). Corpus texts are parsed from 古诗文网 and
credited per case; classical poems are public domain.
