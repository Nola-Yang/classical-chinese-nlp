# Case B — 贺双卿: a peasant woman, or a male scholar's invention?

**The question.** 贺双卿 (He Shuangqing, c. 1713–1740) is celebrated as the
"first woman *ci* poet of the Qing." Yet everything we have of her — her life and
all her poems — survives through a single source: 史震林 (Shi Zhenlin)'s
《西青散记》(*Random Records of West-Green*). She supposedly was an unschooled
farm wife who wrote technically dazzling, allusion-laden 词 on leaves and bran.
Modern scholars (notably Paul S. Ropp, *Banished Immortal*, 2001; and others)
have asked the obvious question: **did she exist, or did 史震林 — a failed
examination scholar — invent her and write the poems himself?**

This case puts the question to a stylometer. It is a deliberately *modest* test,
and — spoiler — it does not (and cannot) settle the controversy. What it *can* do
is measure exactly where her verse sits in style space, and be honest about what
that does and doesn't show.

## Corpus

Built directly from 古诗文网 (texts parsed from raw HTML, not hand-typed; build
script and provenance in [`corpus/`](corpus)).

| group | n | chars | genre | role |
|---|--:|--:|---|---|
| **贺双卿** | 10 | 886 | 词 | the disputed poet (target) |
| 顾太清 | 10 | 752 | 词 | genuine Qing gentry **woman** poet |
| 吴藻 | 10 | 745 | 词 | genuine Qing gentry **woman** poet |
| 纳兰性德 | 10 | 475 | 词 | canonical Qing **male** literatus |
| 项鸿祚 | 10 | 725 | 词 | Qing **male** poet, melancholic register near 双卿's |
| 史震林 | 2 | 80 | **诗** | her alleged "author" — see caveat |

```bash
../../build/stylo cases/heshuangqing/corpus --target 贺双卿 --group-field 类别 --loo-min 5 --out out
```

## What the numbers say

**1. 双卿 sits *inside* the elite-词 cloud, not outside it.** Her nearest
neighbours, by every view, are other polished literati poets — and the single
closest one flips across the gender line depending on the metric:

| view | nearest to 贺双卿 | … then |
|---|---|---|
| function-word cosine | 吴藻 0.746 (woman) | 项鸿祚 0.741, 顾太清 0.741, 纳兰 0.674 |
| Burrows's Δ (smaller=closer) | 项鸿祚 0.304 (man) | 吴藻 0.325, 顾太清 0.362, 纳兰 0.411 |

There is no "she's a stylistic outlier" signal. Her 词 are stylometrically
**ordinary elite 词** — which is itself the heart of the skeptics' unease: this is
not what you would naively expect from an illiterate farm wife. But "reads like
literati verse" is *not* the same as "was written by 史震林."

**2. Gender does not organise this corpus.** The single most-similar pair of
poets in the whole set is **项鸿祚 (man) ↔ 顾太清 (woman)** — cosine 0.833,
Δ 0.230, closer than any same-gender pair. So the 男子作闺音 / woman-author axis
leaves no clean fingerprint at this sample size: you cannot read a poet's gender
off their function-word profile here, and 双卿 fitting "among the women" is no
stronger than her fitting "among the men."

**3. The 史震林 hypothesis is not testable as posed.** His own signed verse is
vanishingly sparse — only two 诗 are readily attested (and they are 诗, not 词, so
any comparison is genre-confounded). You cannot build a stylistic profile from
80 characters of the wrong genre. Notably, the *absence* of a comparable body of
史震林's own 词 cuts both ways: it removes the means to convict him, but it is
also (weakly) odd for someone alleged to have produced 双卿's polished 词 corpus.

**4. Method caveat (the most important line).** On ~10-poem corpora the method is
weak. Leave-one-out nearest-centroid accuracy *among the four control poets* is:

| view | LOO accuracy (4 classes; chance ≈ 0.25) |
|---|---|
| function words | **0.15** (below chance — cannot separate them) |
| single character | 0.48 |
| character bigram | 0.48 |

The character-bigram LM perplexities of 双卿 under each poet are essentially flat
(~1106–1118), i.e. near-uniform — the same weak-signal regime as the companion
满江红 study. So everything above is *suggestive at most*.

## Verdict

> Stylometry cannot confirm or refute the fabrication theory. What it shows
> cleanly is narrower and still interesting: **贺双卿's 词 are indistinguishable
> from genuine elite Qing 词 — male or female — and carry no gender signature the
> method can detect.** That is consistent with the skeptics' observation that her
> verse is "too literati" for its purported author, but it does not point a finger
> at 史震林, whose own corpus is too thin to test. A clean, well-characterised
> *inconclusive* — which, for a small-sample authorship question, is the honest
> result.

*Scholarship: Paul S. Ropp, "Banished Immortal: Searching for Shuangqing, China's
Peasant Woman Poet" (2001); Grace S. Fong on Ming–Qing women's writing; 杜芳琴,
《痛史明心录》. This repository takes no position beyond the measurements.*
