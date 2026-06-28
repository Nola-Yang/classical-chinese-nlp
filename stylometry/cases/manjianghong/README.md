# Case A — 满江红·怒发冲冠 (validation case)

This case exists to **validate the C++ engine** against an independent
implementation. The corpus and question come from the companion project
[`满江红作者考辨`](../../../manjianghong) (a pure-Python stylometry of the
disputed 《满江红·怒发冲冠》, traditionally attributed to 岳飞). Re-running that
corpus through this engine reproduces the Python project's published numbers,
which is the evidence that the port is correct.

## Reproduce

```bash
../../build/stylo corpus --target 待考 --group-field 类别 --loo-min 3 --out out
```

40 documents in 5 groups: `待考` (the disputed poem), `岳飞`, `王越` (the Ming
border-general some scholars credit), `对照` (a 苏轼 control), `发现存疑` (the
disputed 族谱 variants).

## Numbers reproduced (C++ here vs. the Python reference)

| metric | Python reference | this engine |
|---|---|---|
| function-word cosine, 待考 vs 王越 | 0.299 | **0.299** |
| function-word cosine, 待考 vs 苏轼 | 0.185 | **0.185** |
| function-word cosine, 待考 vs 岳飞 | 0.171 | **0.170** |
| Burrows's Δ, 待考 vs 王越 | 0.496 | **0.496** |
| Burrows's Δ, 待考 vs 岳飞 | 0.752 | **0.752** |
| LOO accuracy (岳飞 vs 王越), function words | 0.64 | **0.639** |
| LOO accuracy, single-character | 0.81 | **0.806** |

## The (deliberately inconclusive) finding

The result *flips with the feature view*, which is itself the point:

* **Abstract style (function words / Burrows's Δ)** leans toward 王越 — but the
  same view's LOO classifier only identifies 4/13 of 岳飞's own poems, so its
  "vote" is weak (the 23 near-identical 王越 七律 form an attractor).
* **Content-word collocation (bigrams), the bigram LM, and within-group
  position** lean toward 岳飞 / "compatible with 岳飞".

So the engine does not resolve the authorship — exactly as the Python study
concluded. A clean negative result, and a clean validation of the tool. The
*genuinely* anomalous signals (the 贺兰山 geography, the missing Song–Yuan
transmission) are historical, not stylometric, and live in the companion repo.
