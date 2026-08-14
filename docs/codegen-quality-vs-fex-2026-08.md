# SVM vs FEX 生成代码质量对比(blow-up 口径,2026-08-14)

不依赖安静窗的代码质量对比:同一 guest 热点,SVM(master 0050d9d)与
FEX(f2e35f3,启用原生 SHA/AES lowering 的新参照)各自生成的 host 代码量
比。指标 = **每 guest 指令摊到的 host 指令数**(host/guest blow-up),
按 SVM 侧动态 entries 加权。两侧均为确定性计数,与机器负载无关。

## 主表(entries 加权,匹配覆盖 ≥99.15%)

| 语料 | 匹配覆盖 | SVM host/guest | FEX host/guest | **SVM/FEX** |
|---|---:|---:|---:|---:|
| coremark | 100.00% | 4.166 | 1.807 | **2.305×** |
| stream | 100.00% | 3.087 | 3.336 | **0.925×(SVM 胜)** |
| smallpt | 100.00% | 3.443 | 1.549 | **2.223×** |
| sqlite | 99.93% | 5.193 | 2.253 | **2.305×** |
| cray | 100.00% | 3.916 | 1.616 | **2.423×** |
| zip7 | 99.97% | 3.518 | 1.264 | **2.783×** |
| osslsha | 99.15% | 4.320 | 1.426 | **3.030×** |
| osslaes | 100.00% | 3.081 | 2.541 | **1.213×** |

几何平均(8 语料)≈ **2.01×**;剔除 stream 的 7 语料 ≈ **2.25×**。

读法:在相同 guest 指令流上,SVM 生成的 host 指令量是 FEX 的 2.2~3.0×;
stream 是唯一 SVM 更密的语料(与墙钟 Copy 0.999 持平互洽);osslaes 差距
最小(AES 链合并把热块压到 1.41 h/g,FEX 同区 1.22)。

## 方法学

**SVM 侧**(w67 普查,详见 docs/w67-codegen-quality-svm-census.md):
RE=0(SVM_REGION_EDGES=0)、禁 EXEC_PROF、冷 JIT cache;双跑静态四元组
(host_static, guest_inst, move_static, spill_static)digit-exact 才入 TSV;
guest_inst=decoder 成功 lower 的架构指令数(objdump -dw 复算锚点 170 条一致)。
8 语料共 58,473 个热块,entries 覆盖 ≥99.99998%。TSV(2MB)存
SwiftVM-w67/codegen-quality-svm-*.tsv,可按报告 §3 口径重生。

**FEX 侧**(orb,测量构建 /usr/local/fex-measure,不动 /usr/bin 基准参照):
f2e35f3 + ENABLE_VIXL_DISASSEMBLER=ON + JIT.cpp CompileCode 直插
FEX_BLOCKSTATS 探针(fprintf stderr,旁路 LogMan——上游 OutputLog=server
路由在本部署吞日志,stderr 重定向实测无效,参照二进制同行为)。每编译块输出
rip/guest_bytes/guest_inst/host_inst(host_inst = CodeOnlySize/4,不含块尾)。
运行参数同 harness FEX 臂,MULTIBLOCK=1,码缓存关(每跑全量重编译)。

**特征位对齐**:FEX 默认暴露 AVX,guest 二进制(stream/osslaes)在 FEX 下
走 AVX 路径、SVM 下走 SSE 路径——两侧执行的 guest 代码本身就不同(首版
采集 stream 匹配率仅 19%)。正式表用 FEX_HOSTFEATURES=disableavx 对齐,
匹配率升到 99~100%。**墙钟对比永远包含这层 guest 路径不对称;blow-up
表是同 guest 代码的纯 codegen 对比。**

**汇合规则**:SVM 热 PC p 匹配 FEX 块 [rip, rip+guest_bytes) ∋ p(多重
包含取最紧 span);两侧各算 entries 加权 Σ(e·host)/Σ(e·guest)。

## 逐块细节(top 热块)

```text
coremark top-1 0x402680 e=887.4M  svm 17/6=2.83   fex 超块0x402600+626B 299/171=1.75
stream   top-1 0x416980 e=80.0M   svm 42/14=3.00  fex 同址块+92B 43/22=1.96
smallpt  top-1 0x41ef00 e=12.8M   svm 48/8=6.00   fex 同址块+184B 145/49=2.96
sqlite   top-1 0x4a5149 e=98.2M   svm 34/5=6.80   fex 0x4a5138+553B 30/11=2.73
cray     top-1 0x402fc0 e=11.8M   svm 26/11=2.36  fex 0x402e70+4239B 1259/962=1.31
zip7     top-1 0x42d0b0 e=441.9M  svm 12/5=2.40   fex 0x42ca90+5888B 1666/1605=1.04
osslsha  top-1 0x8ba580 e=4.1M    svm 723/170=4.25 fex 0x8b9340+13118B 3145/2604=1.21
osslaes  top-1 0x634960 e=136.9M  svm 72/51=1.41  fex 0x634580+1606B 389/318=1.22
```

## 发现

1. **2.25× 是结构性的**:SVM move 占比 20~39%(普查 §5),即便把 move 桶
   机械归零(已证当前证明体系下界=0)coremark 也只到 ~2.7,仍高于 FEX 1.81。
   剩余差距在 flags 处理、发布/提交模型与块边界成本——全在已封存方向的
   重开前置条件上(fault recipe、双入口 ABI、跨 class lane fusion)。
2. **stream 证明 SVM 的向量直译在简单密集循环上已优于 FEX**(0.925×),
   FEX 每块固定成本(链接尾巴/ dispatcher 接口)在超块 amortize 后仍略亏。
3. **osslaes 1.21× 是翻盘机制的兑现形态**:热块 1.41 vs 1.22 基本同量级。
4. **osslsha 3.03× 是最深格**:SHA 热块 SVM 4.25 vs FEX 1.21,FEX 超块
   (2604 guest inst)把发布成本摊到近零,而 SVM 每块 43 Set+24 Get 的
   fixed-home commit 是 fault 可见性义务(2026-08-14 审计双归零)。
5. **路径不对称警告**:任何 SVM/FEX 墙钟对比,若不禁 FEX AVX,两边跑
   的可能不是同一段 guest 代码(stream/osslaes 实证)。

## 口径警示

- SVM 侧是 RE=0 块质量口径,默认 region 形态的动态密度另算;
- FEX host_inst 不含块尾(tail/重定位区),SVM host_static 为发码条数,
  两侧都是"块主体指令数",可比但有 ±数% 口径余量;
- 权重取 SVM 侧 entries(同 guest 二进制同工作负载,热点结构可迁移);
- 本轮无任何墙钟声明。
