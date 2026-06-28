#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ml_attribution.py — supervised ML for the 贺双卿 authorship question.

Pure standard library (no numpy / scikit-learn): a multinomial Naive Bayes and an
L2-regularized logistic regression, both written from scratch, evaluated with
LEAVE-ONE-OUT cross-validation. With ~10 poems per author, train accuracy is
meaningless (a classifier can memorize), so every number reported is LOO-CV.

Two tasks:
  1. gender   — train on women (顾太清, 吴藻) vs men (纳兰性德, 项鸿祚); CV the
                classifier, then predict 贺双卿 (the 男子作闺音 question).
  2. author   — 4-way author classification; CV, then 贺双卿's author distribution.

Features: character unigrams + bigrams, top-K by corpus frequency. NB uses raw
counts; LR uses tf-idf (idf fit on the training fold only, to avoid leakage).
"""
import os, re, math, sys
from collections import Counter, defaultdict

CORPUS = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(__file__), "..", "cases", "heshuangqing", "corpus")
TOPK = 200

WOMEN = {"顾太清", "吴藻"}
MEN = {"纳兰性德", "项鸿祚"}
TARGET = "贺双卿"
EXCLUDE = {"史震林"}  # only 2 诗 — too few / wrong genre to train on

# ---- corpus loading (same format as the C++ engine) ------------------------
def load(corpus_dir):
    docs = []
    for fn in sorted(os.listdir(corpus_dir)):
        if not fn.endswith(".txt"):
            continue
        raw = open(os.path.join(corpus_dir, fn), encoding="utf-8").read()
        for chunk in re.split(r"(?m)^~~~~~+\s*$", raw):
            if "=====" not in chunk:
                continue
            head, body = chunk.split("=====", 1)
            meta = dict(re.findall(r"#\s*([^:：]+)[:：]\s*(.+)", head))
            group = (meta.get("类别") or meta.get("作者") or "?").strip()
            title = (meta.get("标题") or "?").strip()
            chars = [c for c in body if "一" <= c <= "鿿"]
            if chars:
                docs.append({"group": group, "title": title, "chars": chars})
    return docs

def features(chars):
    f = Counter(chars)
    for i in range(len(chars) - 1):
        f[chars[i] + chars[i + 1]] += 1
    return f

# ---- multinomial Naive Bayes ----------------------------------------------
class NB:
    def __init__(self, alpha=0.5):
        self.alpha = alpha
    def fit(self, X, y, vocab):
        self.vocab = vocab
        self.classes = sorted(set(y))
        self.logprior = {}
        self.loglik = {c: {} for c in self.classes}
        n = len(y)
        for c in self.classes:
            idx = [i for i in range(n) if y[i] == c]
            self.logprior[c] = math.log(len(idx) / n)
            tot = Counter()
            for i in idx:
                tot.update(X[i])
            denom = sum(tot[w] for w in vocab) + self.alpha * len(vocab)
            self.loglik[c] = {w: math.log((tot.get(w, 0) + self.alpha) / denom) for w in vocab}
        return self
    def scores(self, x):
        s = {}
        for c in self.classes:
            ll = self.loglik[c]
            s[c] = self.logprior[c] + sum(cnt * ll[w] for w, cnt in x.items() if w in ll)
        return s
    def predict(self, x):
        return max(self.scores(x).items(), key=lambda kv: kv[1])[0]
    def proba(self, x):  # softmax over class scores
        s = self.scores(x); m = max(s.values())
        e = {c: math.exp(v - m) for c, v in s.items()}; z = sum(e.values())
        return {c: e[c] / z for c in e}

# ---- L2-regularized logistic regression (binary), from scratch -------------
def tfidf_matrix(docs_feats, vocab, idf):
    rows = []
    for f in docs_feats:
        v = [f.get(w, 0) * idf[w] for w in vocab]
        norm = math.sqrt(sum(x * x for x in v)) or 1.0
        rows.append([x / norm for x in v])
    return rows

def fit_idf(train_feats, vocab):
    N = len(train_feats)
    df = {w: 0 for w in vocab}
    for f in train_feats:
        for w in vocab:
            if w in f:
                df[w] += 1
    return {w: math.log((N + 1) / (df[w] + 1)) + 1.0 for w in vocab}

class LogReg:
    def __init__(self, lr=0.5, l2=0.01, iters=200):
        self.lr, self.l2, self.iters = lr, l2, iters
    def fit(self, X, y):  # X: list[list[float]], y in {0,1}
        n, d = len(X), len(X[0])
        self.w = [0.0] * d; self.b = 0.0
        for _ in range(self.iters):
            gw = [0.0] * d; gb = 0.0
            for i in range(n):
                z = self.b + sum(self.w[j] * X[i][j] for j in range(d))
                p = 1.0 / (1.0 + math.exp(-z))
                err = p - y[i]
                gb += err
                xi = X[i]
                for j in range(d):
                    gw[j] += err * xi[j]
            for j in range(d):
                self.w[j] -= self.lr * (gw[j] / n + self.l2 * self.w[j])
            self.b -= self.lr * gb / n
        return self
    def proba(self, x):
        z = self.b + sum(self.w[j] * x[j] for j in range(len(x)))
        return 1.0 / (1.0 + math.exp(-z))

# ---- build data ------------------------------------------------------------
docs = load(CORPUS)
docs = [d for d in docs if d["group"] not in EXCLUDE]
for d in docs:
    d["feat"] = features(d["chars"])
vocab_freq = Counter()
for d in docs:
    vocab_freq.update(d["feat"])
VOCAB = [w for w, _ in vocab_freq.most_common(TOPK)]

ctrl = [d for d in docs if d["group"] != TARGET]
tgt = [d for d in docs if d["group"] == TARGET]
print(f"corpus: {len(docs)} docs | controls={len(ctrl)} target({TARGET})={len(tgt)} | "
      f"vocab={len(VOCAB)} (uni+bi, top {TOPK})\n")

def loo_nb(items, label_of):
    correct = 0
    for i in range(len(items)):
        tr = [items[j] for j in range(len(items)) if j != i]
        Xtr = [d["feat"] for d in tr]; ytr = [label_of(d) for d in tr]
        nb = NB().fit(Xtr, ytr, VOCAB)
        correct += (nb.predict(items[i]["feat"]) == label_of(items[i]))
    return correct / len(items)

def loo_lr(items, label_of):
    correct = 0
    for i in range(len(items)):
        tr = [items[j] for j in range(len(items)) if j != i]
        idf = fit_idf([d["feat"] for d in tr], VOCAB)
        Xtr = tfidf_matrix([d["feat"] for d in tr], VOCAB, idf)
        ytr = [label_of(d) for d in tr]
        lr = LogReg().fit(Xtr, ytr)
        xt = tfidf_matrix([items[i]["feat"]], VOCAB, idf)[0]
        pred = 1 if lr.proba(xt) >= 0.5 else 0
        correct += (pred == label_of(items[i]))
    return correct / len(items)

# ===== Task 1: gender (women vs men) =====
gen = [d for d in ctrl if d["group"] in WOMEN | MEN]
gender_label = lambda d: 1 if d["group"] in WOMEN else 0  # 1 = woman
nw = sum(1 for d in gen if gender_label(d) == 1)
baseline = max(nw, len(gen) - nw) / len(gen)
print("=== Task 1: gender classifier (1=woman, 0=man) ===")
print(f"  training docs: {len(gen)} ({nw} woman / {len(gen)-nw} man) | majority baseline = {baseline:.2f}")
print(f"  Naive Bayes      LOO-CV accuracy = {loo_nb(gen, gender_label):.3f}")
print(f"  LogReg (tf-idf)  LOO-CV accuracy = {loo_lr(gen, gender_label):.3f}")

# predict 双卿 with full-data models
idf = fit_idf([d["feat"] for d in gen], VOCAB)
Xtr = tfidf_matrix([d["feat"] for d in gen], VOCAB, idf)
lr = LogReg().fit(Xtr, [gender_label(d) for d in gen])
nb = NB().fit([d["feat"] for d in gen], [gender_label(d) for d in gen], VOCAB)
print(f"\n  贺双卿 predictions (P = P(woman)):")
nb_w = lr_w = 0
for d in tgt:
    pnb = nb.proba(d["feat"]).get(1, 0.0)
    plr = lr.proba(tfidf_matrix([d["feat"]], VOCAB, idf)[0])
    nb_w += pnb >= 0.5; lr_w += plr >= 0.5
    print(f"    {d['title'][:20]:22s}  NB P(woman)={pnb:.2f}  LR P(woman)={plr:.2f}")
print(f"  → NB: {nb_w}/{len(tgt)} poems called 'woman';  LR: {lr_w}/{len(tgt)} 'woman'")

# ===== Task 2: author (multi-class) =====
authors = sorted({d["group"] for d in ctrl})
author_label = lambda d: d["group"]
print(f"\n=== Task 2: author classifier ({len(authors)} classes: {', '.join(authors)}) ===")
print(f"  random baseline = {1/len(authors):.2f}")
print(f"  Naive Bayes  LOO-CV accuracy = {loo_nb(ctrl, author_label):.3f}")
nb_a = NB().fit([d["feat"] for d in ctrl], [author_label(d) for d in ctrl], VOCAB)
agg = Counter()
for d in tgt:
    agg[nb_a.predict(d["feat"])] += 1
print(f"  贺双卿 poems assigned to: {dict(agg)}")
probs = defaultdict(float)
for d in tgt:
    for c, p in nb_a.proba(d["feat"]).items():
        probs[c] += p / len(tgt)
print("  mean P(author) over 双卿's poems: " +
      ", ".join(f"{c}={probs[c]:.2f}" for c in sorted(probs, key=lambda k: -probs[k])))
