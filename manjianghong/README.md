# Who wrote 《满江红·怒发冲冠》? — a computational-stylometry investigation

*[中文版 / Chinese version →](README.zh.md)*

《满江红·怒发冲冠》 ("Wrath bristles my helmet…") is one of the most famous
patriotic poems in Chinese — traditionally attributed to the Song general **岳飞
(Yue Fei, 1103–1142)**. But the attribution is disputed: the poem has **no Song
or Yuan record** and first surfaces only in the **Ming**, around 1502/1536. Some
scholars (夏承焘, 1962) argue it was written by a **Ming** border-general,
**王越 (Wang Yue, 1426–1499)**, who actually campaigned at 贺兰山 — a place the
poem names but that lay outside Yue Fei's theatre of war.

This project asks what **quantitative stylometry** can — and cannot — say about
the authorship, and is scrupulous about the difference.

> **Core principle.** The code (`compute_metrics.py`) **only computes numbers**;
> it writes a batch of reviewable CSVs under `数据/`. Every interpretation is done
> by a human, explicitly labelled as interpretation (§5 below). Yue Fei's reliably
> transmitted corpus is tiny and genre-mixed, so the study also **quantifies its
> own reliability** (leave-one-out accuracy, within-group baselines) instead of
> trusting a single number.

## The corpus (40 texts)

| group | texts | chars | what it is |
|---|--:|--:|---|
| **disputed** | 1 | 93 | 满江红·怒发冲冠 |
| **岳飞 Yue Fei** | 13 | 848 | his reliable 词 / 诗 / 文 |
| **王越 Wang Yue** | 23 | 1240 | the Ming border-general some credit as the real author |
| **control** | 1 | 93 | a 苏轼 满江红 (same tune, different author) |
| **suspect variants** | 2 | 186 | the 族谱 ("clan genealogy") versions, themselves contested |

## Method

`compute_metrics.py` (pure standard library, no dependencies) computes, per text
and per group: lexical features (TTR, hapax ratio, Shannon entropy, Yule's K,
Simpson's D, function-word rate, 入声-rhyme rate); distributional distances
(cosine / Jensen–Shannon / KL / Manhattan over function-word, single-char, and
bigram views); **Burrows's Delta**; a **character-bigram language-model
perplexity** (with equal-length truncation to control for corpus size); full
distance matrices; **nearest-centroid leave-one-out (LOO) accuracy** (can the
method even tell 岳飞 from 王越 on *known* poems?); within-group baselines; and a
motif/geography/enemy-term count matrix.

```bash
python3 compute_metrics.py     # reads 语料/ → writes 数据/*.csv + metrics_summary.json
```

## What the data shows (interpretation — not produced by the code)

**The verdict flips with the feature view — and that is the most important
result.**

* **Abstract style (function words / Burrows's Δ) → leans 王越.** Function-word
  cosine: 王越 0.299 > 苏轼 0.185 > 岳飞 0.171; Δ 王越 0.496 < 岳飞 0.752. **But**
  the same view's LOO classifier is only 0.64 accurate and correctly identifies
  just 4/13 of Yue Fei's *own* poems — the 23 near-identical 王越 七律 act as an
  attractor that pulls everything toward them. So this "vote for 王越" is weak.
* **Content collocation (bigrams) / LM / within-group position → leans 岳飞.**
  Bigram containment with 岳飞 ≈ 4× that with 王越; the disputed poem falls
  *inside* Yue Fei's single-char/bigram cloud (z = +0.42 / +0.26) but *outside*
  王越's; the bigram-view LOO (which identifies 11/13 of 岳飞's poems) assigns the
  poem to 岳飞.
* **The old "anachronistic diction" objection is refuted by the data.** The
  generic enemy-terms (胡虏/匈奴/夷狄) the poem uses are exactly Yue Fei's own
  habit (his 五岳祠盟记 alone uses them 9×).
* **The one robust anomaly is geographic.** The poem points **northwest (贺兰山)**;
  every geographically-anchored Yue Fei work points **northeast / central plains**
  (his actual front against the Jin). This is a *historical*, not stylometric,
  problem — and the basis for the Wang-Yue theory.

## Conclusion

> **Stylometry cannot decide the authorship.** Swap the feature view and the
> answer flips between 岳飞 and 王越, while the method's own LOO accuracy (0.64–0.81,
> and poor at recognising Yue Fei's own poems) means no single verdict is
> trustworthy. Where signal does appear — content collocation, LM fit, cloud
> position, motif/enemy habits — it leans **"compatible with 岳飞."** But
> *compatible ≠ proven*. Computational methods dispel the "anachronistic diction"
> objection, yet cannot touch the two real doubts: the **northwest 贺兰山
> geography** and the **missing Song–Yuan transmission**. Those are questions of
> historical philology, and remain open.

## Limitations

Tiny samples (Yue Fei: 848 chars, only 2 词); **genre confound** (the disputed
text is a 词; Yue Fei's corpus is mostly 诗/文 and Wang Yue's is all 诗), so part of
the distance is form, not authorship; the character-bigram LM is near-uniform at
this data size; the geography/motif dictionaries are hand-built feature
engineering (the code only *counts*, it doesn't judge).

## Related

The metrics here were later re-implemented (and validated to the digit) in a
reusable C++ engine, which also tackles a second authorship controversy (贺双卿):
**[classical-stylometry](../stylometry)**.

## Scholarship

Doubt: 余嘉锡《四库提要辨证》(1937, transmission gap); 夏承焘 (1962, the 贺兰山 →
Wang Yue argument). Defence: 邓广铭 (贺兰山 as a generic literary allusion); the
1986 族谱 version (itself contested). This project takes **no prior position** —
it presents computable data and its limits.
