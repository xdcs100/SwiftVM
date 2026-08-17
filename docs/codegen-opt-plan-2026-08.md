# SVM 代码质量优化方案(orb Linux identity,2026-08-18)

承接 docs/codegen-quality-vs-fex-2026-08.md(mac/bias blow-up)与
docs/codegen-mechanism-gap-2026-08.md(三支柱),用 **当前 master 发码 +
orb Linux identity** 重测后的优先级,把可做的切片和已封存重开条件写成
一张施工图。

本文取代 docs/fex-codegen-gap-plan-2026-08.md 作为**现行战役文档**。
旧文的组合论证(flags×边内部化、XMM×合并器×边界不落地、超块×边界免税)
仍然成立;墙钟锚点(baseline6)与 mac 动态%账不再作本方案分子。

裁定口径不变:**代码质量(entries 加权 host/guest)先行,墙钟只在安静窗
复测,受载读数不作裁定**。每项默认 OFF 或保留 `=0` 回退,禁止无开关合入。

## 0. 数据锚点

- **SVM**:orb `ubuntu` aarch64,Linux identity(`SVM_MEM_IDENTITY` 缺省),
  `SVM_REGION_EDGES=0`(与 8/14 普查同口径;生产缺省 RE 已是 ON,门禁须双态),
  禁 `SVM_JIT_CACHE` / `SVM_EXEC_PROF`。二进制 = svm-phasec RelWithDebInfo
  @ `91952f2`,与 HEAD `5ff4344` 发码相同(其后仅文档;最后发码提交
  `4acba9e` 声明零变化)。
- **FEX**:同机 `/usr/local/fex-measure` `f2e35f3`,`MULTIBLOCK=1`,
  `FEX_HOSTFEATURES=disableavx`。
- 权重:w67 TSV 的 entries × `guest_inst`;SVM host 用本次 `svm-hot-all`。
- 机制桶:本次 Linux `svm-gap-op`(bytes/4)。合计数略低于 `host_static`
  (块尾/未入账 IR),缺额主要是 terminal/link,不改排序。
- 原始日志:orb `/tmp/svm-linux-cq/`(hot.log + 全量 log)。

### 0.1 Linux identity blow-up(相对 FEX)

| 语料 | SVM h/g | FEX h/g | SVM/FEX | 相对 8/14 mac 表 |
| --- | ---: | ---: | ---: | --- |
| stream | 2.412 | 3.336 | **0.72×** | 0.925→0.72,热块 `0x416980` 42→32 |
| osslaes | 3.081 | 2.541 | **1.21×** | 本轮 Linux 热 PC 离开 `0x634960`,沿用 8/14 同 PC |
| sqlite | 4.900 | 2.253 | **2.18×** | 5.19→4.90 |
| coremark | 4.044 | 1.807 | **2.24×** | 4.17→4.04;顶块 `0x402680` 仍 17/6 |
| smallpt | 3.550 | 1.549 | **2.29×** | 3.44→3.55,identity 对 FP 不是免费午餐 |
| cray | 4.043 | 1.616 | **2.50×** | 3.92→4.04 |
| zip7 | 3.409 | 1.264 | **2.70×** | 3.52→3.41;顶块 `0x42d0b0` 12→10/5 |
| osslsha | 4.197 | 1.426 | **2.94×** | 4.32→4.20;顶块 `0x8ba580` 723→703/170 |

7 格可复算几何均值(含 stream、不含路径漂了的 osslaes)≈ **2.07×**;
去 stream ≈ **2.46×**。mac 表的 2.01× / 2.25× 仍可引用,但**立项与验收
一律用本表 Linux 列**。

identity 已把访存税从 mac 账里抠掉一截(stream 最明显)。剩下的不是
寻址 bias,是表示层。

### 0.2 先划掉的死账(勿再立项)

沿用并加严 8/6 清单:

- NaN guard(动态账已 0,AFP 白名单默认 ON);
- spill(本次 Linux 热块 `spill_static=0`);
- GPR 全 pin level 3(−10.18%)与 selector 级 SRA(P1 零和、P2 毛池 0.17%,
  docs/w68-p1-joint-distribution.md / docs/w66-p2-homefact-census.md);
- XMM 静态映射单独移植(W76 净负);
- 泛化 move coalescing(可消池 0.8–2.2%,低于 5% 门);
- x25/x27 基建压缩(Linux identity 全负,docs/w66-infra-reclaim-migration.md);
- 跨块活性代价(canonical-per-block 下事件集为空);
- ADRP/literal pool 常量路线;
- direct link 再优化(已与 FEX 打平为一条 `b`);
- 删 SetHost/GetHost/StoreUniform 而不做 per-fault recipe(osslsha 双归零,
  docs/w67-osslsha-tie-audit.md)。

`SVM_EXEC_PROF=1` 污染发码,密度测量一律禁止。

## 1. 优先级总表

| 级 | 机制 | 代表 Δh/g(SVM−FEX) | 状态 | 第一刀 |
| --- | --- | --- | --- | --- |
| **P0** | 发射经济性:flags 打包 ALU + 宽度桥 | coremark alu+xport 1.93 vs FEX 0.88(**+1.05**) | 半开:宽度可测;打包算术封在 flags 表示 | 先拆账,再决定翻 `SVM_RA_WIDTH_CHAIN` / 重开 W-β |
| **P1** | 状态访问(publish+read) | +0.55~1.10,FEX=0 | **封存** | 禁止删 commit;重开 = fault recipe |
| **P2** | 块边界税(AdvancePC/SetLocation/PushRSB) | AdvPC 0.22~0.47 | 条件开 | 必须 flags 先行,否则边内部化净账≈0 |
| **P3** | osslsha SHA 形态 | 2.94× 整格;热块 4.14 vs 1.21 | **封存** | lane-fusion 新基建或 P1 recipe |
| **P4** | smallpt/cray GetOperand+XMM 往返 | GetOperand 0.20~0.29 + SetHostFPR | 条件开 | 合并器落地后才允许静态家 |

P0 吃掉 2.2–2.9× 的 60–80%。P1+P2 是同一套「值从第一行起在家 + 块变长」。
P3/P4 是专用形态,不挡 P0 开工。

## 2. P0 发射经济性

### 2.1 实测构成(Linux,h/g)

| | coremark | zip7 | FEX coremark | FEX zip7 |
| --- | ---: | ---: | ---: | ---: |
| alu | 1.377 | 0.898 | 0.486 | 0.573 |
| transport | 0.556 | 0.438 | 0.397 | 0.208 |

coremark 顶操:`Sub 0.626`、`BitExtract 0.360`、`And 0.350`、
`AdvancePC 0.356`(属 P2)、`SetHostGPR 0.258`(属 P1)、
`ZeroExtend32To64 0.162`、`GetHostGPR 0.155`、`Or 0.138`。

flags 专项桶只有 0.03——**真实 flags 成本藏在 Sub/And/Or/BitExtract**。
这与 8/14 机制文一致,Linux identity 没有改变这条结论。

### 2.2 切成两刀,禁止捆成「再写一个 fold」

**P0-A 宽度/identity 桥(可立即测)**

- 对象:`BitExtract`、`ZeroExtend32To64`、`lsr #0`/`ubfx #0,#32`、
  已物化寄存器上的 fallback mov+alu。
- 已有开关:`SVM_RA_WIDTH_CHAIN` 缺省 OFF(多节点 identity);
  `SVM_RA_WIDTH_CHAIN_LONG` 缺省 ON。
- 不是泛化 coalescing(已否)。只许走「同一 SSA 值的宽度 identity /
  已证 last-use 的发布点定向合并」,与 `intwidth_tie` 同族。
- 第一刀 = **只读密度 A/B + 现成开关**,不写新 pass:
  1. orb Linux,`SVM_RA_WIDTH_CHAIN=1`,`REGION_EDGES` 0/1 两态;
  2. 看 coremark/zip7 的 `BitExtract`+`ZeroExtend32To64` h/g 与
     `host_static`;
  3. sqlite 五态 + 指纹必须零回归(tie 类历史雷区)。
- 过门再谈翻默认。预估上限就是这两项之和(coremark ~0.52 h/g),
  **达不到 parity**,只是把 P0 里还能不用改 flags 契约的部分收掉。

**P0-B flags 打包算术(W-β 重开,先设计后代码)**

- 对象:为 PF/AF/窄 CF 服务的 `Sub`/`And`/`Or`/`Xor`。FEX 对应是
  NZCV 永驻 PSTATE + PF/AF 专用 GPR,这类 ALU **根本不出现**。
- `SVM_FLAGS_REGS_AUDIT`(B0)已证净下界未过 8% 门——因为它数的是
  SaveFlags/MergeNZCV,吃不到 alu 桶里的打包算术。
- 重开纸门(全过才许写生产代码):
  1. 在 coremark/zip7 上把 `Sub`/`And`/`Or` 按「flags 打包 /
     真算术 / 地址」三分,flags 打包须占该三项合计的 **≥60%**
     (否则利润池不够动 x26);
  2. 设计必须让这三分里的 flags 打包在密度账上归零,而不是只消
     `SaveFlags`;
  3. fault/signal/SMC/direct-link 的 deopt 按 docs/w79-backedge-flags-audit.md
     障碍清单逐条有载体;
  4. 开关 `SVM_FLAGS_REGS=0` 现场回退,默认 OFF 交付。
- 未过纸门 = 维持封存。禁止「先合入再看数」。

### 2.3 独立小项(不挡 P0-A)

**常量地址寄存器缓存**(旧 W-ε)

- smallpt/cray 同 unit 重复 `movz+movk+mov`。ADRP/literal 已否,
  只许寄存器缓存或 loop invariant。
- 目标:顶 unit 5–11% host。风险低。默认 OFF,密度过门再议翻盘。

**间接 exit / RSB 瘦身**(旧 W-ζ)

- 对 zip7 有意义,与 P0-A 解耦。须单独 dispatcher 契约审查,
  不与 P0-A 同一 PR。

## 3. P1 状态访问(封存,只列重开)

Linux publish+state_read:

| | pub | sread | 合计 | 大头 |
| --- | ---: | ---: | ---: | --- |
| coremark | 0.39 | 0.23 | 0.62 | SetHostGPR 0.26 / GetHostGPR 0.16 / StoreUniform 0.13 |
| sqlite | 0.66 | 0.38 | 1.04 | SetHostGPR 0.44 |
| zip7 | 0.51 | 0.47 | 0.98 | SetHostGPR 0.40 / GetHostGPR 0.29 |
| smallpt | 0.59 | 0.44 | 1.03 | GetOperand 0.29 / StoreUniform 0.22 / SetHostFPR 0.19 |
| cray | 0.73 | 0.37 | 1.10 | SetGPR+SetFPR+GetOperand |
| osslsha | 0.38 | 0.17 | 0.55 | SetHostFPR 0.24 / GetHostFPR 0.13 |
| stream | 0.42 | 0.00 | 0.42 | SetHostFPR 0.31(循环尾提交) |

FEX 两行恒 0。osslsha 43 Set + 24 Get 已逐 IR 证为 fault 点前义务提交。

**重开条件(缺一不可):**

1. per-fault GPR/FPR recovery recipe(已封 `FAULT_CONTEXT_RECIPE` 家族)
   能在 PageFatal 后恢复「未提交的家」;
2. 新表示下重跑 w68 P1 联合分布,**不再 1-for-1 零和**;
3. 密度门:coremark publish+sread 合计下降 **≥0.25 h/g**,且 spill 不回涨。

在此之前,任何「把 SetHost 和 pin 家 tie 掉」的 PR 都按复读 SRA 死账拒绝。
旧方案 W-α 里「Get/SetHost 合并成功 = 0 条指令」**特指这一条**,已关闭;
W-α 只保留 §2.2 P0-A 的宽度定向合并。

## 4. P2 块边界税(条件开)

Linux 控制桶拆分:

| | AdvancePC | SetLocation | PushRSB | SVM ctrl | FEX ctrl |
| --- | ---: | ---: | ---: | ---: | ---: |
| sqlite | 0.465 | 0.127 | 0.091 | 0.74 | 0.47 |
| coremark | 0.356 | 0.050 | 0.032 | 0.44 | 0.35 |
| stream | 0.329 | 0 | 0 | 0.33 | 摊在超块 |
| smallpt | 0.264 | 0.051 | 0.055 | 0.37 | 0.14 |
| zip7 | 0.219 | 0.048 | 0.013 | 0.28 | 0.14 |

热边 region 形成曾因「可转发 uniform 仅 0.004%」判负
(docs/project-status.md 2026-08-03)。那只否掉 **P1 式跨块转发**,
**没有**否掉「把 AdvancePC/SetLocation/PushRSB 摊进更大编译单元」。
两件事不要再捆死。

开工顺序:

1. P0-B 落地(或至少 NZCV 在内部边上免 Merge)——否则边内部化净账≈0
   (`SVM_REGION_EDGES` 历史教训);
2. 再扩前向窗 / 跨函数 Jcc 链 / 内部边裸 `b.cond`;
3. 每条成环回边保留 W81 同款 acquire poll,SMC 失效升到整 region;
4. 密度门用 **RE=0 对照本表** + 默认 RE=ON 各跑一轮。目标:coremark
   AdvancePC 0.356→≤0.15,且 flags/state 桶不回涨。

软件 RSB 对标 FEX「不用 RSB」是独立切片,归旧 W-ζ,不塞进 region PR。

## 5. P3 osslsha / P4 XMM

**P3** 热块 Linux 703/170=4.14 vs FEX 1.21。顶操
`VecSha256Rnds2 1.75` + `VecSha256Msg2 0.33` + `SetHostFPR 0.24`。
67 条毛池是形态池。可立项的只有:

- 跨 register-class SHA lane-fusion(新 lowering,进不了现有 FPR alias 门);
- 或等 P1 recipe 让 fixed-home commit 可延后。

禁止再开 unit-local SetHostFPR tie。

**P4** GetOperand + XMM 往返是 smallpt/cray 2.3–2.5× 的主肉。
必须 **P0-A 合并器能力先在 GPR 上证过**,再交「静态家 + 边界只在
dispatcher/helper 填倒」。单独 `SVM_XMM_STATIC` 继续禁止默认。

## 6. 路线图

```
P0-A 宽度开关复测 ──┐
                    ├─> 过门才翻 WIDTH_CHAIN
P0-A' 常量地址(ε) ──┘

P0-B flags 三分账 + 纸门 ──> W-β 默认 OFF spike
        │
        └─> P2 region 摊薄(δ) ──> coremark/sqlite 中场

P1 fault recipe ──> 状态零发射(SRA 形态,不是再 pin 一档)
P0-A 合并器证过 ──> P4 XMM 组合(γ)
P3 SHA lowering ── 独立,不挡上面
```

波次(只报密度,不报墙钟):

| 波 | 做 | 密度预期(相对本表) |
| --- | --- | --- |
| 0(本周) | P0-B 三分账;P0-A `WIDTH_CHAIN` Linux 双态 A/B | 决策,不承诺收益 |
| 1 | 过门则翻 WIDTH_CHAIN;并行 W-ε | coremark ≤ −0.3 h/g 量级;smallpt/cray 顶 unit 个位数% |
| 2 | W-β OFF spike | coremark alu 桶 Sub/And/Or 打包项明显下降,总 h/g 朝 3.5 走 |
| 3 | P2 在 β 之后 | coremark AdvPC 腰斩量级,总 h/g 朝 3.0 走 |
| 4 | P1/P4/P3 按重开条件 | 2.2× 带的后半;不排进当前冲刺 |

parity(FEX 1.8 h/g 带)不在本方案任何一波的验收里。

## 7. 验收与纪律

每一切片:

1. **密度**:orb Linux,禁 EXEC_PROF,冷 cache;`RE=0` 必须能对上本表同 PC
   `host_static`;默认 RE 另报一列。
2. **正确性**:mac + orb Linux 全量;sqlite 作为 tie/memory 类强制格;
   指纹默认态零差,纯 IR 收缩才许重生 golden(指挥官执笔)。
3. **开关**:新路径默认 OFF,或旧开关 `=0` 精确回退。禁止拆默认又留死开关。
4. **提交**:中文技术说明,不写任务编号,不写 AI trailer。
5. **墙钟**:只在安静窗复测;本方案任何一节都不构成墙钟承诺。
6. **osslaes**:下一次同 PC 普查前,先确认 guest 仍走 `0x634960` SSE 路径,
   否则单独重做 TSV,禁止拿漂移热 PC 和 8/14 FEX 块硬接。

复现本表(orb):

```sh
env -u SVM_JIT_CACHE -u SVM_EXEC_PROF \
  SVM_REGION_EDGES=0 SVM_DENSITY_PROF=1 SVM_RA_HOT_COALESCE_ALL=1 \
  SVM_RA_HOT_COALESCE=/tmp/hot.log \
  svm_translator_linux <guest> <args>
```

FEX 侧保持 `FEX_HOSTFEATURES=disableavx FEX_MULTIBLOCK=1 FEX_BLOCKSTATS=1`。

## 8. 决策请求

方案按「P0-A 先测现成开关、P0-B 先做三分账」排期,不先开 fault recipe /
SRA / 超块。若要打乱顺序(例如先上 XMM 或先扩 region),等于复读已经
NO-GO 的组合拆件,需要明示接受净负风险。
