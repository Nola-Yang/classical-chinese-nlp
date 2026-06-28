#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
compute_metrics.py —— 纯计算脚本（只算数据, 不下结论）
======================================================
输入: 语料/ 下的全部 .txt (支持单篇或多篇/文件, 篇间以一行 ~~~~~ 分隔)
输出: 数据/ 下的若干 CSV + 一个 metrics_summary.json
      —— 全部是可复核的数值表; 解释与判断不写在代码里, 由人去读数据。

实现的统计/度量(全部纯标准库):
  per-document:  字数, 单字数, TTR, hapax比, Shannon熵, Yule's K, Simpson's D,
                 平均句长, 句数, 虚词率, 入声韵率
  分布度量:      余弦(虚词/单字/二元组), Burrows's Delta, Manhattan, Euclid,
                 Jensen-Shannon散度, KL散度, Jaccard/containment/Dice
  语言模型:      字符二元组 LM(加k平滑) 在各作者语料上的困惑度/交叉熵(含等长截断对照)
  聚类/分类:     全样本距离矩阵; 最近质心分类; 留一法(LOO)作者判别正确率(岳飞 vs 王越)
  小样本基线:    组内两两距离分布, 待考词到各组距离所处的 z / 分位
  特征计数:      地理指向/敌称/母题 词元的逐篇计数矩阵(只计数, 不判断)
"""

import os, re, math, json, csv, itertools
from collections import Counter

BASE = os.path.dirname(os.path.abspath(__file__))
CORPUS_DIR = os.path.join(BASE, "语料")
OUT_DIR = os.path.join(BASE, "数据")

PUNCT_SET = set("，。、；：？！「」『』“”‘’（）()《》〈〉…—－·　 \n\r\t")
CLAUSE_SPLIT = re.compile(r"[，。、；：？！\s]+")
STRONG_STOPS = set("。；！")

# 繁体/异体 -> 简体 规范化(保证各文本同一字形, 不致因字形差异虚增距离)
VARIANT = {"閒":"闲","鍊":"炼","閤":"阁","捲":"卷","噉":"啖","馀":"余","裏":"里",
           "衞":"卫","凈":"净","緑":"绿","喫":"吃","棊":"棋","陞":"升","脩":"修"}

# 文言虚词/高频语法字(文体指纹特征集; 与题材基本无关)
FUNCTION_WORDS = list(dict.fromkeys(
    "之乎者也矣焉哉耳兮而以于於其则乃且因由从向与及至故若如苟虽纵但"
    "不非弗未莫勿无亦又犹尚已即既必当应须将欲能可得方更最甚颇略"
    "何谁孰安岂宁是此彼斯夫盖凡所自相为"
    "处却待还空漫只惟唯独都尽满复向归来去上下中前后"
    "余予我吾臣君卿尔汝子"))

# 入声常用字(中古入声精选子集; 覆盖本语料全部韵脚字, 用于判定满江红入声韵)
RUSHENG = set(
    "歇烈月切雪灭缺血阙郭阁作恶锷壑落洛鹤列穴节结羯兀说竭窟碧色客惜瑟忽读独"
    "一七八十白黑国德食石竹菊急入出发别绝设折决骨卒没突物屈哭福服北墨默得测"
    "刻极息力直识实失室日吉漆膝或惑鹿木目沐速族续玉曲局浴欲育烛屋复腹伏拂弗"
    "佛勿屹蹀肉笛敌摘籍夕昔锡激迹脊益翼乙鸭押牍犊毒掘厥蹶劫怯接捷睫妾叶页协"
    "约略掠雀鹊嚼爵脚却角觉学握渥幄朔铄烁酌灼勺芍岳乐药虐谑确凿")
RUSHENG = set(c for c in RUSHENG if 0x4E00 <= ord(c) <= 0x9FFF)

# 特征词典: 地理指向 / 敌称 / 母题  —— 只用于逐篇"计数", 不在代码内判断异常与否
GEO = {
    "geo_西北_贺兰": ["贺兰山", "贺兰"],
    "geo_东北中原": ["黄龙","燕幽","河洛","河朔","中原","京阙","汴","汉阳",
                     "清河","郊畿","金城","五国城","雁门","神京","神州"],
    "geo_泛指方位": ["沙漠","山河","故地","三关","穷边","龙沙","巡边"],
}
ENEMY = {
    "enemy_泛化胡虏匈奴夷": ["胡虏","匈奴","胡羯","夷狄","夷荒","夷种","虏廷",
                            "虏酋","虏","夷","胡"],
    "enemy_具体金可汗阏氏": ["金酋","金兀","金城","可汗","阏氏","鞑"],
}
MOTIF = {
    "motif_功名": ["功名","功业","万户侯","封侯","军功"],
    "motif_白发年华": ["白了少年头","少年头","白首","白头","白发","白雪老僧头"],
    "motif_收复报国": ["收拾旧山河","旧山河","恢复","收复","迎二圣","迎二帝",
                       "还车驾","请缨","取故地","渡河洛","返神州","酬明圣","报君仇"],
    "motif_魏阙面君": ["朝天阙","天阙","京阙","门阙","金銮","宫阙"],
    "motif_靖康": ["靖康"],
    "motif_饮血暴烈": ["饮匈奴血","蹀血","马蹀","餐胡虏肉","锋锷","膏锋","挥戈"],
}


# ----------------------------------------------------------------------------
# 语料加载(支持多篇/文件)
# ----------------------------------------------------------------------------
def normalize(s):
    return "".join(VARIANT.get(c, c) for c in s)

def parse_doc(chunk):
    meta, body_lines, in_body = {}, [], False
    for line in chunk.splitlines():
        if line.strip().startswith("====="):
            in_body = True; continue
        if not in_body and line.startswith("#"):
            m = re.match(r"#\s*([^:：]+)[:：]\s*(.*)", line)
            if m: meta[m.group(1).strip()] = m.group(2).strip()
        elif in_body:
            body_lines.append(line)
    body = normalize("\n".join(body_lines).strip())
    chars = [c for c in body if c not in PUNCT_SET]
    return meta, body, chars

def load_corpus():
    docs = []
    for fn in sorted(os.listdir(CORPUS_DIR)):
        if not fn.endswith(".txt"): continue
        raw = open(os.path.join(CORPUS_DIR, fn), encoding="utf-8").read()
        for chunk in re.split(r"(?m)^~~~~~+\s*$", raw):
            if "=====" not in chunk: continue
            meta, body, chars = parse_doc(chunk)
            if not chars: continue
            docs.append({
                "file": fn, "title": meta.get("标题","?"),
                "author": meta.get("作者","?"), "genre": meta.get("体裁","?"),
                "klass": meta.get("类别","?"), "reliab": meta.get("可靠度","—"),
                "body": body, "chars": chars, "n": len(chars),
            })
    return docs


# ----------------------------------------------------------------------------
# 计量原语
# ----------------------------------------------------------------------------
def counts(seq): return Counter(seq)
def bigrams(chars): return [chars[i]+chars[i+1] for i in range(len(chars)-1)]

def relfreq(cnt, vocab):
    tot = sum(cnt.values()) or 1
    return [cnt.get(w,0)/tot for w in vocab]

def shannon_entropy(chars):
    n = len(chars) or 1; c = Counter(chars)
    return -sum((v/n)*math.log2(v/n) for v in c.values())

def yule_k(chars):
    n = len(chars) or 1; c = Counter(chars)
    vr = Counter(c.values())  # r -> 出现r次的字种数
    m2 = sum(r*r*v for r,v in vr.items())
    return 1e4*(m2 - n)/(n*n)

def simpson_d(chars):
    n = len(chars)
    if n < 2: return 0.0
    c = Counter(chars)
    return sum(v*(v-1) for v in c.values())/(n*(n-1))

def hapax_ratio(chars):
    c = Counter(chars); v = len(c) or 1
    return sum(1 for x in c.values() if x==1)/v

def func_rate(chars):
    fset = set(FUNCTION_WORDS)
    return sum(1 for c in chars if c in fset)/(len(chars) or 1)

def clause_lengths(body):
    cl = [s for s in CLAUSE_SPLIT.split(body) if s]
    return cl

def rusheng_rate(body):
    rh, cur = [], []
    for ch in body:
        if ch in PUNCT_SET:
            if ch in STRONG_STOPS and cur: rh.append(cur[-1])
            if ch in "。；！？，、": cur = []
            continue
        cur.append(ch)
    if not rh: return None, []
    return sum(1 for c in rh if c in RUSHENG)/len(rh), rh

# 相似度/距离
def cosine(a,b):
    dot=sum(x*y for x,y in zip(a,b)); na=math.sqrt(sum(x*x for x in a)); nb=math.sqrt(sum(y*y for y in b))
    return dot/(na*nb) if na and nb else 0.0
def manhattan(a,b): return sum(abs(x-y) for x,y in zip(a,b))
def euclid(a,b): return math.sqrt(sum((x-y)**2 for x,y in zip(a,b)))
def jaccard(A,B):
    A,B=set(A),set(B); return len(A&B)/len(A|B) if (A or B) else 0.0
def containment(A,B):
    A,B=set(A),set(B); return len(A&B)/len(A) if A else 0.0
def dice(A,B):
    A,B=set(A),set(B); return 2*len(A&B)/(len(A)+len(B)) if (A or B) else 0.0
def jsd(P,Q):  # Jensen-Shannon 散度(bits), 对称, 有界[0,1]
    M=[(p+q)/2 for p,q in zip(P,Q)]
    def kl(a,b): return sum(x*math.log2(x/y) for x,y in zip(a,b) if x>0 and y>0)
    return 0.5*kl(P,M)+0.5*kl(Q,M)
def kl_smooth(P,Q,eps=1e-9):  # KL(P||Q), Q加eps平滑
    return sum(p*math.log2(p/(q+eps)) for p,q in zip(P,Q) if p>0)

# 字符二元组语言模型(加k平滑) -> 困惑度 / 交叉熵
def train_lm(train_chars, vocab, k=0.5):
    bi = Counter(); uni = Counter()
    prev = "∎"
    for c in train_chars:
        bi[(prev,c)] += 1; uni[prev] += 1; prev = c
    V = len(vocab)
    def logp(prev, cur):  # 自然对数
        return math.log((bi.get((prev,cur),0)+k)/(uni.get(prev,0)+k*V))
    return logp
def eval_lm(test_chars, logp):
    if not test_chars: return float("inf"), float("inf")
    prev="∎"; tot=0.0
    for c in test_chars:
        tot += logp(prev,c); prev=c
    avg = tot/len(test_chars)         # 平均对数似然(nats)
    ppl = math.exp(-avg)              # 困惑度
    ce_bits = -avg/math.log(2)        # 交叉熵(bits/字)
    return ppl, ce_bits


# ----------------------------------------------------------------------------
def write_csv(name, header, rows):
    path = os.path.join(OUT_DIR, name)
    with open(path,"w",newline="",encoding="utf-8-sig") as f:
        w=csv.writer(f); w.writerow(header); w.writerows(rows)
    return path

def fmt(x, nd=4):
    if x is None: return ""
    if isinstance(x,float):
        if math.isinf(x): return "inf"
        return round(x,nd)
    return x


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    docs = load_corpus()
    target = next(d for d in docs if d["klass"]=="待考")
    written = []

    # 分组
    G = {
        "岳飞_全":    [d for d in docs if d["klass"]=="岳飞"],
        "岳飞_高可靠": [d for d in docs if d["klass"]=="岳飞" and d["reliab"]=="高"],
        "王越":       [d for d in docs if d["klass"]=="王越"],
        "苏轼对照":   [d for d in docs if d["klass"]=="对照"],
        "族谱存疑":   [d for d in docs if d["klass"]=="发现存疑"],
    }
    def cat(dl):
        out=[]; [out.extend(d["chars"]) for d in dl]; return out

    # 各"视图"的词表与逐篇计数器
    vocab_uni = sorted({c for d in docs for c in d["chars"]})
    vocab_bi  = sorted({b for d in docs for b in bigrams(d["chars"])})
    vocab_fw  = [w for w in FUNCTION_WORDS if any(w in d["chars"] for d in docs)]
    cnt_uni = {d["file"]+d["title"]: counts(d["chars"]) for d in docs}
    cnt_bi  = {d["file"]+d["title"]: counts(bigrams(d["chars"])) for d in docs}
    def key(d): return d["file"]+d["title"]

    # ============ (1) 逐篇特征表 ============
    hdr=["title","author","genre","group","reliab","n_chars","n_types","TTR",
         "hapax_ratio","shannon_bits","yule_K","simpson_D","mean_clause_len",
         "n_clauses","func_word_rate","rusheng_rhyme_rate"]
    rows=[]
    for d in docs:
        cl=clause_lengths(d["body"]); rr,_=rusheng_rate(d["body"])
        rows.append([d["title"],d["author"],d["genre"],d["klass"],d["reliab"],
            d["n"],len(set(d["chars"])),fmt(len(set(d["chars"]))/d["n"]),
            fmt(hapax_ratio(d["chars"])),fmt(shannon_entropy(d["chars"])),
            fmt(yule_k(d["chars"]),2),fmt(simpson_d(d["chars"])),
            fmt(sum(len(x) for x in cl)/(len(cl) or 1),2),len(cl),
            fmt(func_rate(d["chars"])), fmt(rr) if rr is not None else ""])
    written.append(write_csv("01_逐篇特征.csv",hdr,rows))

    # ============ (2) 待考 vs 各篇: 全套相似度/距离 ============
    tU=relfreq(cnt_uni[key(target)],vocab_uni)
    tB=relfreq(cnt_bi[key(target)],vocab_bi)
    tF=relfreq(counts(target["chars"]),vocab_fw)
    tcs,tbs=set(target["chars"]),set(bigrams(target["chars"]))
    hdr=["对比对象","group","genre","cos_虚词","cos_单字","cos_二元组","JSD_单字",
         "JSD_二元组","KL_待考||对象_单字","Manhattan_单字","Euclid_单字",
         "Jaccard_字","containment_字被覆盖","Dice_字","Jaccard_二元组","containment_二元组"]
    rows=[]
    for d in docs:
        if d is target: continue
        dU=relfreq(cnt_uni[key(d)],vocab_uni); dB=relfreq(cnt_bi[key(d)],vocab_bi)
        dF=relfreq(counts(d["chars"]),vocab_fw)
        rows.append([d["title"],d["klass"],d["genre"],
            fmt(cosine(tF,dF)),fmt(cosine(tU,dU)),fmt(cosine(tB,dB)),
            fmt(jsd(tU,dU)),fmt(jsd(tB,dB)),fmt(kl_smooth(tU,dU)),
            fmt(manhattan(tU,dU)),fmt(euclid(tU,dU)),
            fmt(jaccard(tcs,set(d["chars"]))),fmt(containment(tcs,set(d["chars"]))),
            fmt(dice(tcs,set(d["chars"]))),
            fmt(jaccard(tbs,set(bigrams(d["chars"])))),
            fmt(containment(tbs,set(bigrams(d["chars"]))))])
    rows.sort(key=lambda r:-r[5])  # 按 cos_二元组 降序
    written.append(write_csv("02_待考vs各篇_相似度.csv",hdr,rows))

    # ============ (3) 待考 vs 各组质心: 相似度 + Burrows Delta ============
    # Burrows Delta: 以全体单篇为参照算虚词 z 分
    fwmat=[relfreq(counts(d["chars"]),vocab_fw) for d in docs]
    means=[sum(col)/len(col) for col in zip(*fwmat)]
    stds=[(math.sqrt(sum((x-m)**2 for x in col)/len(col)) or 1e-9)
          for col,m in zip(zip(*fwmat),means)]
    def zfw(vec): return [(vec[j]-means[j])/stds[j] for j in range(len(vocab_fw))]
    tz=zfw(tF)
    def delta(group_chars):
        z=zfw(relfreq(counts(group_chars),vocab_fw))
        return sum(abs(a-b) for a,b in zip(tz,z))/len(vocab_fw)
    hdr=["组","n_docs","n_chars","cos_虚词","cos_单字","cos_二元组","BurrowsDelta_虚词",
         "JSD_单字","JSD_二元组","containment_字","containment_二元组"]
    rows=[]
    for name,dl in G.items():
        if not dl: continue
        gc=cat(dl)
        gU=relfreq(counts(gc),vocab_uni); gB=relfreq(counts(bigrams(gc)),vocab_bi)
        gF=relfreq(counts(gc),vocab_fw)
        rows.append([name,len(dl),len(gc),fmt(cosine(tF,gF)),fmt(cosine(tU,gU)),
            fmt(cosine(tB,gB)),fmt(delta(gc)),fmt(jsd(tU,gU)),fmt(jsd(tB,gB)),
            fmt(containment(tcs,set(gc))),fmt(containment(tbs,set(bigrams(gc))))])
    written.append(write_csv("03_待考vs各组质心.csv",hdr,rows))

    # ============ (4) 字符二元组语言模型 困惑度 ============
    # 全量训练 + 等长截断(取与岳飞_全等长, 控制语料规模混淆)
    n_match=len(cat(G["岳飞_全"]))
    hdr=["训练语料(作者组)","训练字数(全量)","困惑度PPL_全量","交叉熵bits_全量",
         "训练字数(截断)","困惑度PPL_等长","交叉熵bits_等长"]
    rows=[]
    for name in ["岳飞_全","岳飞_高可靠","王越","族谱存疑","苏轼对照"]:
        dl=G[name];
        if not dl: continue
        gc=cat(dl)
        ppl_full,ce_full=eval_lm(target["chars"],train_lm(gc,vocab_uni,0.5))
        gc_m=gc[:n_match]
        ppl_m,ce_m=eval_lm(target["chars"],train_lm(gc_m,vocab_uni,0.5))
        rows.append([name,len(gc),fmt(ppl_full,2),fmt(ce_full),
                     len(gc_m),fmt(ppl_m,2),fmt(ce_m)])
    written.append(write_csv("04_语言模型困惑度.csv",hdr,rows))

    # ============ (5) 全样本距离矩阵(二元组余弦 & 单字JSD) ============
    labels=[f'{d["klass"][:2]}|{d["title"][:10]}' for d in docs]
    for metric,name in [("cosB","05a_距离矩阵_二元组余弦.csv"),
                        ("jsdU","05b_距离矩阵_单字JSD.csv")]:
        mat=[]
        for di in docs:
            vi_U=relfreq(cnt_uni[key(di)],vocab_uni); vi_B=relfreq(cnt_bi[key(di)],vocab_bi)
            row=[]
            for dj in docs:
                vj_U=relfreq(cnt_uni[key(dj)],vocab_uni); vj_B=relfreq(cnt_bi[key(dj)],vocab_bi)
                row.append(fmt(cosine(vi_B,vj_B) if metric=="cosB" else jsd(vi_U,vj_U)))
            mat.append([labels[docs.index(di)]]+row)
        written.append(write_csv(name,[""]+labels,mat))

    # ============ (6) 最近质心分类 + 留一法(LOO)作者判别正确率 ============
    # 两大类 = 岳飞_全, 王越 (各有足够样本); 用余弦最近质心。
    def relfreq_from_docs(dl, view, vocab):
        c=Counter()
        for d in dl:
            c += (cnt_uni[key(d)] if view=="uni" else
                  cnt_bi[key(d)] if view=="bi" else counts(d["chars"]))
        return relfreq(c,vocab)
    classes={"岳飞":G["岳飞_全"],"王越":G["王越"]}
    views={"虚词":("fw",vocab_fw),"单字":("uni",vocab_uni),"二元组":("bi",vocab_bi)}
    def classify(vec, dl_exclude=None, view="bi", vocab=vocab_bi):
        best,bn=-1,None; scores={}
        for cn,dl in classes.items():
            sub=[d for d in dl if d is not dl_exclude]
            cen=relfreq_from_docs(sub,view,vocab); s=cosine(vec,cen); scores[cn]=s
            if s>best: best,bn=s,cn
        return bn,scores
    hdr=["视图","LOO正确率(岳飞vs王越)","岳飞→岳飞","岳飞→王越","王越→王越","王越→岳飞",
         "待考判给","cos_岳飞","cos_王越","判别边际margin"]
    rows=[]; loo_detail=[]
    for vlabel,(view,vocab) in views.items():
        correct=0; tot=0; conf={("岳飞","岳飞"):0,("岳飞","王越"):0,("王越","王越"):0,("王越","岳飞"):0}
        for cn,dl in classes.items():
            for d in dl:
                vec=relfreq(cnt_uni[key(d)] if view=="uni" else cnt_bi[key(d)] if view=="bi" else counts(d["chars"]),vocab)
                pred,_=classify(vec,dl_exclude=d,view=view,vocab=vocab)
                conf[(cn,pred)] = conf.get((cn,pred),0)+1
                correct += (pred==cn); tot+=1
                loo_detail.append([vlabel,cn,d["title"],pred])
        tvec=relfreq(cnt_uni[key(target)] if view=="uni" else cnt_bi[key(target)] if view=="bi" else counts(target["chars"]),vocab)
        tpred,tsc=classify(tvec,view=view,vocab=vocab)
        margin=abs(tsc["岳飞"]-tsc["王越"])
        rows.append([vlabel,fmt(correct/tot),conf[("岳飞","岳飞")],conf[("岳飞","王越")],
            conf[("王越","王越")],conf[("王越","岳飞")],tpred,fmt(tsc["岳飞"]),fmt(tsc["王越"]),fmt(margin)])
    written.append(write_csv("06_最近质心分类_LOO判别.csv",hdr,rows))
    written.append(write_csv("06b_LOO逐篇判别明细.csv",["视图","真实作者","篇目","判给"],loo_detail))

    # ============ (7) 小样本基线: 组内两两距离 vs 待考到组距离 ============
    def pair_sims(dl, view, vocab):
        vs=[relfreq(cnt_uni[key(d)] if view=="uni" else cnt_bi[key(d)] if view=="bi" else counts(d["chars"]),vocab) for d in dl]
        return [cosine(vs[i],vs[j]) for i,j in itertools.combinations(range(len(vs)),2)]
    def to_group_sims(dl, view, vocab):
        tv=relfreq(cnt_uni[key(target)] if view=="uni" else cnt_bi[key(target)] if view=="bi" else counts(target["chars"]),vocab)
        return [cosine(tv, relfreq(cnt_uni[key(d)] if view=="uni" else cnt_bi[key(d)] if view=="bi" else counts(d["chars"]),vocab)) for d in dl]
    hdr=["组","视图","组内cos_均值","组内cos_标准差","组内cos_min","组内cos_max",
         "待考到该组cos_均值","z=(待考均值-组内均值)/组内sd"]
    rows=[]
    for gname in ["岳飞_全","王越"]:
        for vlabel,(view,vocab) in views.items():
            within=pair_sims(G[gname],view,vocab);
            if not within: continue
            wm=sum(within)/len(within); wsd=math.sqrt(sum((x-wm)**2 for x in within)/len(within)) or 1e-9
            tg=to_group_sims(G[gname],view,vocab); tgm=sum(tg)/len(tg)
            rows.append([gname,vlabel,fmt(wm),fmt(wsd),fmt(min(within)),fmt(max(within)),
                         fmt(tgm),fmt((tgm-wm)/wsd)])
    written.append(write_csv("07_组内基线_vs_待考.csv",hdr,rows))

    # ============ (8) 特征计数矩阵(地理/敌称/母题; 只计数) ============
    feat_groups=[("GEO",GEO),("ENEMY",ENEMY),("MOTIF",MOTIF)]
    cols=[];
    for _,fg in feat_groups: cols += list(fg.keys())
    hdr=["title","group","genre"]+cols+["命中词例"]
    rows=[]
    for d in docs:
        rowcounts=[]; hitwords=[]
        for _,fg in feat_groups:
            for label,words in fg.items():
                hits=[w for w in words if w in d["body"]]
                rowcounts.append(sum(d["body"].count(w) for w in hits))
                hitwords += hits
        rows.append([d["title"],d["klass"],d["genre"]]+rowcounts+["/".join(dict.fromkeys(hitwords))])
    written.append(write_csv("08_特征计数_地理敌称母题.csv",hdr,rows))

    # ============ JSON 汇总(机器可读) ============
    summary={
        "n_docs":len(docs),
        "groups":{k:{"n_docs":len(v),"n_chars":len(cat(v))} for k,v in G.items() if v},
        "target":target["title"],
        "vocab_sizes":{"unigram":len(vocab_uni),"bigram":len(vocab_bi),"funcword":len(vocab_fw)},
        "outputs":[os.path.basename(p) for p in written],
        "note":"本文件仅含数据与运行参数; 不含结论。解释见 README / 人工分析。",
    }
    json.dump(summary,open(os.path.join(OUT_DIR,"metrics_summary.json"),"w",encoding="utf-8"),
              ensure_ascii=False,indent=2)

    print(f"语料: {len(docs)} 篇 | 各组: "+", ".join(f"{k}={len(v)}篇/{len(cat(v))}字" for k,v in G.items() if v))
    print(f"词表: 单字{len(vocab_uni)} 二元组{len(vocab_bi)} 虚词{len(vocab_fw)}")
    for p in written: print("写出:", os.path.relpath(p,BASE))
    print("写出:", os.path.relpath(os.path.join(OUT_DIR,'metrics_summary.json'),BASE))

if __name__=="__main__":
    main()
