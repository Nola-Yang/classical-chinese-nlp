# classical-chinese-nlp

**Compiler and stylometry tooling for classical Chinese poetry.**

A small suite where my systems/compiler side meets a long-standing interest in
classical Chinese literature. The same skills that go into a language runtime or
a linter turn out to fit Tang/Song verse surprisingly well: a poem in a fixed
form *is* a formal grammar, and the question "who wrote this?" *is* a
classification problem. Everything here is built from scratch — no NLP
frameworks, no ML libraries.

| Subproject | What it is | Stack |
| :--- | :--- | :--- |
| **[`prosody/`](prosody)** | A **prosody linter / type-checker** for classical verse (诗 / 词). It parses a poem, looks up each character's *historical* tone (平/仄, including the 入声 that modern Mandarin lost) in an embedded 平水韵 table, and checks it against generated 近体诗 templates or 《钦定词谱》 patterns (818 词牌), emitting **compiler-style `line:col` diagnostics** plus rhyme checks. | C++20 · CMake |
| **[`stylometry/`](stylometry)** | A from-scratch **authorship-attribution engine** — Burrows's Delta, character-bigram LM perplexity, Jensen–Shannon / cosine distances, leave-one-out nearest-centroid — over function-word / single-char / bigram views. | C++17 · CMake |
| **[`manjianghong/`](manjianghong)** | The original **computational-stylometry study** the engine grew out of: a disputed-authorship investigation of 《满江红·怒发冲冠》. | Python (stdlib) |

## Highlights

- **The 入声 problem.** Classical 平/仄 can't be read off pinyin: the "entering
  tone" (入声) collapsed into modern Mandarin, so 白/月/一/竹 *sound* level but are
  *oblique* in verse. `prosody/` ships the historical rime table that gets this
  right, which is what lets it flag a mis-toned character the way a type-checker
  flags a type error. Every canonical fixture (满江红·怒发冲冠, 杜甫 春望/登高,
  李清照 如梦令…) validates to **0 violations**.
- **Validated stylometry.** The C++ engine reproduces the Python study's
  published numbers **to the digit** (function-word cosine, Burrows's Δ, LOO
  accuracy) — that agreement is its regression test.
- **Two real authorship controversies**, both answered honestly (including when
  the honest answer is "the data can't decide"):
  - *Did 岳飞 write 《满江红》?* — the verdict flips with the feature view; the
    method can't settle it, and says so. ([`manjianghong/`](manjianghong),
    [`stylometry/cases/manjianghong/`](stylometry/cases/manjianghong))
  - *Was the Qing "peasant woman poet" 贺双卿 real, or her male recorder's
    invention?* — her 词 are stylometrically indistinguishable from elite
    literati 词, but carry no detectable gender signature.
    ([`stylometry/cases/heshuangqing/`](stylometry/cases/heshuangqing))

## Build everything

Each subproject is self-contained. The C++ tools need only a C++17/20 compiler
and CMake; the Python study needs only Python 3.

```bash
# prosody linter
cmake -S prosody -B prosody/build && cmake --build prosody/build -j
ctest --test-dir prosody/build
./prosody/build/pingze --ci 满江红 prosody/examples/manjianghong-yuefei.txt

# stylometry engine + the 贺双卿 case
cmake -S stylometry -B stylometry/build && cmake --build stylometry/build -j
ctest --test-dir stylometry/build
./stylometry/build/stylo stylometry/cases/heshuangqing/corpus --target 贺双卿 --out out

# the original Python study
cd manjianghong && python3 compute_metrics.py
```

## A note on rigor

Across all three, the code **computes** and a human **interprets** — and the two
are kept visibly separate. Where the data is thin or a result is inconclusive,
that is stated plainly and quantified (leave-one-out accuracy, within-group
baselines), rather than dressed up as a finding. The literary texts are public
domain; data provenance is documented per subproject.

## License

MIT — see [`LICENSE`](LICENSE). Bundled rime/词谱 and corpus data are derived from
the third-party sources credited in each subproject's `NOTICE` / README.
