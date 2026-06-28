# Supervised ML for the 贺双卿 question

`ml_attribution.py` — a **multinomial Naive Bayes** and an **L2-regularized
logistic regression**, both written from scratch in pure Python (no numpy /
scikit-learn), evaluated with **leave-one-out cross-validation**.

Why LOO-CV and not accuracy on the training set: with ~10 poems per author a
classifier can simply memorize, so train accuracy is meaningless. Every number
below is cross-validated. Features are character unigrams + bigrams (top 200 by
frequency); NB uses raw counts, LR uses tf-idf with idf fit on the training fold
only (no leakage).

```bash
python3 ml/ml_attribution.py            # ~4 s, stdlib only
```

## The twist: the classifiers *work* — but still can't place 双卿

The distance methods couldn't even separate the four control poets (function-word
LOO = 0.15). A supervised classifier does much better, because it can *learn*
which features discriminate:

### Task 1 — gender (women 顾太清·吴藻 vs men 纳兰·项鸿祚)

| classifier | LOO-CV accuracy | majority baseline |
|---|--:|--:|
| Naive Bayes | **0.725** | 0.50 |
| Logistic regression (tf-idf) | 0.625 | 0.50 |

So there *is* a learnable gender signal in these poets' 词 (NB well above chance)
— interesting in itself for the 男子作闺音 question. **But applied to 贺双卿, the
working classifier sits on the fence:**

* Naive Bayes calls **5/10** of her poems "woman", 5/10 "man" — a dead split, and
  its per-poem probabilities are wildly polarized (0.00 … 1.00), a sign it is
  latching onto topic words as much as gender.
* Logistic regression (regularized, the more trustworthy model here) returns
  **P(woman) ≈ 0.42–0.55 for every poem** — i.e. it declines to decide.

### Task 2 — author (4-way)

| classifier | LOO-CV accuracy | random baseline |
|---|--:|--:|
| Naive Bayes | **0.575** | 0.25 |

Authors are clearly learnable (0.575 ≫ 0.25). 双卿's 10 poems are then assigned
**吴藻 ×4 (woman), 项鸿祚 ×4 (man), 纳兰 ×2** — again split across the gender line.
Mean class probability: 吴藻 0.44 > 项鸿祚 0.38 > 纳兰 0.17 > 顾太清 0.02.

## What this adds

ML sharpens — but does not overturn — the earlier conclusion. Where the distance
and permutation tests said "the methods are too weak to separate anyone," the
classifiers show the controls *are* separable (NB: author 0.58, gender 0.73); yet
those same working classifiers put 贺双卿 **right on the decision boundary** — a
50/50 gender split and an author vote tied between a woman (吴藻) and a man
(项鸿祚). The regularized model's verdict is, literally, P ≈ 0.5.

So across distance metrics, a permutation test, and now cross-validated
classifiers, the answer converges: **贺双卿's verse is genuinely in-between —
indistinguishable elite literati 词, attributable to no one in particular, and
carrying no decisive gender signature.** Caveats remain: n = 40 training docs
gives wide confidence intervals (gender 0.725 ≈ ±0.14), and NB's topic-sensitivity
makes the regularized LR the more reliable read.

Full run: [`out/results.txt`](out/results.txt).
