# 贺双卿 case — corpus provenance

All texts were **parsed from raw HTML** retrieved from 古诗文网
(`m.gushiwen.cn`), not hand-transcribed, to avoid typing errors. The poem body
of each entry comes from the page's `contson` block; editorial variant notes
(e.g. `（一作：…）`) were stripped; traditional/variant glyphs are normalised by
the engine at load time.

Each `*.txt` file holds one poet, with multiple poems separated by a line of
`~~~~~`. Per-poem headers drive the grouping:

```
# 标题: <词牌·题>
# 作者: <poet>
# 类别: <poet>     ← the group label the engine attributes by (--group-field 类别)
# 体裁: 词 | 诗
=====
<poem body>
```

| file | poet | n | genre |
|---|---|--:|---|
| `00_heshuangqing.txt` | 贺双卿 (target) | 10 | 词 |
| `10_gutaiqing.txt` | 顾太清 | 10 | 词 |
| `11_wuzao.txt` | 吴藻 | 10 | 词 |
| `20_nalanxingde.txt` | 纳兰性德 | 10 | 词 |
| `21_xianghongzuo.txt` | 项鸿祚 | 10 | 词 |
| `30_shizhenlin.txt` | 史震林 | 2 | 诗 |

**Caveats baked into the data.** Samples are small (~10 poems / ~750 chars per
poet); 双卿's surviving corpus is itself only ~14 词. 史震林's readily-attested
verse is just two 诗, so his group is tiny and genre-mismatched — see the case
write-up for why that limits (rather than enables) the fabrication test.
