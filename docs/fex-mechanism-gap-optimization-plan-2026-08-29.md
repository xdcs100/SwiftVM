# SwiftVM 对齐 FEX：剩余机制缺口优化方案（2026-08-29）

## 1. 目标与边界

本文把当前 SwiftVM 与 FEX 的代码形态差距收敛为可实施的机制改进，不再重复已经完成的局部 lowering，也不把 function-level 编译、GPR pin 或 continuation 整体误判为缺失。

目标是优先成批减少以下残余指令：

- 跨编译边界产生的 `b`、`bl`、`ldr`、`str`、`mov`。
- flags 在边界和 join 上产生的 `mrs`、`cfinv`、`bfxil`、`subs`。
- guest 值复制与窄值桥产生的 `mov`、`uxtb`、`uxth`、`ubfx`、`lsr`。
- helper、字符串循环和冷 stub 布局产生的额外调用与常量物化。

本文不以一次长跑结果驱动设计。候选先经过同输入短跑、正确性 oracle 和定向热点对比，只有确认缩小目标账目后才进入正式加权比较。

## 2. 当前基线与判读口径

基线提交为 `0066fc3ef20033983a3309b3e31461a7451e70ce`：

| 语料 | roots | SwiftVM 静态 host 指令 | 正确性 |
|---|---:|---:|---|
| SQLite | 2,164 | 267,957 | 输出一致 |
| smallpt | 263 | 37,745 | PPM SHA-256 一致 |
| CoreMark | 294 | 37,717 | `crcfinal=0x382f` |

SQLite 与 FEX 的共同正差 root 中，当前可归因的正向超额合计约 2,346 条。该值只用于定位机制，不能解释为全程序或动态性能差距：两边的 root 划分、尾部代码和 relocation 统计口径并不完全一致。

当前最大的共同 root 差距为：

| root | SwiftVM | FEX | 差值 | 主要残余形态 |
|---|---:|---:|---:|---|
| `freeSpace` | 416 | 309 | +107 | `mov/ldr/subs/b/bl` |
| `__printf_buffer+0x710` | 197 | 143 | +54 | `mov/subs/str/lsr/ldr/bl/b` |
| `sqlite3DefaultRowEst` | 174 | 120 | +54 | `b/mov/subs/uxth/lsr` |
| `__printf_buffer+0x6b0` | 264 | 213 | +51 | `b/subs/ldur/bl/lsr/adr` |
| `_IO_new_file_xsputn` | 433 | 383 | +50 | `str/ldr/movk/bl/subs/mrs/b` |
| `pcache1TruncateUnsafe` | 188 | 141 | +47 | `mov/ldr/b/str/lsr/ubfx` |
| `powerOfTen` | 200 | 153 | +47 | `mov/ubfx/b/lsr/cmp/bfi` |
| `__strcmp_sse42` | 241 | 195 | +46 | `b/ubfx/orr/lsr/bfi` |

## 3. 已有机制，不重复立项

后续设计必须建立在以下现状上：

- function-level 编译默认开启，从一个入口解码最多 128 个可达块；已知入口截断和 late split 重解码已经落地。
- 默认 pin level 2 已驻留 15/16 个 guest GPR，仅 R15 不在默认映射；level 3 已实测为净负，不再以“补齐最后一个 pin”解释主要差距。
- XMM resident ABI、GPR/FPR fault snapshot、局部 fixed-home coalescing 已存在。
- direct link、静态调用、BL/call-entry continuation、fault-backed indirect-L1 safepoint、RSB/continuation invalidation 已有生产实现。
- 块内 flags 消除、Direct carry、pending-flags entry bypass、完整和部分 NZCV merge 已有实现。
- 常见窄 load、窄 store、32 位 count/div/rem、低位 extract 和简单 EA 融合已经覆盖。

因此，剩余工作不是把上述能力再实现一次，而是把它们从局部证明扩展为统一、可组合的跨边状态契约。

## 4. 总体设计

依赖顺序如下：

```text
函数入口所有权
    -> 外部入口契约与双入口
        -> flags 边状态版本化
        -> guest 值版本与 fault snapshot
            -> 宽度事实跨边传播
    -> continuation 覆盖与热冷布局
        -> 精确 helper ABI / 字符串循环优化
```

核心原则：

1. internal edge 可以携带已证明的 resident 状态；external edge 只能进入有明确契约的 veneer。
2. 状态是否可复用由版本和观察点决定，不能由局部相邻形态猜测。
3. fault、signal、SMC invalidation、dispatcher 和 helper 都是架构状态观察点。
4. join 不要求所有路径都保留最优状态；不兼容路径在 join 前或 entry veneer 中规范化一次。
5. 每个阶段完成迁移后删除被替代的旧分支，不保留重复协议或运行时调试开关。

## 5. P0：函数中部外部入口与多入口代码对象 ABI

### 5.1 问题定义

现有 function-level 能把从规范入口发现的内部直接边编译为一个 HIRFunction，但不能把所有指向函数中部的外部直接边安全地当作普通内部边：

```text
外部入口 E0 -> A -> B
                   ^
另一个 code object -> E1
```

`A -> B` 可以相信内部转发状态；`E1 -> B` 可能来自 dispatcher、旧 direct-link site、另一个函数、return continuation 或 SMC 重链接，不能直接相信 B 的内部入口假设。

此前强制暴露无条件直接跳转并内部化目标，已经出现 host fault、heap corruption、wild PC、错误图像和错误 CRC。缺失的是外部入口状态与所有权协议，不是 function-level 编译本身。

### 5.2 目标形态

每个可外部进入的 guest PC 具有两个不同 host 标签：

- `internal_entry`：只供同一代码对象内、状态契约匹配的边使用。
- `external_entry`：供 dispatcher、direct link、return/indirect continuation 使用；执行必要的规范化后跳入 `internal_entry`。

代码缓存只发布 `external_entry`。普通内部边不得绕过契约跳入其他代码对象的 `internal_entry`。

### 5.3 最小数据模型

为每个已发布入口记录一份 `FunctionEntryContract`，只保存跨模块确实需要的信息：

| 字段 | 用途 |
|---|---|
| guest PC 与代码对象 generation | 查找、重编译和避免旧 host 地址复用 |
| entry kind | canonical、direct-link、continuation 或内部入口 |
| flags requirement | 需要的 NZCV 位、carry 极性及 packed-flags 新鲜度 |
| guest-state requirement | 哪些固定 home/架构槽必须是规范值 |
| continuation requirement | 是否要求已建立 host continuation 及其种类 |
| dependency ownership | 覆盖的 guest 页、SMC generation 和 owner |

第一阶段不引入全量通用状态对象，只实现 flags、continuation 和规范 fixed-home 三类现有 ABI 能表达的字段。

### 5.4 职责拆分

| 组件 | 职责 |
|---|---|
| `FunctionDecodeFrontier` | 发现入口、验证指令边界、判定 owner 是否可重解码；不生成 host 入口代码 |
| 新的 function-entry contract 模块 | 保存 backend-neutral 的入口需求和 provenance |
| 新的 ARM64 entry emitter | 生成 `external_entry` veneer，复用现有 flags/continuation emission |
| code object / LinkManager | 发布入口表，按 generation 链接、unlink 和 invalidation |
| fault/SMC metadata | 把一个代码对象的全部已发布入口纳入同一所有权事务 |

`translator_terminal.cpp` 只消费已经规划好的 entry/link plan，不继续堆叠入口分析逻辑。

### 5.5 分阶段落地

1. 让 `FunctionDecodeFrontier` 为已接受和拒绝的 split target 产生稳定 provenance；不改变生成代码。
2. 仅覆盖“同 module、精确指令边界、唯一 owner、可重解码、无 call-return ownership”的外部直接目标。
3. 为目标生成 external veneer，内部边继续使用 internal label。
4. LinkManager 发布入口 generation，并在 SMC invalidation 中一次性撤销该代码对象的全部外部入口。
5. 通过 `freeSpace` 的 direct-entry 账确认收益后，再扩展到其他 root；不先泛化到重叠指令流或不透明间接目标。

`340247d` 先在现有 function-level decoded roots 上闭合前四步；`34ff5a9` 随后让已有 canonical
return entry 消费该契约并完成第五步的首轮收益账。未经 live-in 证明的 terminal-only return block
仍不发布，不能把 synthetic 空 return 的通过外推到普通函数中部入口。

### 5.6 验收

- 同一目标分别从内部边和外部边进入，结果一致且走不同标签。
- 多个外部入口共享同一代码对象，不产生重复 guest 后缀。
- 任一依赖页失效后，所有入口和 direct-link site 都不再到达旧代码。
- call-return owner、重叠入口、ENDBR64 和非精确边界继续走当前规范外部路径。
- `freeSpace` 至少消除一组重复 LinkBlock/dispatcher/cold-tail 序列；若该 root 不缩小，则不扩大入口覆盖面。

## 6. P1：版本化 edge flags ABI

### 6.1 问题定义

现有实现已经能绕过部分完整 NZCV publication，但边状态仍分散在 region、direct-link、terminal 和 trampoline 逻辑中。跨 region join 或 partial-mask observer 仍可能重复执行 `mrs`、`cfinv`、`bfxil` 和 merge。

### 6.2 设计

在现有 pending-flags entry 基础上引入统一的 `EdgeFlagsState`：

| 状态 | 含义 |
|---|---|
| valid NZCV mask | 当前 PSTATE 中哪些位可被目标直接观察 |
| carry polarity | Direct、Inverted 或 Unknown |
| packed flags version | x26/架构 flags 槽对应的逻辑版本 |
| producer kind | arithmetic、logical、restore 或 canonical |

边兼容规则：

- 完全匹配：直接进入 internal entry，不发射 merge。
- 目标在任何观察前覆盖所需位：允许忽略对应 incoming 位。
- 仅极性不匹配：在边或 join 处规范化一次。
- mask 或版本不兼容：进入 external veneer，使用现有 merge 逻辑修复。
- 多前驱 join：只有所有前驱状态相同时继续保留 PSTATE，否则在 join 前统一到 canonical。

### 6.3 迁移顺序

1. 让 region entry、DirectLinkFlagsBypass 和静态 `SetLocation` 共同产生 `EdgeFlagsState`。
2. 先覆盖 full-NZCV 与 overwrite-first 目标，再覆盖连续 partial mask。
3. 最后处理不连续 partial mask 和混合 carry polarity。
4. 所有生产路径迁移后删除重复的 flags bypass 表示，保留一个契约模型。

### 6.4 禁止路线

- 不把全局 inverted-carry ABI 翻为默认；该路线已经显著回退。
- 不把 broad flags liveness 或 full-elim 直接扩到所有边。
- 不通过无条件 `nzcv_dirty=true` 掩盖 entry 状态不明确的问题。

### 6.5 验收

- `_IO_new_file_xsputn`、`sqlite3VdbeMemSetStr` 和 printf fragments 的 `mrs/cfinv/bfxil` 总量下降。
- Direct、Inverted 和 Unknown 三种输入覆盖 overwrite-first、partial observer 和 mixed join。
- direct link、SMC unlink、signal recovery 和非 FlagM 路径保持正确。
- 不允许为了消除一条 `CFINV` 在目标入口增加更大的通用 merge。

## 7. P1：snapshot-aware guest 值版本与宽度事实

### 7.1 问题定义

默认 GPR 已经接近全 pin，剩余 `mov/ldr/str` 的主体不是 GetHost/SetHost 本身，而是：

- guest 寄存器之间的真实复制。
- fixed-home overwrite 前保留旧版本。
- fault/signal 可见状态的发布。
- helper 边界同步。
- 32 位写零扩展和窄值在观察点前的架构语义。

此前 multi-use snapshot reuse 和直接 pinned immediate publication 虽然缩小形态，但改变了 CRC，说明局部 alias whitelist 无法表达旧值观察关系。

### 7.2 设计

在 IR 优化与寄存器分配之间维护精简的 `GuestStateMap`：

| 信息 | 含义 |
|---|---|
| guest slot | GPR、XMM 或 flags 架构槽 |
| value version | 最近一次架构写对应的 SSA 版本 |
| resident location | fixed home、普通 host register 或已发布内存 |
| width facts | known-zero high bits、known-sign extension、有效低位宽度 |
| observation status | 在下一个 fault/helper/external edge 前是否必须可恢复 |

基本规则：

- 同一 value version 才能复用 resident value；寄存器编号相同不代表版本相同。
- fixed-home 被新架构版本覆盖后，仍存活的旧版本必须有独立位置或明确 snapshot。
- 可能 fault 的指令执行前，snapshot plan 必须能恢复该时刻的全部架构可见值。
- 32 位 guest 写只有在 snapshot 已表达高 32 位为零时，才能省略物理 self-extension。
- CFG join 仅在所有前驱的 value version 和 width facts 一致时保留事实，否则退化为较弱事实或规范化值。

### 7.3 实现边界

- 新的 guest-state analysis 模块只计算版本、位置和事实，不发射 ARM64 指令。
- 现有 GPR/FPR coalescer 消费分析结果，不各自维护第二套版本判断。
- 现有 uniform/fault snapshot pass 负责把观察点需要的版本转换为保存计划。
- ARM64 operand、memory 和 flags emitter 只根据计划选择 W/X、`STRB/STRH`、extended address 或显式 bridge。

### 7.4 分阶段落地

1. 只追踪 pinned GPR 的 full-width value version，替换当前最保守的局部 fixed-home lifetime 判断。
2. 接入 fault snapshot，证明旧版本不会被后续观察后再允许跨多 use 复用。
3. 加入 `KnownZeroAbove(8/16/32)` 和 `KnownSignExtended(8/16/32)`。
4. 让 memory operand、narrow store、compare 和 branch consumer 消费 width facts。
5. GPR 路径稳定后再复用到 XMM scalar lane；不同时重写 GPR 和 FPR 分配器。

### 7.5 验收

- `sqlite3DefaultRowEst` 的 `uxth/lsr/mov` 和 `pcache1TruncateUnsafe`、`powerOfTen` 的 `ubfx/lsr/mov` 成组下降。
- fault 前的 32 位写、窄 memory RMW、helper fault 和 signal context 均恢复正确架构高位。
- fixed-home old/new version 交错、diamond join 和循环 backedge 有定向覆盖。
- 若只能通过不断增加形态白名单获得收益，则停止该候选并回到版本模型补足观察关系。

## 8. P1：continuation 覆盖与热冷代码分离

### 8.1 当前边界

continuation 不是空白机制。现有实现已有：

- call-kind `BL` 与 call-entry host continuation。
- continuation stack/RSB 交互。
- 4 MiB fault-backed indirect-L1 safepoint。
- return target 保留、SMC invalidation 与 unlink 测试。
- 部分路径上的延迟 `current_loc` publication。

剩余问题是不同 exit kind 仍有各自的 publication 和冷路径布局，且当前 emitter 会把部分冷 stub 放在热 terminal 后面，阻止安全的 hot-block scheduling。

### 8.2 设计

1. 用统一 continuation contract 描述 guest target、host continuation、generation 和 miss/invalidation 恢复入口。
2. static call、indirect call、return 和 direct link 复用同一 publication 顺序；任何可能进入 dispatcher/signal 的路径都必须先满足可观察状态。
3. emitter 先生成所有 hot block，再集中生成 cold publisher、fault recovery、cycle/halt 和 unlink stub。
4. 热 terminal 只保留到 cold label 的最短条件分支，冷 stub 共享公共尾部。
5. 在热冷分离完成前不做任意 trace scheduling；完成后才依据 fallthrough 和权重重新排序 hot blocks。

### 8.3 验收

- static/indirect call、return hit/miss、guard fault、SMC invalidation 和 code-cache reuse 共享一致的 continuation 行为。
- 不允许简单把 `current_loc` store 移到 shared call entry；必须由契约证明 publication 顺序。
- printf fragments、`_IO_new_file_xsputn` 和 `powerOfTen` 的 `b/bl/adr/ldr/str` 冷路径税下降。
- 热代码缩小后用短配对 wall-time 检查分支预测和 I-cache，避免“静态变小、执行变慢”。

## 9. P2：精确 helper ABI

### 9.1 设计

在现有 `CallLambda`、`UniformEffectId` 和 helper ABI 上增加静态 `HelperCallContract`：

- 精确 GPR/FPR clobber 集。
- 是否读取或写入 guest state。
- 是否观察或破坏 host NZCV。
- 是否可能 fault、回调或重入 dispatcher。
- 是否为 leaf，以及是否需要完整 fault snapshot。

backend 根据 contract 只发布真正被 helper 观察或破坏的状态。未知 helper 继续使用规范保守 ABI，这是必要的正确性边界，不建立第二套可选运行时机制。

截至 `4bf7117`，GPR/FPR clobber、pinned-state、FPCR、preserve-all 与 uniform effect 已统一进入
ARM64 `HelperCallContract`；fault、callback/reentry 和 host-NZCV observer 仍保持保守，尚未开放精确
consumer。

### 9.2 选择规则

- 只从加权 opcode ledger 证明 helper snapshot/call tax 占主导的 root 开始。
- 优先纯 leaf、固定签名、无回调、无 guest-state 隐式访问的 helper。
- 不把 `PCMPISTRI EqualAny` 重新 outline；该路线曾缩小静态代码但使 warmed wall-time 回退。
- `preserve_all` 只作为已存在的编译器 ABI 能力，不以“保存更多寄存器”替代精确 clobber 描述。

### 9.3 验收

- helper 前后的 guest-state publication、flags merge 和 caller-saved pinned spill 成组下降。
- fault、回调、TLS 和平台 ABI 用例保持正确。
- 静态缩小但短配对 wall-time 明确回退的候选不合入。

## 10. P2：字符串循环与复杂 EA

该阶段只处理在前述 ABI 改进后仍存在的确定残差：

- `__strcmp_sse42`、`__strcspn_sse42`：保持内联，优化循环 fallthrough、重复 mask/extract 和热冷特殊分支。
- 复杂 EA：只覆盖有足够动态权重的 bias、32 位 wrap 或可证明等价的复合地址；简单 base+imm/index/scale 已经完成。
- 绝对常量：现有 relocation 约束下真实需要两段 materialization 的地址不再尝试 ADRP/literal 变体。

该阶段不能先于 hot/cold emission 分离，否则 block 重排会被紧邻 terminal 的冷 stub 约束。

## 11. 实施顺序

| 阶段 | 交付物 | 主要目标 root | 晋级条件 |
|---|---|---|---|
| A | FunctionEntryContract 与入口 provenance | `freeSpace` | 元数据路径不改变代码；入口所有权测试完整 |
| B | 首类 external veneer 与 code-object 多入口 invalidation | `freeSpace` | 目标 root 实质缩小，三语料正确，无 top-20 增长 |
| C | EdgeFlagsState 统一 region/direct/static entry | `xsputn`、Vdbe、printf | flags 指令成组下降，混合 join 正确 |
| D | GuestStateMap 与 GPR fault snapshot 版本 | DefaultRowEst、pcache、powerOfTen | move/width bridge 成组下降，fault/CRC 正确 |
| E | width facts 跨边传播 | 窄值热点 | 不增加额外 publication，不扩大未知 ABI 假设 |
| F | continuation contract 收口与 hot/cold emission | printf、xsputn、powerOfTen | 热路径缩小且短 wall-time 不回退 |
| G | 精确 helper ABI、字符串和复杂 EA | helper/string roots | ledger 证明收益来源，静态与短 wall-time 同向 |

每个阶段独立提交。一个提交只引入一个状态模型或一个生产消费者，避免同时修改 frontend、RA、emitter 和 runtime 后无法归因。

## 12. 快速验收流程

详细命令复用 `docs/codegen-benchmark-fast-path.md`，本文只规定晋级门：

1. 本地构建和定向单元测试。
2. Orb 目标构建及与改动机制直接相关的测试组。
3. 同输入 SQLite static-only shape，对比 root 总数、总指令和目标 mnemonic。
4. `smallpt_wh_x64 4 8 6` 短跑，要求输出逐字节一致。
5. 短 CoreMark，要求 `crcfinal=0x382f`。
6. 使用 retained formal weights 做严格 PC/version join：host-weight coverage 不低于 99.9%，top-20 PC 全覆盖，禁止共同热点增长。
7. 只有 1–6 全部通过，才运行一个正式加权语料；不并行启动多个长基准或压力测试。

立即停止并回退候选的条件：

- PPM、CRC 或标准输出变化。
- host fault、wild PC、heap corruption、signal 124/134。
- 目标 root 未缩小，却增加了通用 entry/merge/publication 代码。
- common-PC 覆盖不足以解释收益。
- 依赖调试日志、临时路径或环境开关才能成立。

## 13. 测试与代码约束

- 测试只覆盖状态契约的分界：internal/external entry、version join、fault snapshot、SMC generation 和 continuation miss。
- 不为每个 opcode 复制相同测试；一个机制测试覆盖一个不变量。
- 临时 census/probe 完成归因后删除；最终树不保留调试输出、临时路径或新 env 开关。
- 新分析逻辑按职责拆到独立模块，emitter 文件只保留指令生成。
- 机制完全迁移后删除被替代的旧表示和分支，不保留双协议兜底。
- 提交作者保持 `swift_gan`，提交信息不带任务编号；按阶段本地提交，不主动 push。

## 14. 明确不再尝试

- 把 pin level 3/R15 当作当前主要差距来源。
- 无契约地把所有无条件 direct target 强制内部化。
- 只移动 `current_loc` store，而不表示 continuation/publication 顺序。
- 全局 inverted-carry ABI 或 broad flags full elimination。
- 依靠局部 alias whitelist 复用 multi-use fixed-home snapshot。
- 重新把 SSE4.2 EqualAny 内联循环 outline 成 helper。
- 仅扩大 function/region block window；冷块增多会放大总静态代码。
- 在 hot/cold emission 分离前进行任意 hot-block trace reorder。
- 对已经证明必须两段 materialization 的绝对地址重复尝试 ADRP/literal。

## 15. 第一批实际工作

第一批只做阶段 A/B：

1. 从 `freeSpace` 提取仍走外部 direct edge 的目标集合和 ownership 原因。
2. 扩展 `FunctionDecodeFrontier` 的结果，使每个候选明确给出 owner、边界、call-return ownership 和 dependency pages。
3. 引入最小 FunctionEntryContract，并只为一种严格候选生成 external veneer。
4. 把入口发布、direct link 和 SMC invalidation 纳入一个 generation 事务。
5. 用短 SQLite/smallpt/CoreMark 裁定；只有 `freeSpace` 确认缩小后才扩大覆盖。

该顺序能直接验证当前最大 root 的机制判断，同时为后续 flags 和 guest-state 版本化提供共同入口载体。

## 16. 落地进度

### 16.1 函数内已知无条件边

代码对象和发布路径复核后确认，当前 runtime 已经为每个已解码块发布
external/direct/pending-flags/call entry，并把全部块范围和入口纳入同一个 SMC ownership
事务。因此阶段 A/B 不再新增重复的 `FunctionEntryContract` 容器或第二层 veneer。

第一批生产消费者采用更窄的所有权证明：无条件常量跳转只有在目标已经存在于当前
HIRFunction 时才转为 `LinkBlock`。该目标由已有条件边、fallthrough 或入口创建，指令边界和
代码对象归属均已确定；未知目标、间接目标和需要新 split 的目标继续走规范 dispatcher 路径。

提交 `6e7c3d7` 的短门禁结果：

- SQLite main 公共根 `259,747 -> 255,517`，新增 72 个边界根共 286 条，完整总量
  `259,747 -> 255,803`；`freeSpace` `416 -> 409`，输出归一化后一致。
- smallpt `37,745 -> 37,132`，PPM SHA-256 保持
  `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`。
- CoreMark 20k `37,717 -> 37,160`，`crcfinal=0x382f`。
- Mac/Orb 的入口边界、late split、region ownership、CallLambda、大 CFG 和 SMC 定向测试通过。

该候选改变了 root 划分，不使用要求同 PC/version 的 retained-weight 估算；本阶段比较完整
static-only root 总量和正确性 oracle。下一阶段从版本化 flags edge ABI 的重复表示审计开始。

### 16.2 立即数双精度移位的分配后 funnel fusion

`powerOfTen` 的主要局部残差不是 pin 缺失，而是立即数 `SHLD/SHRD` 仍沿用动态计数路径：
计数 mask、补数、两条变量移位、零计数选择和 flags guard 均在计数编译期已知时保留。前端现在对
32/64 位有效非零立即数直接生成互补的 `LsrImm/LslImm/Or` 图，零计数直接保持目的值和旧 flags，
寄存器计数及 16 位未定义区间继续使用规范动态路径。

ARM64 后端在寄存器分配完成后识别相邻、单 use、等宽且移位量互补的图，把它发成一条
`EXTR`，并直接写最终 `Or` 的分配结果。这里没有增加首类 funnel IR：早期折叠会改变 fixed-home
publication 的 live range，曾在 SQLite 单线程短跑中触发确定性 heap corruption；分配后 fusion
保留原图的寄存器所有权，同时删除三条中间指令。matcher 与 emitter 均复证完整形态，并拒绝
共享 shift、局部 condition 和现有 narrow-extract fusion。

双精度移位随后通过 identity `Or` 保存 PF。flags-register ABI 下只为已确认的 funnel 结果保留
parity token，并把原始融合结果作为 token producer；不全局改变普通 `Or/Xor`。全局补齐 logical
token 虽能修正 PF，却使 SQLite 增长 592 条，已完整撤销。最终窄化方案的门禁结果：

- SQLite 精确公共集合 2,241 roots / 100% host 与 entry coverage，`264,582 -> 264,565`，仅
  `powerOfTen@0x408ee0` 变化，`194 -> 177`，无公共热点增长；相对 FEX 153 条的残差由 41 条降到
  24 条。
- SQLite `--threads 1` 返回 0，时间归一化输出逐字节一致。
- smallpt 保持 267 roots / 37,132 条，PPM SHA-256 保持
  `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`。
- CoreMark 20k 保持 299 roots / 37,160 条，`crcfinal=0x382f`。
- 32/64 位 SHLD/SHRD 六组立即数的 result、CF、PF、ZF、SF 与原生 x86 的 96 字节结果逐字节一致；
  临时差分 probe 已删除。Mac/Orb 的前端路径、fusion 拒绝边界、parity token 和 logical flags
  定向测试通过。

该阶段没有新增环境开关、诊断路径或运行时兜底。下一批继续按加权 root 残差选择
GuestStateMap/width facts 或 continuation hot/cold contract 的首个生产消费者，不做零收益的通用
flags ABI 扩张。

### 16.3 pinned GPR full-width value version 转移

`sqlite3DefaultRowEst` 的入口先执行 guest `RDI -> RCX`，随后覆盖 RDI，但多次内存访问仍从为旧
RDI 保留的普通 host 临时寄存器取地址。RCX 的 fixed home 此时已经保存同一个 full-width value
version，因此旧值不需要第二个物理驻留位置。

新的独立 ARM64 planner 追踪这一版本发布关系。它只接受零偏移、完整 64 位、不同 pinned home
之间的发布，枚举透明 full-width alias 的全部 use，并确认目标 home 在最后一个转移 use 前没有被
覆盖。caller-saved home 遇到 helper clobber 时拒绝。faulting memory use 不会结束该版本：发布后
source 和 target 的架构槽均有正确值，source 后续覆盖产生新版本，旧版本仍由 target home 保存，
现有 fault snapshot 可直接恢复两者。matcher 与 emitter 都复证同一计划。

首个 consumer 只让完整 value version 供后续 memory address 使用，不同时扩张到普通 ALU、窄值和
FPR。`sqlite3DefaultRowEst` 的入口由 `mov x9, x1; mov x23, x9` 收敛为 `mov x23, x1`，后续三次
load 直接读取 x23。短门禁结果：

- SQLite 精确公共集合 2,242 roots / 100% host 与 entry coverage，`264,573 -> 264,568`；5 个
  root 各减少 1 条，无增长。`sqlite3DefaultRowEst@0x40d240` `168 -> 167`，相对 FEX 120 条的残差
  降到 47 条。
- SQLite `--threads 1` 返回 0，时间归一化输出逐字节一致。
- smallpt 的短 oracle 保持 PPM SHA-256
  `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`；共同 root 发码不变。
- CoreMark 20k 保持 299 roots / 37,160 条，`crcfinal=0x382f`。
- Mac/Orb 的跨多个 faulting load 版本复用、目标 home 覆盖失效和既有 pinned snapshot 边界测试通过。

该阶段没有新增环境开关、诊断路径或运行时兜底。下一步继续在同一 root 中加入
`KnownZeroAbove(16/32)`，让 compare/select consumer 消费宽度事实；不把 full-width transfer
直接扩大成无法说明观察关系的通用 alias 规则。

### 16.4 pinned GPR 窄值宽度事实消费

窄 load 发布到 pinned GPR 后，原 planner 只允许扩展节点和少数低位 alias 使用该 fixed home。
优化器把低 32 位 alias 消去、让 `Select` 直接消费 `ZeroExtend32` 时，planner 会因 producer 多 use
拒绝整条发布链，后端随后为同一个已知高位为零的值保留普通临时寄存器并产生多次 move。

本阶段没有增加全局宽度格或新的 IR。pinned GPR planner 现在枚举窄扩展 producer 的完整 use set，
只为 publication 之后、精确同宽的 U32 ALU/Select consumer 建立 `(definition, consumer)` 固定 home
映射；低位 `BitExtract` 仍按原有单 use 规则证明。目标 home 在最后一个 consumer 前被覆盖，或 use
集合出现未审核的 consumer 时，整条计划拒绝。`Select` emitter 只在该精确映射存在时读取 pinned W
view，其余路径继续使用寄存器分配结果。

`sqlite3DefaultRowEst@0x40d240` 中的窄 load 从
`ldrh w10; mov w13, w10; mov w22, w13` 收敛为 `ldrh w22`；后续 shift 和 `csel` 直接读取 w22，
该 root `167 -> 164`，相对 FEX 120 条的残差降到 44 条。短门禁结果：

- SQLite 精确公共集合保持 2,242 roots / 100% host 与 entry coverage，`264,568 -> 264,255`
  （`-313` / `-0.118306%`）；175 个 root 缩短，0 个增长。时间归一化输出逐字节一致，SHA-256 为
  `610f791a79bff6436ec37a0b7863aa9a18d26785233630ed942a2d94ab938a93`。
- smallpt `4 8 6` 保持 PPM SHA-256
  `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`；250 个共同 root 中 3 个
  共减少 4 条，无增长。
- CoreMark 显式 20k 保持 `crcfinal=0x382f`；299 个基线 root 全部覆盖，8 个共同 root 共减少 13 条，
  无增长。
- Mac/Orb 的 pinned 相关 25 个用例、97 条断言通过；新增边界只验证 Select 直接消费和目标 home
  覆盖失效。

该阶段没有新增环境开关、诊断日志、临时 probe 或运行时兜底。下一批继续从剩余加权 root 中选择
能共享同一状态事实的 compare/flags consumer，或转向 continuation hot/cold contract；不把
consumer allowlist 扩成通用寄存器别名系统。

### 16.5 full-width publication 的低位宽度视图

full-width SSA value 写入 pinned GPR 后，fixed home 已经承载同一 value version。原后端仍为后续
零偏移低位 `BitExtract` 分配普通临时寄存器，因此在 compare 和地址计算前生成 `lsr W, W, #0`。

本阶段新增独立的 ARM64 publication-view planner。它只接受 publication 之后、单 use、零偏移的
U8/U16/U32 `BitExtract`，且 consumer 必须是精确同宽的 `Add`、`Sub` 或 `Select`。planner 枚举
producer 的完整 ordinary use set；目标 fixed home 在最后一个 consumer 前被覆盖，caller-saved
目标跨 helper，或出现未审核 consumer 时整条计划拒绝。`SetHostGPR` 仍按原路径发布值，低位 alias
只在该证明成立时读取目标 W view，没有引入新的 IR、运行时协议或兜底路径。

`sqlite3DefaultRowEst@0x40d240` 中三个零位 `lsr` 被删除，后续 `Sub/Add/Sub` 直接读取已发布的 W
view，root 从 164 降到 161，相对 FEX 120 条的残差降到 41 条。短门禁结果：

- SQLite 精确集合保持 2,242 roots / 100% host 与 entry coverage，`264,255 -> 264,174`
  （`-81` / `-0.030652%`）；70 个 root 缩短，0 个增长。时间归一化输出逐字节一致，SHA-256 保持
  `610f791a79bff6436ec37a0b7863aa9a18d26785233630ed942a2d94ab938a93`。
- smallpt `4 8 6` 保持 267 roots，`37,128 -> 37,126`，无增长；PPM SHA-256 保持
  `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`。
- CoreMark 显式 20k 保持 300 roots，`37,157 -> 37,156`，无增长；`crcfinal=0x382f`。
- Mac/Orb 的 pinned 与 full-width publication 定向测试通过，覆盖正向复用和目标 home 覆盖失效。

该阶段没有新增环境开关、诊断日志、临时 probe 或运行时兜底。后续宽度事实仍按 value version 和
观察边界扩展，不把 fixed-home view 变成全局寄存器别名。

### 16.6 SelectZero 结果的 pinned 低位直接发布

`Div -> MulSub -> SelectZero -> ZeroExtend32To64 -> SetHostGPR` 一类链路中，寄存器分配仍把
`SelectZero` 结果放入普通临时寄存器，随后再 move 到 pinned GPR。这里真正需要发布的是选择结果的
低 32 位，而不是重新构造一个 guest value version。

本阶段新增独立的分配后 publication planner。它只接受零偏移、非 dead、未由 RA 合并的 pinned
`SetHostGPR`，并要求来源精确为 `ZeroExtend32To64(SelectZero(...))`。planner 保留原有 IR 和 RA
生命周期，枚举 extension 以及 publication 之后零偏移 U32 alias 的完整 ordinary use set；producer
到 publication 之间存在 fault、helper、架构观察或目标 home 覆盖时拒绝，publication 到最后一个
映射 use 之间存在目标覆盖，或 caller-saved home 跨 helper 时同样拒绝。证明成立后，`EmitSelectZero`
直接向目标 W home 发出 `CSEL`，extension 和 `SetHostGPR` 不再生成指令，后续已证明的低位 consumer
复用同一 W home。emitter 会重新执行 matcher 并校验计划，避免准备阶段和发射阶段的事实漂移。

短门禁结果：

- SQLite 候选重复两次均正常退出，保持 2,239 roots，shape 完全一致；与 fresh HEAD 的稳定公共集合
  比较，32 个 root 共减少 99 条，0 个增长。`pcache1TruncateUnsafe@0x412f50` 从 188 降到 182，
  相对 FEX 141 条的残差降到 41 条。时间归一化输出逐字节一致，SHA-256 为
  `3f68fab47764cf6ac36bf315944cb6746d318e0b665f7cbc1317b2a262c6de06`。
- SQLite 的 `0x413270`、`0x48aa47`、`0x505da0` 会在同一 baseline 机制下形成不同的可选 root；
  因此本阶段不宣称 formal 99.9% join 通过，只报告重复候选稳定且排除两个大于 20 条的已知可选
  variant 后的成对公共集合。该波动不是候选新增的 code growth。
- smallpt `4 8 6` 保持全部 267 个 root，`37,126 -> 37,125`，无增长；PPM SHA-256 保持
  `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`。
- CoreMark 显式 20k 两次均保持 `crcfinal=0x382f` 和 299 个稳定 root；共同 root
  `37,146 -> 37,145`，无增长。基线中另有一个 10 条的运行时可选 root，不计入机制收益。
- Mac/Orb 的 pinned 定向测试均通过 101 条断言、27 个 case，新增边界覆盖直接 W-home 发布、
  目标覆盖拒绝和 publication 前 fault 拒绝；Mac/Orb 的 x86 `div/idiv` fuzz 同时通过。

热冷 block-tail 延迟发射和通用零常量 SSA 删除两个原型都触发了可重复的 SQLite heap corruption，
已完整删除。前者需要先定义可序列化的 cold-stub 编译状态 contract，后者会改变 RA/fixed-home
生命周期；在对应机制建立前不再按局部 peephole 重试。交付中没有保留 probe、环境开关、调试路径
或兼容兜底。

### 16.7 full-NZCV overwrite-first edge flags ABI

第一批版本化 flags edge ABI 已替换原先只记录“目标是否有 pending entry”的布尔兼容判断。
backend-neutral `EdgeFlagsState` 记录有效 NZCV mask、carry polarity、producer 和 packed-flags version；
`EdgeFlagsTargetContract` 记录目标在观察或 fault 前覆盖的位及 `AdvancePC` 提交边界。region 分支证明、
direct link、静态 forward、LinkManager、SMC unlink 和磁盘缓存现在使用同一兼容判定，缓存格式升至 v15。

首批生产 consumer 只发布当前确实存在来源的 full-NZCV 状态，并允许目标在任何观察前仅以
`SaveFlags(..., Flags::NZCV)` 覆盖 NZCV；不再错误要求同时覆盖已由 packed flags 保存的 PF/AF。
partial overwrite 合同可以被分析和序列化，但在 partial source state 与 polarity/version join 完成前不会
发布无消费者的 pending/call entry，也没有保留旧的宽松兜底。

短门禁结果：

- fresh `a241d60` 同提交 smallpt `4 8 6` 静态集合保持 279 roots、100% host/entry coverage 和
  top-20 `20/20`，`49,590 -> 49,498`（`-92` / `-0.185521%`），无增长 root；PPM SHA-256 保持
  `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`。
- Mac 通过 1,107 条非压力 direct-link、46 条 region flags、31 条 NZCV 和 11 条 indirect fault
  continuation 断言；Orb 对应通过 831、46、31 和 11 条断言。
- 生产 direct/static 测试明确使用只写 NZCV 的目标，并覆盖兼容链接、SMC invalidation 后重新发布、
  不兼容目标恢复 merge，以及 v15 source/target contract 往返。
- 修正 `flags_merge_test` 逐字节滑窗解码 ARM64 指令的测试错误，改为按 VIXL 指令宽度前进；临时
  disassembly 捕获已删除。

该阶段没有新增环境开关、诊断日志、运行时探针或调试路径。下一批在这个单一合同上增加连续 partial
mask source、Direct/Inverted polarity 和 packed-flags version join，不重新引入独立 bypass 协议。

### 16.8 连续 partial-NZCV edge contract

direct link 和静态 forward 现在能把连续 partial-NZCV merge 注册为可撤销 bypass。目标只要在任何
观察或 fault 前覆盖全部 incoming 有效位并在 `AdvancePC` 提交，就发布同一个 pending entry；full
source 不会进入只覆盖 partial mask 的目标。动态 indirect-call continuation 仍只发布 full-NZCV
兼容入口，避免无目标合同的 pending-call L1 把 full source 送入 partial target。

`EdgeFlagsTargetContract` 同时记录目标要求的 packed-flags version，source/target 版本必须相等；磁盘
缓存格式升至 v16。带 C 的生产 source 仅在 FlagM canonical carry 开启时声明 `Direct`，其他情况保持
`Unknown`，不会把未知或 inverted carry 当成 direct。首个生产消费者覆盖连续 `NZ`，非连续 mask
继续走规范 merge。

短门禁结果：

- AArch64 生产测试覆盖 full/partial 静态 forward、partial pending entry、链接后跳过三条 merge、
  SMC invalidation 恢复原指令，以及 partial target 不发布 pending-call entry。
- Mac 通过 1,166 条非压力 direct-link、46 条 region flags、31 条 NZCV 和 11 条 indirect fault
  断言；Orb 对应通过 867、46、31 和 11 条断言。
- fresh `51951e3` 同提交 smallpt `4 8 6` static-only 保持 279 roots、100% coverage、top-20 `20/20`
  和 `49,498` 条，PPM SHA-256 保持
  `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`。fallback merge 字节必须
  留给不兼容目标，因此该指标验证无静态增长，不计作动态 bypass 收益。
- SQLite 和 counter-based smallpt 在 8 秒门限内未正常结束，已停止且不作为收益证据；没有延长或
  启动压力测试。

该阶段没有新增环境开关、诊断日志、运行时探针或兼容兜底。下一步为 inverted carry 建立可证明的
source provenance，或转入 continuation contract 与 hot/cold emission；不让 `Unknown` 极性宽松匹配。

### 16.9 函数级热冷 emission 分离

提交 `888c3db` 把 block terminal 与 cold-path emission 拆成两个阶段。每个 hot block 完成后，
`BlockColdPathPlan` 移出该 block 的 backedge、cycle exit、fault recovery、VecNaN、flags audit 和 density
状态；函数全部 hot block 生成完毕后，再按 block 顺序集中发出 cold stubs。独立 block 翻译仍立即消费
同一 plan，不建立第二套 emitter 协议。新的布局测试直接解码首个 block 的 NaN guard，验证其 cold
target 位于后续 hot block 之后。

延迟 cold emission 暴露了一个与本阶段无关的既有 indirect-L1 边界：guest target 为零时，空表项的
零 key/零 value 会被 key-only 热命中误认为可执行地址。提交 `5dcaefb` 在设置 invalid value 时同步初始化
零 key 的 sentinel value，使 target 0 进入既有 miss trampoline，不增加 JIT 热路径指令或运行时分支。

短门禁结果：

- Mac 的布局、continuation empty/mismatch、invalidated indirect call、SMC L1 redirect、region cycle、
  observing-exit flags、cycle reason、call-kind continuation、indirect target preservation 和 AFP NaN
  定向测试通过 209 条断言；`[direct-link][production]`、`[continuation]` 和非压力 `[smc]` 分组分别
  通过 840、29 和 793 条断言。
- fresh `63ad1de` 与 `888c3db` 的 smallpt `4 8 6` static-only 对比保持 279 roots、100% host/entry
  coverage、top-20 `20/20` 和 49,498 条总指令；PPM SHA-256 保持
  `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`。总 emission 大小不变符合
  预期，本阶段改变的是 hot/cold 空间顺序。
- SQLite `main/1` 两侧均在 8 秒门限退出 124，已停止；仅确认候选未出现 host fault、heap corruption
  或 wild PC，不作为覆盖或性能证据。
- Orb 在本阶段验证时 SSH 端口立即断开，未宣称远端门禁通过；恢复后需对 `888c3db` 补跑同一组定向
  测试。

该阶段没有新增环境开关、诊断日志、运行时探针、临时路径或兼容兜底。下一步统一 static call、
indirect call、return 和 direct link 的 continuation publication contract 与公共 cold tail；在这一合同
完成前不进行任意 trace scheduling。

### 16.10 call miss continuation publication contract

提交 `0fb3113` 引入 ARM64 `ContinuationContract`，把 `{x14 guest return, x30 host continuation}`
frame、push/pop 和 region traversal tag 收进同一 ABI。普通 call entry、pending-flags call entry 和 return
consume 不再各自手写 frame 操作。region linker 对已解析 call 仍进入 call entry；未解析、retiring、far
失败或 generation 失配的 call 使用 `CallMiss` traversal，trampoline 在进入 dispatcher 前只发布一次
原 site continuation。

indirect-call L1 的热命中序列保持不变。每个 call site 只在函数 cold 区生成 continuation 准备入口，
miss 或失效 target fault 在那里物化该 site 的 BLR 后继地址，再进入按 location register 共享的 publisher。
ordinary 和 pending-flags miss 都先发布 frame，再分别执行规范 L1 fallback 或 flags merge/current-location
publication。lookup guard fault 发生在 guest call 提交前，继续走既有 signal/terminal recovery，不错误压入
未发生的调用；return mismatch 仍重置栈并走普通 indirect fallback，也不会被当成 call miss。

短门禁结果：

- 新增生产测试让 static 和 indirect call 都先命中真实 `CodeMiss`，验证 frame 跨 host 往返保留；目标随后
  编译后从 canonical entry 执行 guest return，均回到原 source continuation 并把 RSB 恢复为空。失效
  indirect-call target 的 host exit 现在明确保留尚未 guest-ret 的 frame。
- Mac 的 `[continuation]`、`[direct-link][production]`、非压力 `[smc]` 和 `[indirect-l1]` 分组分别
  通过 57、785、706 和 33 条断言；guarded return stack 与 cold-path layout 通过 5 和 3 条断言。
- fresh `73082f5` 与 `0fb3113` 的 smallpt `4 8 6` static-only 配对均为 279 roots，PPM SHA-256 均为
  `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`。精确 host continuation 使
  17 个含 call-miss 冷入口的 root 共增加 50 条静态指令，`49,498 -> 49,548`；其余 262 个 root 不变，
  indirect-call 热命中 emitter 没有新增指令。单次 wall-time 为 `3.437s -> 3.464s`，只作一致性检查，
  不作为性能结论。
- Orb SSH 仍由 `198.18.0.190:22` 立即关闭，本阶段未宣称远端门禁通过。

该阶段没有新增环境开关、诊断日志、运行时 probe、临时源路径或兼容兜底。静态冷区增长来自当前
return PC 只能使用代码对象内部入口；P0 external veneer/多入口 ABI 完成后，可让冷 miss 使用可失效的
外部 return entry，届时再收回逐 call-site continuation 准备入口。continuation 剩余工作是把 generation
与 unlink/invalidation 纳入同一首类 contract，而不是继续扩展 traversal tag。

### 16.11 function entry provenance 与多入口代码对象 ABI

提交 `340247d` 把 function-level 编译原有的多入口能力从隐式发布循环收敛为首类 ABI。现有 backend
本来已经为每个已解码 HIR block 生成 canonical、direct-link、pending-flags、continuation 和
pending-flags continuation 入口，并由 LinkManager 按 allocation owner/generation 管理；本阶段没有
重复生成第二套入口，而是补齐此前缺少的来源证明和统一发布事务。

`FunctionDecodeFrontier` 现在为 candidate、accepted 和 rejected split target 保留稳定 provenance，包含
target、唯一 owner 范围、重解码前的 guest dependency、call-return ownership 以及明确 rejection reason。
provenance 随 HIRFunction 进入 backend，不在 x86 frontend 局部对象销毁时丢失。`FunctionEntryContract`
统一记录入口 kind、canonical/pending flags 要求、fixed-home 要求、continuation frame 要求、guest 范围、
dependency ownership 和来源；`FunctionEntryPublisher` 负责 LinkManager target generation、canonical L2、
call L1 与 pending-call L1 的一次性发布。

只有 accepted split、普通 decoded block 和 function root 可进入 LinkManager。ambiguous owner、call-return
ownership、owner reset 失败或 boundary mismatch 的 split 仍保留 canonical L2 正确性入口，但不会发布
direct/pending/call target。该 linkable 属性写入 disk-cache v17，恢复后的代码对象不会把被拒绝的 split
重新升级为可链接入口。函数在线编译与磁盘恢复复用同一 publisher；同一 allocation 的 generation 与
SMC invalidation 仍由既有 LinkManager/SmcTracker owner transaction 一次性撤销。

短门禁结果：

- `[function-entry]` 通过 42 条断言；`[direct-link][jit-cache]` 通过 355 条断言，其中 v17 跨进程恢复
  聚合为 259 条；`[region-edges][production]` 通过 42 条，覆盖函数三个 external entry 的同 owner SMC
  失效。
- 本阶段修改前 binary 与 `340247d` candidate 的 smallpt `4 8 6` static-only 配对均为 279 roots、
  49,548 条 host 指令、100% PC/version/top-20 coverage，delta 为 0。最终 candidate 单跑为 3.415s，
  PPM SHA-256 仍为 `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`；elapsed
  只作一致性检查。
- 没有新增运行时 env 开关、诊断日志、probe、临时源路径或旧机制兜底；只运行了 8 秒上限的
  static-only screen，没有压力测试或正式长基准。

P0 当前剩余的是 terminal-only/有非 canonical live-in 的 return entry 规范化；已有非空 decoded return
entry 的 call-miss consumer 已在下一阶段落地。

### 16.12 fault-backed external return continuation

提交 `34ff5a9` 把 indirect call miss frame 从 `{x14 guest return, x30 site resume}` 改为
`{x14 guest return, 0 external sentinel}`。call hit 仍由 `BLR` 产生精确 x30，并从 target call entry 发布原
frame；只有 key miss 或失效 target fault 在共享 cold publisher 写 external sentinel。callee guest return
沿用原来的 `LDP/CMP/B/BLR` 热序列，sentinel 的 `BLR 0` 由 fault metadata 转到 generation-aware
indirect L1/L2 external entry，因此正常 return 没有增加 tag test 或分支。

原来每个 indirect call site 的独立 miss/resume label、`ADR x30,resume` 和共享 publisher branch 已完整
删除。cold plan 按 location register 分为 call miss、guest-key mismatch 和 external continuation fault：
call miss 发布 external frame，key mismatch 明确清空不可信 RSB，external fault 只消费当前 frame 并保留
外层 frame。旧 `ContinuationMiss` signal reset 机制随之删除，disk-cache v18 持久化新的
`ExternalContinuation` recovery kind。

生产验证覆盖：

- static/indirect call 均覆盖 `CodeMiss -> 后编译 target -> guest return`，并增加一层外部 frame；indirect
  miss frame 的 continuation word 必须为 0，return 后只弹出当前 frame。call-return external entry 的
  LinkManager generation 非零，source owner 失效会同时清除该 return L2 entry。
- 失效 indirect-call target 先留下 external frame；随后 source owner 失效，另一个 return driver 消费
  frame 后得到规范 `CodeMiss` 而不是跳旧 host PC，编译 replacement return entry 后继续成功。
- Mac `[continuation]`、`[direct-link][production]`、非压力 `[smc]`、`[indirect-l1]` 和 guarded return
  stack 分别通过 105、825、742、33 和 5 条断言；disk-cache v18 serializer 通过 63 条断言。

短 smallpt `4 8 6` static-only 保持 279 roots 与 canonical PPM SHA-256
`a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`，host 指令从
`49,548 -> 49,520`，减少 28 条（`-0.056511%`）。最终 candidate 为 3.572s，只作一致性检查。

曾尝试把所有 terminal-only call-return block 直接发布为 external entry，smallpt 在 8 个 root 后出现
PageFatal；单变量撤回后完整短跑恢复。该路径没有保留：terminal-only entry 必须先有 RA/live-in
canonicalization 证明，不能依赖测试中的空 `ReturnToHost` 形态。阶段末没有 probe、调试 env、日志、
临时源路径、兼容兜底或长基准。

### 16.13 helper call state contract

提交 `4bf7117` 把 `EmitHostCall` 和四个 pinned GPR planner 中分散的 helper ABI 判定收敛到独立
`HelperCallContract`。contract 从 direct/indirect `Lambda`、`HelperABI`、`HostFpEffect`、
`HostRegisterEffect`、`UniformEffectId` 和编译器实际支持解析出 GPR/FPR clobber、argument snapshot、
preserve-all leaf、FPCR transparency、general-register-only 与 pinned-state preservation。CallLocation、
CallDynamic、X87 和专用 SSE4.2 helper 继续得到 opaque contract。

`EmitHostCall` 的 snapshot 选择和参数 reload 现在消费同一 contract，替换原来三组重复布尔条件。首个
跨指令 consumer 只允许有汇编保存证明的 `PreservesPinnedState` helper 保留 x3-x9 pinned value
version；x0-x2、x11、x16/x17、未知或间接 helper 仍判定为 clobber。resident string wrapper 明确保存
x3-x15、q16-q31 和 x30，因此该权限不是基于 helper 地址白名单猜测。

门禁结果：

- contract、pinned consumer、helper metadata/uniform effect、resident XMM snapshot、AFP transparent
  helper 和 CallLambda interaction 共通过 1,155 条断言。
- smallpt `4 8 6` 保持 279 roots、49,520 条和 canonical PPM SHA-256
  `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`，最终单跑 3.298s。
- SQLite 两臂均在 8 秒上限结束；714 个公共 root 指令逐条相同，candidate 只因截断时序多到达 32 个
  root，因此不作为收益或回退证据。
- REP MOVS fuzz 在旧屏障与 candidate 上都出现同类 Unicorn/flags baseline mismatch，分别为 416 和
  397；该环境结果不作为门禁，也没有为通过它加入例外。

本阶段不宣称宏观代码密度收益。fault、reentry/callback 和 host-NZCV effect 没有足够静态来源，继续
fail-closed；没有新增 env 开关、日志、probe、临时路径或运行时兜底。

### 16.14 canonical external CFG root

提交 `4b9e67e` 补齐函数中部入口缺失的 frontend 状态边界。x86 frontend 对尚未成为当前 CFG block 的
无条件直接目标记录 `ExternalDirectLink` side table，但继续生成原有 `SetLocation + ReturnToDispatch`，
未晋级的普通直接跳转不改变 IR 和 host code。`FunctionDecodeFrontier` 只选择至少三个显式直接来源共享、
位于唯一已解码 owner 内部、没有 call-return ownership 的目标；收益门槛与边界正确性判定彼此独立。

晋级目标会让原 owner 在精确边界重新解码，并以 `ExternalLinkBlock` 结束 prefix；目标则由新的 decoder
从 canonical frontend 状态独立解码。`HIRFunction` 的 RPO 现在依次遍历主 CFG 和 canonical external
roots，两个 root 不建立 HIR edge，因此 RA、flags local value 和 width/live-in 事实不会跨 root 继承。
accepted provenance 明确记录 `external_root`，被 reset 的 source 同时删除旧 direct-link side-table 记录，
避免重解码后残留过时来源。

ARM64 backend 对同一代码对象中的 `ExternalLinkBlock` 先完成 dispatcher 级状态提交，再直接进入目标的
published entry；不注册可失效 direct-link site，也不经过 L1/L2 dispatcher。向后 external edge 仍保留
fault-backed interrupt poll，fault recovery 使用目标 guest location。解释器对同一 terminal 更新
`current_loc`，因此 JIT 与非 JIT 使用相同的多 root 语义。

短门禁结果：

- `[function-entry]` 通过 66 条断言 / 6 个 case，其中生产执行验证两个 root 共享同一 code owner 和
  region、目标 entry 可执行，并且 allocation 内没有 direct-link site；`[direct-link][production]` 通过
  860 条断言 / 14 个 case。
- 非压力 `[smc]` 通过 767 条断言 / 11 个 case，`[continuation]` 通过 105 条断言 / 4 个 case。
- 与 `9455952` 的同配置 Debug 基线配对，smallpt `4 8 6` 两侧均为 279 roots、49,520 条 host 指令，
  PPM SHA-256 均为 `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`。
- SQLite guest 的静态反汇编确认 `freeSpace` 内 `0x449b0c` 有七个无条件直接来源，`0x4498c9` 与
  `0x449a5b` 各一个；当前三来源门槛只晋级前者。Debug static-only 两侧均在 8 秒停止，candidate 未到达
  `freeSpace`，因此本阶段不宣称该 root 已缩小，后续只补同提交的 Release/Orb 短账，不扩大覆盖面。

本阶段没有保留探针、诊断日志、env 开关、临时源路径或旧协议兜底，也没有运行压力测试或长基准。
P0 剩余项收窄为 `freeSpace` 的 Release 代码量验收，以及 terminal-only/call-return root 的显式
live-in canonicalization；后者仍不从普通 split root 的通过结果外推。

### 16.15 call-owned canonical external root

提交 `a83b654` 将 canonical external root 扩展到唯一 owner 内、split 点位于 call 之前的
call-owned block。`FunctionDecodeFrontier` 仍在 provenance 中保留 call-return ownership，但不再把它
作为拒绝原因。`ResetDecodedBlock` 只在 return block 没有其他 owner 时解除旧关系；目标 root
从 canonical frontend 状态重解码到同一 call 后，`RegisterCallReturn` 重建 owner 和 return-target
标记。这不是保留两套 ownership 协议，ambiguous owner 和 missing owner 仍 fail-closed。

Release 短账确认 `freeSpace` 的 `0x449b0c` 已真正晋级：7 个原始 direct source 与 1 个
owner-prefix 边共生成 8 个 `ExternalLinkBlock`，decoded block 数从 37 增至 38。同配置
SQLite `main/size1` 保持 2,285 roots，host 指令总数 `370,399 -> 367,040`（`-3,359`），
`freeSpace` `482 -> 471`（`-11`），程序正常完成。smallpt `4 8 6` 保持 279 roots 和
canonical PPM SHA-256，host 指令 `49,520 -> 49,265`（`-255`）。

Mac 通过 `[function-entry]` 70 条断言 / 6 个 case、`[direct-link][production]` 867 / 14、
非压力 `[smc]` 773 / 11、`[continuation]` 105 / 4，以及 glibc 72-block 定向用例 38 / 1。
Orb 本阶段未验证，不宣称远程门禁通过。没有运行长基准或压力测试，临时诊断、
probe、env 开关和迟绑定原型均未保留。

P0 的 `freeSpace` Release 收益门禁已闭合。terminal-only 且含非 canonical RA/live-in 的 return
entry 仍未开放；它与本阶段“从函数内部精确边界重放完整 call”的条件不同。

### 16.16 非连续 partial-NZCV edge contract

提交 `8e0927d` 去掉 emitter 中仅允许连续 NZCV mask 进入 flags bypass 的阶段门槛。
`EdgeFlagsState` 和 `EdgeFlagsTargetContract` 原本已能表示任意合法 mask；现在只要 source
是 well-formed pending PSTATE，target 在任何 observer/fault 之前覆盖全部 incoming 位、在
`AdvancePC` 提交且 packed-flags version 一致，LinkManager 就可以使用原有可撤销
patch 跳过整段 merge。过时的 `HasContiguousPendingPState` 表示和其唯一生产 gate 已删除。

生产定向用例新增 `N|C` 非连续 source/target，验证 direct/static link 后分支跨过完整
可变长 merge，target invalidation 后恢复原始 merge 首指令；partial target 仍不发布无目标
contract 的 pending-call entry。Mac 通过 165 条 direct-link flags、821 条 production direct-link、
42 条 production region-edge 和 704 条非压力 SMC 断言。

Release 同源 A/B 短账保持 smallpt `4 8 6` 的 279 roots / 49,265 条 host 指令和
canonical PPM SHA-256；SQLite `--size 1 --testset main :memory:` 两侧均为 2,187 roots /
356,265 条且正常完成。这符合本阶段不删除 incompatible-target/SMC fallback 字节、只改变
compatible link 动态执行路径的设计。没有运行长基准或压力测试，也没有保留 probe、
env 开关、诊断路径或兼容兜底。

EdgeFlags 剩余高优先级项是 inverted carry 的可证明 source provenance、mixed-polarity/
multi-predecessor join，以及用同一 target contract 取代 region 内独立的
`SuccessorCoversIncomingNzcv` 判定。

### 16.17 mixed region edge-flags join

提交 `d24e946` 把 region 内部边的 flags 决策从 `translator_region.cpp` 拆到独立的
`translator_edge_flags_join.cpp`。原先的条件 terminal 只有“两个 successor 都可以 defer”与
“两条边都先 canonical merge”两种结果；现在 `RegionFlagsJoinPlan` 能表示 mixed join。
当且仅当 source 携带 full NZCV、canonical arm 正好是无 cycle/poll 的布局 fallthrough、
compatible arm 可直达，且存活 PF/AF token 会被 compatible target 在观察前全部覆盖时，
条件分支先进入 compatible target，只在 canonical fallthrough 上调用已登记的 token-aware
shared merge trampoline。其他布局继续使用原先的单次 merge，不以增加静态分支换取局部收益。

external/direct-link target contract 同时把原来的 `observes_before_commit` 布尔值拆成
4-bit `observed_nzcv_mask` 与独立 `barrier_before_commit`。只要 source 携带位与 target
提前观察位不相交，且所有 incoming 位在 fault/helper barrier 前被覆盖，partial observer
不再让无关 mask 整体退化。该 ABI 写入 disk-cache v19。same-allocation region edge 仍使用
snapshot-aware 内部观察边界，不把 external entry 的 fault-sensitive 规则错用到内部边。

生产定向用例覆盖一个 successor 先观察、另一个 successor 覆盖全 flags 的 mixed join，
验证结果与 canonical 路径一致，并直接检查 host 条件分支位于 canonical merge 之前。
Mac 通过 48 条 region-flags、42 条 production region-edge、167 条 direct-link flags、794 条
production direct-link、302 条 jit-cache、682 条非压力 SMC 和 105 条 continuation 断言。

Release 同源 A/B 完全一致：smallpt `4 8 6` 两侧均为 279 roots / 49,265 条，
PPM SHA-256 保持 `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`；
SQLite `--size 1 --testset main :memory:` 两侧均为 2,187 roots / 356,265 条且正常完成。
被拒绝的中间版本曾因错用 external fault 边界使 smallpt 增长 878 条，另一个未登记
outline site 在 12 秒门限内被检测为自环；两条路径均已删除。没有运行长基准或压力测试，
也没有保留 probe、env 开关、诊断日志或兼容兜底。

EdgeFlags 剩余项继续收窄为非 FlagM 路径的 Direct/Inverted/Unknown source provenance，以及
无法利用 canonical fallthrough 的多前驱 mixed-polarity join；后者需要可共享且不增长静态代码的 entry veneer。
