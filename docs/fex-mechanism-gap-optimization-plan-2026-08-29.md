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

### 16.18 non-FlagM edge carry provenance

提交 `0caec7c` 让 `EdgeFlagsState::carry_polarity` 从仅有 FlagM `Direct` 与其他 `Unknown`，
扩展为可从现有 x86 `carry_inverted` publication 证明的三态 source contract。新的
`EdgeCarrySourceState` 在每个 HIR block 进入时清空；ARM64 `StoreUniform` 只在目标精确为
U8 `ThreadContext64::carry_inverted` 且 value 是常量 0/1 时发布 Direct/Inverted。动态值、
错误宽度或非 0/1 常量立即退回 Unknown。FlagM canonical carry 仍优先解析为 Direct；
incoming mask 不含 C 时仍必须为 Unknown。

该追踪只更新 JIT 编译期 metadata，不发射 host 指令、不增加 runtime branch，也不新建
第二个极性存储。生产 static-forward 用例现在覆盖 full-NZCV Direct、NZ Unknown、
non-contiguous `N|C` Direct 和关闭 FlagM 后的 `N|C` Inverted；四种 source 均进入既有
LinkManager contract，并覆盖 link 与 target invalidation 恢复。

Mac 通过 200 条 direct-link flags、97 条 static-forward 生产断言、913 条 production direct-link、
360 条 jit-cache、48 条 region-flags、769 条非压力 SMC 和 105 条 continuation 断言。
Debug smallpt `4 8 6` 保持 279 roots / 49,265 条与 canonical PPM SHA-256。Debug SQLite 在
8 秒门限结束，不作为收益或回退证据。本阶段没有新增 env 开关、probe、诊断日志、
临时路径或运行时兜底。

EdgeFlags 下一步不再是补极性枚举，而是让无 canonical fallthrough 的多前驱 join 共享按
`{mask, polarity, version}` 区分的 canonicalizing veneer；只有 veneer 成本被多条边摊薄且总静态代码不增长时才晋级。

### 16.19 pinned GuestStateMap 生命周期基础层

提交 `81405ec` 新增独立的 ARM64 `GuestStateMap`，把 pinned GPR planner 原先各自扫描
`SetHostGPR`、helper clobber 和 fault/observation 窗口的逻辑收敛为同一个 block 分析对象。
full-width value transfer、publication low view 和 SelectZero publication 现在分别消费
`FixedHomeSurvives` 与 `PublicationWindowSafe`；`MayFaultOrObserve` 也由该对象提供唯一分类，
region 与其他既有 consumer 继续调用同一入口，不保留旧扫描作为兜底。

publication low view 同时从单 consumer 扩展为完整 use 集可审核的多 consumer。零偏移
U8/U16/U32 `BitExtract` 的每个普通 use 都必须恰好一次进入同宽 `Add`、`Sub` 或 `Select`，
目标 fixed home 必须存活到最后一个 use；任何额外 use、覆盖或 caller-saved helper clobber
都会拒绝整条计划。生产定向用例验证一个已发布低 32 位视图同时服务 `Sub` 和 `Add`，且不生成
`lsr W,#0`。

Mac Debug 通过 multi-use 3 条断言、fault/overwrite 边界 5 条、pinned 分组 104 条和
published 分组 37 条断言。Release 同源 A/B 的 smallpt `4 8 6` 两侧均为 279 roots /
49,265 条 host 指令，PPM SHA-256 保持
`a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`；SQLite
`--size 1 --testset main :memory:` 两侧均为 2,188 roots / 356,545 条且正常完成。本阶段建立
统一生命周期与观察边界，不宣称这两个短 workload 已产生净代码量收益。

该对象当前仍是 block-local 的 pinned fixed-home 基础层，不等同于第 7 节完整状态格。
下一阶段需要在同一对象上加入 guest slot/value version、width facts 和 fault snapshot，随后才允许
事实跨 diamond/backedge join 或扩展到 memory/XMM consumer。本阶段没有新增 env 开关、probe、
诊断日志、临时源路径、运行时分支或旧机制兜底，也没有运行压力测试或长基准。

### 16.20 non-fallthrough EdgeFlags canonical tail

提交 `6871978` 将 mixed region join 扩展到 canonical arm 不是布局 fallthrough 的情况。
当 source 携带 full NZCV、恰好一个 successor 接受 pending state、compatible arm 会在观察前
覆盖 incoming flags，且两条边都不是 cycle/cut 时，`RegionFlagsJoinPlan` 选择新的
`CanonicalTail`。compatible arm 保留布局 fallthrough 或普通本地分支；canonical arm 进入函数冷区
的 canonicalizing stub。

stub 按 `{target, mask, polarity, version, token}` 建键，相同状态和目标的多条边共用一个入口。
每个入口只生成 `ADR x17,target` 和到既有 region merge trampoline 的分支；full NZCV merge 体不在
代码对象中复制，PF/AF token 路径在 source 分支前物化到既有 token register。相比 source 内先做
三条 full-NZCV merge 再选择 successor，单个来源也不会增加静态指令，多来源会继续摊薄同一 stub。
cycle/cut 仍走原有 fault/poll 路径，不从冷 stub 绕过恢复协议。

生产用例新增 compatible-fallthrough 布局，执行验证关闭/开启优化时的 selector、guest flags 和 halt
结果一致，条件分支位于 merge 之前，并且新 tail 与原 canonical-fallthrough split 的总代码对象大小
相同。Mac Debug 通过 60 条 region-flags、929 条 production direct-link、105 条 continuation 和
788 条非压力 SMC 断言。

Release 同源短账保持 smallpt `4 8 6` 的 279 roots / 49,265 条和 canonical PPM SHA-256；
SQLite `--size 1 --testset main :memory:` 保持 2,188 roots / 356,545 条且正常完成。这两个 workload
没有命中新 tail，因此本阶段只声明机制覆盖和零回退，不声明宏观收益。没有运行长基准或压力测试，
也没有新增 env 开关、probe、日志、临时源路径或兼容兜底。EdgeFlags 后续只剩需要 exact-mask merge
trampoline 的 partial-mask shared tail，以及非零 packed-flags version 的真实 producer。

### 16.21 terminal-only canonical return entry

提交 `79bce76` 为无普通 IR 指令的 return connector 增加显式 live-in canonicalization。
`FunctionEntryContract::AnalyzeCanonicalTerminalEntries` 从 function root、accepted external root 和
唯一 call-return owner 开始，只接受常量 `LinkBlock/LinkBlockFast`，并在所有 predecessor 已经是
canonical connector 时沿空 block 链继续传播。带 SSA 的 `If/Switch`、依赖 PSTATE 的 `Condition`、
未知 terminal 和多 owner return 仍不晋级。

这些 entry 只发布到 AddressSpace L2，`linkable=false`，不注册 LinkManager direct/pending/call target。
空 guest range 规范为 `[pc,pc+1)`，disk-cache hash 和 SMC 注册消费与 entry contract 相同的范围。
ARM64 connector 不再沿 region internal label 继承 predecessor RA/live-in，而是在同一 allocation 内用
一条分支进入目标的 published label。SMC 仍按整个 allocation owner 撤销 L2 entry；这条一次性
continuation resume 不引入通用 backward-edge site 或额外 poll。

smallpt `4 8 6` 的只读 IR census 有 383 个 terminal-only block，其中 55 个是 call-return target，
实际形态全部为常量 `LinkBlock`。曾评估把它们作为通用 external edge 发布：PageFatal 消失，但
Debug 总量增长 186 条；该版本已删除。最终 L2-only 方案的生产用例验证两级空 connector 从 L2
进入目标、两级都不是 LinkManager target，并在 source guest byte 失效后一起从 L2 清除。

Mac 通过 87 条 function-entry、117 条 continuation、829 条 production direct-link、684 条非压力
SMC 和 309 条 jit-cache 断言；新增 dataflow contract 与生产执行分别为 5 和 12 条。Release 同源
A/B 中，smallpt 从 279 roots / 49,265 条变为 275 / 49,249，消除 4 个各 4 条的独立 connector root，
公共 275 roots 逐条不变，PPM SHA-256 保持 canonical。SQLite 从 2,188 / 356,545 变为
2,115 / 356,245：73 个不再独立编译的 connector 合计 290 条，2,115 个公共 root 再减少 10 条，
无增长 root，程序正常完成。

本阶段没有保留 census probe、env 开关、日志、临时源路径或旧的通用 external-edge 方案，也没有
运行压力测试或长基准。P0 的 stateless terminal-only return connector 已闭合；含 SSA/PSTATE live-in
的 terminal 仍必须重解码为独立 canonical root，不能复用本合同。

### 16.22 精确 helper observation 与 host-NZCV contract

提交 `0ad7d65` 为 `HelperCallTraits` 补齐 guest-state read/write、直接 fault、dispatcher reentry 和
host-NZCV effect。所有零值保持保守；间接 helper、CallLocation/CallDynamic、X87 和专用 Sse42 IR
继续解析为 opaque contract。`HelperCallContract` 只有在 helper 不访问隐式 guest state、不直接 fault、
不重入且明确保留 NZCV 时才允许 pending host flags 穿过调用，未知 helper 不建立第二条运行时路径。

`EmitHostCall` 在调用前解析一次 contract，满足完整条件时不再执行无条件 NZCV merge/flush。
`GuestStateMap` 和 JIT 的 block/region 扫描改为消费 instruction-aware effect；fault/observation 与物理
NZCV、x12 flags-token clobber 分开判定，避免仅凭“无 fault”推导寄存器状态仍存活。现有静态 opcode
分类继续服务没有 instruction metadata 的调用点，不保留旧 helper 全屏障作为兜底。

首个生产 consumer 是 resident REP-string wrapper。它已有 x3-x15、q16-q31 和 x30 保存合同；本阶段
利用 AAPCS64 保留的 d8 低 64 位跨 C helper 保存 NZCV，d8 仍属于 contract 明确声明的 caller-clobbered
FPR，JIT 会按既有 live snapshot 处理。REP helper 的 guest page fault 继续通过返回值交给紧随其后的
`CheckMemoryAlignment`，该指令在测试 fault bit 前提交仍存活的旧 guest flags。

Mac Debug 的 helper 分组通过 86 条断言 / 9 个 case，region flags 通过 60 条，pinned value 通过 5 条；
生产执行用例验证实际 `SwiftRepStos1Resident` 前后的 pending NZCV 与无 helper 基线一致。Release 同源
static-only A/B 中，smallpt `4 8 6` 两侧均为 275 roots / 49,249 条并保持 canonical PPM SHA-256。
SQLite `--size 1 --testset main :memory:` 两侧均命中 2,114 roots，100% root/top-20 覆盖，
`355,965 -> 355,961`（`-4`）；`0x46eb50` 与 `0x4c1e9f` 各减少 2 条，无增长 root。

本阶段没有运行压力测试或长基准，也没有新增 env 开关、probe、日志、临时源路径或兼容兜底；同源
Release 构建和 capture 目录已删除。其他 helper 只有在函数实现、wrapper 和调用点能同时提供静态证明
时才可升级；第 7 节完整 guest slot/value version、fault snapshot 与跨 CFG join 仍是下一项机制工作。

### 16.23 block-local guest value version 与 W/X 宽度事实

提交 `2d2556e` 将 `GuestStateMap` 从 fixed-home 生存期查询扩展为同一 block 内的值版本模型。
`FixedHomeValue` 同时记录 home、32/64 位视图和物理高 32 位是否已归零；分析只在确实存在 publication
后复用的 block 中启用。入口 `GetHostGPR`、零偏移 `SetHostGPR` 和 `ZeroExtend32To64` 的低位版本进入
同一 active state，后续 `SetHostGPR`、精确 helper clobber 和分配后 coalesced write 的实际物理写入点
都会撤销对应 home，避免按较晚的 IR publication 错估旧版本仍存活。

consumer-specific fixed-home 解析已归入 `GuestStateMap`，旧的 translator 私有
`pinned_gpr_use_homes` 被删除。已证明的 `SetHostGPR`、`Add`、`Select` 以及 32 位
`Sub/And/Or/Xor` 可直接读取 resident home；当 `GetHostGPR` 的全部普通 use 都有同一版本依据时，
入口 move 不再生成。32 位值若仍驻留在未规范化高位的 X home，发布到同一架构 home 时显式执行 W
self-move，不能把逻辑低位等价误当成物理 64 位状态已经可观察。

Mac Debug 的 pinned 分组通过 104 条断言 / 28 个 case，helper 分组通过 86 条断言 / 9 个 case，
region flags 通过 60 条断言；新增生产 codegen 用例验证已发布版本直接供普通 ALU consumer 使用。
Release 同源 static-only A/B 中，smallpt `4 8 6` 两侧均为 275 roots，100% root/top-20 覆盖，
`49,249 -> 49,107`（`-142`，`-0.288331%`），PPM SHA-256 保持 canonical。SQLite
`--size 1 --testset main :memory:` 两侧均为 2,114 roots，100% root/top-20 覆盖，
`355,961 -> 354,915`（`-1,046`，`-0.293852%`），无增长 root 且程序正常完成。

单次同机 profile 中 2,114 个函数的 codegen 阶段约从 289 ms 增至 323 ms；TOTAL 受运行顺序和系统
负载影响较大，本阶段不把一次样本当作稳定 wall-time 结论。实现没有增加无条件全函数预扫描，
early-clobber 使用小向量而非逐项树节点分配。没有运行压力测试或长基准，也没有保留 env 开关、
probe、日志、临时源路径或兼容兜底。当前状态格仍是 block-local；fault snapshot、diamond/backedge
join、`KnownZeroAbove(8/16)`、sign-extended facts 和 memory/XMM consumer 仍属于第 7 节后续工作。

### 16.24 fault-visible width snapshot 与 CFG join

提交 `4fd6d24` 为 `GuestStateMap` 增加函数级 W/X 宽度事实合流和 fault-visible value snapshot。
入口 facts 使用 must lattice：函数入口、canonical external root、call-return root 和无 predecessor block
均从 Unknown 开始；普通内部边取全部 predecessor 的交集，diamond 和 backedge 迭代到稳定点。
零偏移 32 位 `SetHostGPR` 产生 `KnownZeroAbove32`，64 位写只在定义可证明为 zero-extend、32 位常量
或已知 entry value 时保留该事实；partial high write 和不透明调用撤销对应 home。

值版本扫描在 fault/observation 指令执行前记录当前已发布的 `{version, home, width}` 以及高位事实。
提前写 pinned home 的 publication window 不再只询问“中间是否可能 fault”，而是要求每个 fault snapshot
已经包含同一版本；否则仍拒绝 producer-time 写入。faulting load 不会被误当成已经写回 destination，
外部入口也不会继承只在内部 predecessor 上成立的高位结论。

函数级求解按需启动：只有当前 block 存在可消费 entry width fact 的 U32 same-home publication，或存在
需要 fault snapshot 的 SelectZero publication window 时才遍历 CFG。最初的无条件版本在 SQLite 的
2,114 个函数上增加约 27 ms codegen 时间，已删除；最终需求驱动版本的单次配对为 319.5 ms 与
312.8 ms，属于运行噪声范围，不声明编译性能收益。

Mac Debug 的 pinned 分组通过 110 条断言 / 30 个 case，覆盖 internal/external entry、diamond 和
backedge；fault-snapshot 分组通过 56 条断言 / 3 个 case，真实 PageFatal 用例验证已提交的 W 写恢复为
高 32 位清零的 GPR，同时 faulting load 的 destination 保持旧值。SelectZero 定向分组通过 4 条断言，
helper 和 region-flags 分别通过 86 和 60 条断言。

Release 同源 static-only A/B 中，smallpt 两侧均为 275 roots / 49,107 条并保持 canonical PPM；
SQLite 两侧均为 2,114 roots / 354,915 条，100% root/top-20 覆盖且正常完成。两个短语料没有命中
新的跨 CFG consumer，因此本阶段只声明机制覆盖和零回退，不声明宏观缩小。没有运行压力测试或长
基准，也没有保留 env 开关、probe、日志、临时源路径或兼容兜底。第 7 节剩余项收窄为
`KnownZeroAbove(8/16)`、`KnownSignExtended(8/16/32)` 和 memory/XMM consumer 的实际接入。

### 16.25 zero/sign extension facts 与生产 consumer

提交 `f4ca978` 用首类 `ExtensionFacts` 替换 `known_zero_above_32` 布尔量，不保留两套宽度机制。
每个 resident value 和 CFG entry 现在可表达 `KnownZeroAbove(8/16/32)`，以及
`KnownSignExtended(from,to)`；32/64 位 publication、窄 partial write、helper clobber、external root、
diamond 和 backedge 都消费同一 transfer/meet 规则。不同 predecessor 的 zero facts 取共同较弱宽度，
sign facts 取共同较大 source width 与较小 destination width，无法共同证明时退回 Unknown。

`SetHostGPR` 发布链会沿 `ZeroExtend32To64`、`ZeroExtend32` 和 `SignExtend` 记录低位 SSA alias。
后续 U8/U16 `ZeroExtend32` 或 `SignExtend` 只有在同一 home 仍承载该版本且扩展范围匹配时，才把
`uxtb/uxth/sxtb/sxth` 收敛为 resident-home move；target overwrite、fault clobber 和 helper clobber
都会撤销该事实。扩展 consumer 暂不参与 definition-level `GetHostGPR` 删除计数，因为旧
`fused_pin_zext32` 仍可能先于新 consumer 合同返回。曾经允许它参与的中间版本在 smallpt/SQLite
第 84 个 root 后因未物化原结果寄存器提前退出；该路径已删除，最终 Release 两个语料均正常完成。

Mac Debug 的 pinned 分组通过 120 条断言 / 33 个 case，覆盖 U8 zero extension、S8 sign extension、
overwrite、external root、diamond 和 backedge；fault-snapshot、SelectZero、helper 和 region-flags
分组分别通过 56、4、86 和 60 条断言。Release 以 `911c57e` 为同源基线，smallpt 保持 275 roots、
100% root/top-20 覆盖和 canonical PPM，`49,107 -> 49,095`（`-12`，`-0.024436%`）；SQLite 保持
2,114 roots 和 100% root/top-20 覆盖，`354,915 -> 354,800`（`-115`，`-0.032402%`），无增长 root。

一次同机 profile 的 codegen 为 312.1 ms 与 316.9 ms，TOTAL 为 1.486 s 与 1.485 s；只作为没有
明显 wall-time 回退的一致性检查，不声明性能结论。没有运行压力测试或长基准，也没有保留 env
开关、probe、日志、临时源路径或兼容兜底。第 7 节下一步只扩展到能由同一事实格证明的 narrow
memory/compare consumer 和 XMM scalar lane，不再增加 producer 形态白名单。

### 16.26 narrow compare 与 memory consumer

提交 `b69d594` 让 narrow compare 和普通 `StoreMemory` 直接消费 `GuestStateMap` 的 resident version。
U8/U16 `Sub` 只有在相同 home 的相同版本仍存活，且 `ExtensionFacts` 证明
`KnownZeroAbove(width)` 时才获得 fixed-home operand，避免用带脏高位的 W compare 改变 carry。
窄 store 只需要版本、home 和宽度完全匹配，`EmitStoreMemory` 在旧的专用
`pinned_memory_values/fused_pin_gpr_reads` 之前消费这一统一事实；overwrite、helper clobber 和 fault
边界继续由同一 active-state invalidation 处理。

定向 codegen 用例验证已发布 U8 版本直接生成 `cmp w22,#5` 和 `strb w22`，不再物化额外低位副本。
Mac Debug 的该分组通过 2 条断言，pinned、fault-snapshot、SelectZero、helper 和 region-flags 分别通过
120、56、4、86 和 60 条断言。

Release 以 `f4ca978` 为同源基线。smallpt 保持 275 roots、100% root/top-20 覆盖和 canonical PPM，
`49,095 -> 49,065`（`-30`，`-0.061106%`）；SQLite 保持 2,114 roots 和 100% root/top-20 覆盖，
`354,800 -> 354,519`（`-281`，`-0.079200%`），无增长 root 且正常完成。单次 SQLite profile 的
codegen 为 315.6 ms、TOTAL 为 1.483 s，与上一阶段同量级，只作为无明显回退检查。

没有运行压力测试或长基准，也没有保留 env 开关、probe、日志、临时源路径或兼容兜底。GPR
fault/CFG/extension 格的生产 consumer 已覆盖 ALU extension、narrow compare 和 narrow memory store；
第 7 节下一步只剩 XMM scalar lane 是否能复用该模型的收益审计，不为低权重形态强行扩展。

### 16.27 reclaim-generation continuation invalidation

提交 `3de2a5a` 将 host continuation 生命周期绑定到现有 QSBR reclaim generation。每个
`RuntimeEpoch` 记录已同步的 reclaim epoch 和该 Runtime 的 RSB 持久指针；`BeginJit` 在任何代码缓存
查找之前比较 generation，发现旧代码已经进入退休队列时直接把持久 RSB 恢复为空。多线程路径复用
原本必需的 `global_epoch_` 读取，单线程只有启用 RSB 时才读取一次 generation；return hit/miss 和
call publication 的生成代码没有增加 frame generation、load、compare 或分支。

当前线程触发受保护 guest code 页写 fault 时，SMC transaction 完成 target generation 撤销、direct
unlink 和 dispatch slot 清除后，signal recovery 通过 `GuardedReturnStack::Reset` 立即把 ucontext 的
x25 恢复为空。其他正在运行的 Runtime 仍遵守原有语义，可以完成失效前已经进入的旧代码；QSBR 在其
退出前不复用 allocation，下一次 `BeginJit` 又会在读取 x25 之前消费新 generation，因此不存在旧
host continuation 在 code-cache 地址复用后形成 ABA 命中的窗口。guard-page 越界恢复与 SMC 失效
复用同一个 x25 reset 入口。

Mac Debug 通过 continuation、production direct-link、非压力 SMC、indirect-L1 和 guarded return
stack 定向分组；包含真实 source allocation 退休与重用门禁的 production lifecycle 用例通过 144 条
断言。Release static-only 保持 smallpt `275 / 49,065` 和 SQLite `2,114 / 354,519`，与 `ae28113`
完全一致。一次临时同源 SQLite 短配对为 `TOTAL 1.555s -> 1.554s`，只用于确认边界 generation 检查
没有明显回退；临时 worktree、构建和 capture 均已删除。本阶段没有新增 env 开关、probe、日志、
临时运行路径或兼容兜底。第 8 节 generation/unlink/invalidation 与 code-cache reuse 的 correctness
缺口至此闭合，后续不再给 16-byte continuation frame 增加热路径 generation 字段。

### 16.28 exact-mask EdgeFlags shared tail

提交 `ee04fb9` 去掉 mixed region join 只接受 full NZCV 的最后一道生产 gate。partial source 与一个
compatible successor、一个 canonical successor 相遇时，也可以把 canonical merge 移到函数冷区；
canonical tail 继续按 `{target, mask, polarity, version, token}` 合并相同入口。函数内 stub 只生成
`ADR target` 和到 region trampoline 的分支，不在每个代码对象里复制 mask materialization 或 merge
主体。

每个 code-cache region 为 14 个非零非 full 的 NZCV mask 生成一次精确 merge 入口，并分别提供普通
与 PF/AF token 版本。连续 mask 使用 `MRS/UBFX/BFI`，非连续 mask 使用寄存器 mask 的
`MRS/BIC/AND/ORR`；两条路径都只替换 incoming mask，未请求的 NZCV、packed AF 和 parity 保持原值。
full NZCV、return、cycle 和普通 direct-link trampoline 继续使用原入口与协议。最初的单个动态-mask
入口要求每个函数 stub 额外物化 mask，使 SQLite 增长 14 条；该版本未保留，最终 per-mask region
入口把函数 stub 收回两条，并在共享 region 中摊薄可变 merge 主体。

Mac Debug 的 masked trampoline 用例覆盖连续 `N|Z`、非连续 `N|C` 以及带/不带 token 的四种组合，
通过 25 条断言；region-flags production、全部 direct-link trampoline、production direct-link、
direct-link flags 和非压力 SMC 分组均通过。Release 以 `dd4a51b` 为同源基线，smallpt 保持 275 roots
和 100% root/top-20 覆盖，`49,065 -> 49,055`（`-10`，`-0.020381%`），6 个 root 缩小、无增长；
SQLite 保持 2,114 roots 和 100% 覆盖，`354,519 -> 354,491`（`-28`，`-0.007898%`），19 个 root
缩小、无增长。一次 SQLite 短配对为 `TOTAL 1.571s -> 1.573s`，只作为无明显回退检查。

同时审核了 XMM scalar 的最窄 value-version 候选：允许同一 `GetHostFPR` 值跨同 slice 的自发布
`SetHostFPR` 保持 fixed alias。smallpt 与 SQLite 均为零代码变化，候选已完整删除；后续 XMM 工作
必须先证明更广的 producer/consumer lineage 命中真实热点，不再重试该同值自发布形态。本阶段没有
保留 env 开关、probe、日志、临时路径或兼容兜底，也没有运行长基准或压力测试。EdgeFlags 剩余的
`packed_flags_version` 目前没有非零生产者，下一步应删除这项推测性 ABI，而不是伪造第二种 layout。

### 16.29 删除未使用的 packed-flags version ABI

提交 `d7076b7` 删除 `EdgeFlagsState` 与 `EdgeFlagsTargetContract` 中从未有生产非零值的
`packed_flags_version`。pending state、target acceptance 和 canonical-stub key 不再携带恒为 0 的
version；`LinkSiteRecord` 与 `LinkTargetRecord` 分别从 80/96 字节缩小到 72/88 字节。磁盘 JIT cache
同时删除 block target contract 和 link-site edge state 中的两个 64 位序列化槽，格式版本从 19 升为
20，旧 cache 由现有 validity key 直接拒绝，不保留双格式读取兜底。

Mac Debug 的 direct-link flags、serializer、完整 JIT-cache、region-flags production 和全部
direct-link trampoline 分组通过；serializer v20 定向分组通过 63 条断言。Release static-only 保持
smallpt `275 / 49,055` 与 SQLite `2,114 / 354,491`，说明该阶段只收缩状态/缓存 ABI，不改变发码。
源码与测试中已不存在 `packed_flags_version` 或测试专用 nonzero producer，也没有新增日志、env 开关、
probe、临时路径或兼容读取机制。第 6 节 EdgeFlags ABI 至此没有未实现的状态维度。

### 16.30 inline SSE4.2 result packing

提交 `51c783c` 收敛 inline `Sse42Str` 的 IntRes2/index/flags 打包。NEON movemask 与三种 validity/
polarity 变换已经保证结果位于 architectural n-bit mask，删除末尾重复 `AND all`；least-significant
索引在 `RBIT/ORR/CLZ` 前比较 IntRes2，most-significant 索引复用本来就需要的空结果比较，使 CF 在
index 打包后直接 `CSET`，不再重新 `AND+CMP`。最高位索引同时用 `CLZ+EOR #31` 取代
`CLZ+MOV 31+SUB`。

Mac Debug 的 SSE4.2 Rosetta/SDM 差分通过 16,255 条断言，16-byte memory boundary 与 alias/REX
分组分别通过 4 和 27 条。Release 同源 strict A/B 保持 2,114 个 SQLite root 与 100% root/top-20
覆盖，`354,491 -> 354,486`（`-5`，`-0.001410%`），唯一变化是 `__strspn_sse42`
`318 -> 313`，无增长；smallpt 保持 `275 / 49,055`。一次 SQLite 短配对为
`TOTAL 1.485s -> 1.490s`，translation/codegen 两项均略降，只作为总时间无明显回退检查。

该阶段没有改动共享 `0x02/0x1a` vector helper ABI，因此不宣称缩小 `__strcspn_sse42` 或
`__strcmp_sse42`；也没有重试已经回退的 per-unit EqualAny outline。没有保留 probe、日志、env 开关、
临时构建路径或兼容兜底。后续字符串收益必须从共享 helper 边界或循环/EA 布局获得，不能继续堆叠
只对未命中 imm 形态生效的局部 mask 白名单。

### 16.31 spilled pinned-base EA 延迟物化

提交 `ea4fd15` 将复杂 EA 的首个生产 consumer 落到实际 spill 大户。对 `setupLookaside`
的 ARM64 代码重新分类后，原先归入 state publication 的 `x28+0xb0..0xd0` 访问实际是
`State::spill_area`：576 条 host 指令中有 176 条 GPR spill load/store。主要残差不是未 pin 的
R15，而是默认 level 2 只有七个动态 value/scratch register 后产生的短生命周期地址中间值。

既有 `MatchPinnedMemoryAddress` 只能证明 identity `GetOperand(base)`。本阶段把同一 proof 扩展为
`base + immediate`：地址必须只有一个普通 `LoadMemory/StoreMemory` consumer，base 必须已映射到
静态 fixed home，且从 fixed-home publication 到 memory consumer 之间没有同 home 写入或
caller-saved helper clobber。只有地址结果已经被 RA spill 时才延迟物化；identity 路径继续复用原
fixed alias。memory-base 模式通过 `BiasMem(base, offset)` 保留 guest wrap/mask 语义，identity 模式
只接受可直接编码的 load/store displacement；`INT64_MIN` 等不能安全取反的偏移继续走原路径。

短门禁结果：

- 新增定向用例强制 `GetOperand(base + 24)` 落入 `RegAlloc::MEM`，分别验证 identity
  `[x6,#24]` 与 biased `add x10,x6,#24; [x10,x24]`，通过 5 条断言。相关 Debug 六个
  address/GetOperand case 共通过 29 条断言。
- SQLite `--size 1 --testset main :memory:` 保持 2,114 roots 与 100% 公共 root 覆盖，
  `354,486 -> 350,451`（`-4,035`，`-1.138%`）；363 个 root 缩小、零增长，
  `setupLookaside` 从 `576 -> 558`，正好删除九组 EA spill 的 18 条存取。
- smallpt `4 8 6` 保持 275 roots，`49,055 -> 48,601`（`-454`，`-0.925%`），PPM SHA-256
  保持 `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`。

同时删除了两条不满足正确性/零增长门禁的原型。把普通 spill scratch 跨指令保留会在 SQLite
第 24 个 root 触发 PageFatal；限制到 last-use 或 VIXL x16/x17 仍失败。把旧 full-pin
fixed-class 直接放宽到 level 2 则产生大量增长 root，width-only 变体总量还增长 2,106 条。
这些原型均未保留。后续 RA 工作需要首类 interval split/relocation，不能用隐式 scratch 生存期或
宽泛 fixed-class 代替。该阶段没有新增 env 开关、日志、probe、临时路径或兼容兜底，也没有运行
压力测试或长基准。

### 16.32 biased memory 的故障安全 spilled update

提交 `3fb918b` 收敛 memory-base 模式下的 `Sub -> StoreMemory -> SetHostGPR` spill 链。identity
memory 可以用 ARM64 pre-index store 同时完成访存和基址更新；带 page-table bias 时无法表达
`[guest_base + pt]` 的硬件 writeback，旧路径因此先物化 `Sub` 结果，store 成功后再从该 SSA 发布 guest
基址。当更新值被 RA spill 时，这会为一个短生命周期地址引入 spill store、memory-address reload 和
publication reload。

新的公共 proof 仍要求 U64 `Sub` 只有 memory/publication 两个 use、递减量为 1..256 且等于 store
宽度、三条 IR 相邻、源值来自目标 fixed home 的零偏移 `GetHostGPR`，并拒绝 stored value 与基址
寄存器重叠。只有 memory-base 且 `Sub` 结果确实落入 `RegAlloc::MEM` 时才启用新发码：访存地址从旧
fixed home 通过 `BiasMem(base, -decrement)` 计算，faulting store 先执行，store 成功后才在
`SetHostGPR` 位置把递减量直接发布回 fixed home。这样越界 fault 仍观察到更新前的 guest 基址；未
spill 的 biased 路径保持一次普通地址计算，identity 路径继续使用 pre-index store。

短门禁结果：

- Release/Debug 构建通过；stack writeback、故障保持、spilled biased update 和上一阶段 spilled EA
  五个定向 case 共通过 24 条断言。强制 spill 用例验证地址计算、store、基址发布的机器指令顺序。
- SQLite `--size 1 --testset main :memory:` 保持 2,114 roots、100% root/top-20 覆盖，
  `350,451 -> 346,127`（`-4,324`，`-1.234%`）；159 个 root 缩小、零增长，`setupLookaside`
  `558 -> 537`。
- smallpt `4 8 6` 保持 275 roots、100% 覆盖，`48,601 -> 47,992`（`-609`，`-1.253%`）；
  24 个 root 缩小、零增长，PPM SHA-256 保持
  `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`。

本阶段没有增加 env 开关、日志、probe、临时路径或旧机制兜底，只运行了 8 秒上限的 static-only
短跑；实际 SQLite/smallpt capture 分别约 4.1 秒和 0.6 秒。

### 16.33 紧邻 spilled producer 直接发布

提交 `5ca4239` 把剩余的 `producer -> spill store -> reload -> SetHostGPR move` 收敛为一个分配后
publication transaction。新 planner 只接受 U32/U64 `LoadImm`、`LoadMemory`、`Add`、`Sub` 和
`And`：producer 必须确实落入 `RegAlloc::MEM`，只有一个紧邻的零偏移 `SetHostGPR` use，不产生
pseudo flags，目标必须是启用的 pinned GPR，且不能与 dead/coalesced write 或已有 pinned value plan
重叠。producer emitter 直接选择目标 fixed home，publication 本身不再发码。

`LoadMemory` 也使用同一事务，但不把 publication 移到访存之前：ARM64 faulting load 只有成功完成才
提交目标寄存器，因此同步 data abort 仍保留旧 fixed-home 值，fault snapshot 可以恢复 fault 前 guest
状态。纯 ALU/常量 producer 与 publication 之间没有其他 IR，提前一个 IR id 写入不会跨越 observer。

短门禁结果：

- Debug 的全部 pinned 分组通过 35 个 case / 131 条断言；紧邻常量、faulting load 和 ALU producer
  均由强制 `RegAlloc::MEM` 的定向形态覆盖。相关 publication/fault 分组通过 5 个 case / 22 条断言。
- Release SQLite 保持 2,114 roots，`346,127 -> 343,547`（`-2,580`，`-0.745%`），对应 860 个
  transaction。8 秒 Debug 公共集的 753 个 root 中有 47 个缩小、零增长，所有变化严格为三条指令的
  整数倍。
- Release smallpt 保持 275 roots，`47,992 -> 47,650`（`-342`，`-0.713%`），对应 114 个
  transaction；PPM SHA-256 保持
  `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`。

本阶段没有新增 env 开关、日志、probe、运行时兜底或长期基准；SQLite/smallpt Release static-only
capture 分别约 4.3 秒和 0.5 秒。

### 16.34 平台无关的紧邻算术 spill forwarding

提交 `3b7acd4` 把原先仅限 desktop Linux x18 的相邻 spill forwarding 扩展为平台无关的单边
ownership transfer。spilled scalar def 的 scratch 只有在 consumer active mask 未占用、consumer 没有
同寄存器 fixed clobber、没有 memory/helper/control barrier 时才可转交；`TickIR` 在 emitter 和 VIXL
scratch leasing 前把实际转交的寄存器写入 consumer dirty mask，scratch-only x12/x13 和 level-3 x10
lease 也不得把它重新释放。非 Linux 路径只接受 definition 的全部剩余 use 都位于该 consumer，Linux
x18 继续保留既有的多 use 延迟 writeback 能力。

portable 路径进一步只允许会真实写出结果的整数 `Add/Sub/Adc/Sbb`、逻辑、select 和 shift
consumer。最初的广覆盖版本以及只加 last-use 的版本都会在短 SQLite 中于 `rip=0x414a7e` 触发
PageFatal；执行追踪确认 `ZeroExtend32To64` 等 ownership-transfer IR 可能因 RA width-chain 证明而不
发机器指令，转交 source scratch 会绕过其结果 slot。最终版本明确排除全部 transparent wrapper/alias，
并增加负向用例验证 width ownership transfer 仍提交并重载 spill slot。

短门禁结果：

- Debug spill 分组通过 10 个 case / 23,078 条断言，pinned 分组通过 35 个 case / 131 条断言；新增
  portable forwarding 与 width-transfer rejection 共覆盖 11 条断言。
- Release SQLite 保持 2,114 roots，`343,547 -> 339,999`（`-3,548`，`-1.033%`），对应 1,774 个
  紧邻算术 forwarding edge；`setupLookaside` `534 -> 524`。
- Release smallpt 保持 275 roots，`47,650 -> 47,110`（`-540`，`-1.133%`），对应 270 个 edge；
  PPM SHA-256 保持
  `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`。

每个命中只删除相邻的 spill store/reload 两条指令，不改变 consumer 发码或 root 划分。本阶段没有保留
诊断输出、probe、env 开关、临时路径或兼容兜底，只运行了 8 秒上限的 static-only 短跑。

### 16.35 基本块内多 use spill reload region

提交 `48ef441` 把相邻 forwarding 之后的多 use spill 残差纳入一等分配所有权。新增的
`register_alloc_spill_reload` 在普通线性扫描完成后按 LIR 基本块收集 scalar GPR spill use，并在
`BindLabel/Goto/NotGoto` 处分段。只有一个动态 GPR 在完整区间内都不与 live value、fixed clobber 或
既有 reload region 冲突，且扣除该 value 的逐 use reload 后每条 IR 仍满足精确 scratch budget，才发布
region。`RegAlloc` 把该寄存器写入区间内每条 IR 的 dirty mask，现有最终 verifier 继续逐指令复核。

JIT 不在 region 起点无条件插入 load；`SpillGPR` 在 emitter 第一次真实读取该 value 时才从 canonical
spill slot 加载，并按 region id 复用到最后一个 use。这样 `GetOperand` 地址重物化、直接 pinned
publication 和 transparent width owner 等“IR 有 use、emitter 不读 slot”的形态不会产生假 load。
进入 region 的 consumer 禁止旧 pending-write forwarding，先按原顺序提交 spill slot，保证 fault、
helper 和调度边界仍看到 canonical backing。region 不跨 LIR 局部控制流，也不改变 FPR spill 或 CFG
live-in ABI。

短门禁结果：

- Debug spill 分组通过 12 个 case / 23,087 条断言；新增正向用例证明同一 spill slot 的两个非紧邻
  arithmetic use 只加载一次，负向用例证明 region 不跨 `NotGoto/BindLabel`。CallLambda 与函数级
  static interaction 分别通过 67/65 条断言，SSE4.2 Rosetta/SDM 差分通过 16,255 条断言。
- 完整 smallpt `4 8 6` static-only A/B 保持 275 roots、100% root/top-30 覆盖和零增长，
  `47,110 -> 46,573`（`-537`，`-1.140%`）；PPM SHA-256 保持
  `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`。
- SQLite 只跑 8 秒上限，不将缺失 root 外推为全量结果。共同的 768 roots 覆盖 baseline host code
  `88.348%`，`138,603 -> 137,087`（`-1,516`，`-1.094%`），96 个 root 缩小、672 个不变、零增长；
  `__strcmp_sse42@0x505120` `351 -> 341`。

本阶段没有保留 probe、日志、env 开关、临时源码路径或兼容兜底，也没有运行长基准或压力测试。
剩余 RA 缺口收窄为跨局部控制流的显式 split interval、FPR spill region，以及有完整 fault/observer
证明的 definition-to-region transfer；在新的加权账证明其规模前不继续泛化。

### 16.36 spill definition-to-region ownership transfer

提交 `f5897d2` 让第 16.35 节的 reload region 直接接管满足闭包证明的 spilled definition。producer
必须属于会真实写出 scalar GPR 结果的 load/整数 ALU/select/shift/bit-extract 集合；所有普通 use 必须
位于同一个无局部控制流的 segment，use 计数必须与 definition 的完整非 pseudo use 集一致，terminal
不得再引用该 value。所有 consumer 也必须属于明确读取输入的 memory store 或整数 consumer 集合，
因此 `ZeroExtend32To64`、`BitCast/GetOperand`、直接 publication 和 helper 边不会以透明 ownership
transfer 冒充真实读取。

满足证明后，region 从 definition IR 开始占有同一物理 GPR。`SpillGPR` 在 definition 写入时直接激活
region，不再建立 pending spill write；后续 use 读取同一 resident owner，因此 canonical store 和首次
reload 同时删除。不满足完整 use、terminal、fixed-clobber 或 scratch headroom 证明的 value 继续使用
第 16.35 节的 canonical backing，没有增加第二套运行时开关或回退协议。

短门禁结果：

- Debug spill 分组通过 12 个 case / 23,086 条断言；width ownership 负向用例继续观察到 canonical
  store/reload，正向用例确认 multi-use definition 直接驻留且不访问 spill slot。CallLambda 67 条、
  function/static interaction 65 条、SSE4.2 差分 16,255 条和 direct-link 900,306 条断言全部通过。
- smallpt `4 8 6` 保持 275 roots、100% root/top-30 覆盖和零增长，
  `46,573 -> 46,363`（`-210`，`-0.451%`）；PPM SHA-256 仍为
  `a70375e511474ad45215f93df3e2c3db44af41afe40bb1c76e0f14d5528ea7b1`。同源 RA shape 中
  `spill_loads/stores` 从 `907/922` 降到 `858/872`。
- SQLite 8 秒共同 768 roots 保持 100% baseline root/top-30 覆盖，
  `137,087 -> 136,445`（`-642`，`-0.468%`），68 个 root 缩小、700 个不变、零增长；候选额外完成
  86 个 root，不把它们计入收益。`__strcmp_sse42@0x505120` `341 -> 339`。

本阶段没有保留 probe、日志、env 开关、临时源码路径或兼容兜底，也没有运行长基准或压力测试。
FPR pool 审计显示 smallpt 最大 live FPR 只有 7、默认 pool 为 16，因此不立 FPR spill region；剩余 RA
方向只保留需要显式 CFG split interval 的跨局部控制流形态，等待新的加权规模证明。

### 16.37 fault-aware spilled fixed publication window

提交 `84a4d08` 将原本只接受 IR 紧邻的 spilled fixed publication 收敛为显式 publication
window。producer 仍必须只有一个普通 `SetHostGPR` consumer、宽度与目标 fixed home 完全匹配，
但 flags pseudo use 不再阻断同一算术事务；`GuestStateMap::PublicationWindowSafe` 统一证明提前写入
目标 home 不会穿过未覆盖的 fault/observer。局部控制流、固定寄存器 clobber、目标 home 的其他
输入或定义继续整条拒绝。

`GetOperand` 与 `SignExtend` 现在和既有 load/整数 ALU producer 一样直接选择目标 fixed home，
publication 不再生成 spill store/reload 和末尾 move。两个 emitter 只消费已经建立的
`pinned_gpr_values` 结果，不新增运行时状态或第二套 publication 协议。定向用例同时覆盖跨纯 flags
窗口的正向路径和没有 fault snapshot 时的拒绝路径。

短门禁结果：

- Debug spill 分组通过 14 个 case / 23,091 条断言；CallLambda、function entry、fault snapshot、
  SSE4.2 Rosetta/SDM 差分和 production direct-link 分组分别通过 67、87、56、16,255 和 864 条断言。
- smallpt `4 8 6` 保持 275 roots、100% root/top-30 覆盖、零增长和 canonical PPM，
  `46,363 -> 46,258`（`-105`，`-0.226%`）；同源 RA shape 的 `spill_loads/stores`
  从 `858/872` 降到 `842/856`。
- SQLite `main/size1` 保持 2,114 roots、100% root/top-30 覆盖和零增长，
  `334,592 -> 334,016`（`-576`，`-0.172%`）；去除 timing 后的输出逐行一致。
  `__strcmp_sse42@0x505120` 从 `339 -> 336`。

本阶段没有运行长基准或压力测试，也没有保留 probe、日志、env 开关、硬编码 guest PC、临时源码
路径或兼容兜底。

### 16.38 剩余机制的规模与前置条件复核

本轮同时用同一 smallpt/SQLite 短门禁复核了三个更宽的机制候选，均未保留：

- 跨局部控制流的显式 spill preload 具备 dominance、scratch headroom 和 canonical slot 证明，
  定向 13 个 spill case 通过，但 smallpt 与完整短 SQLite 都是严格零代码变化。当前真实 spill
  residual 不命中这一形态，不建立空机制。
- R15 block carrier 的最窄无 store/helper 版本在 SQLite 只减少 36 条；扩展到显式块入口 reload 后
  会在约第 477 个 root 触发 guest trap。level 2 emitter 仍有区别于完整 level 3 的隐藏 fixed-register
  ABI，单独保留 x9 会形成不受支持的 hybrid。该原型已完整删除，不重试启发式 R15 residency。
- fallthrough-preserving hot-chain stitching 的 smallpt 静态上限只有 33 条额外邻接边；直接在 emitter
  换序会破坏 function RA 的 instruction-id/RPO emission 同序约束，使 smallpt 在第 5 个 root 后提前
  退出。后续只有在 RA live interval 本身支持 layout order 后才可重启，不保留 emitter-only 重排。

### 16.39 live-through 精确的 SSE4.2 shared helper ABI

提交 `0f2c7c1` 将 shared vector helper 的保存合同从“当前指令占用”收敛为“调用后仍存活”。RA 在
分配区间时记录 value live end，helper emitter 因此可以排除本条 `Sse42Str` 刚定义的 GPR 结果和只在
本条消费的 FPR 参数；跨调用仍有 use 的参数继续保存。REF alias 通过现有 allocation owner 解析，
未知区间保持保守，不增加 opcode 形态白名单。

ARM64 native `0x1a` helper 的真实 FPR clobber 是 `q2-q7`，generic `0x02` helper 仍按 `q0-q7`
处理；`q0/q1` 的参数搬运 clobber 与 helper body clobber 分开建模。参数恰为 `q1/q0` 时使用 helper
本来就会破坏的 `q2` 完成并行交换。`w16` 结果只在旧 `x16` 确实 live-through 时进入栈槽，否则直接
移到 RA 结果 home；奇数个 GPR 保存同时与 `x30` 配对，不再分别 `str/ldr`。

短门禁结果：

- helper-effects、SSE4.2 scratch、Rosetta/SDM 差分、CallLambda 和 single-block RA 等价用例分别
  通过 25、512、16,255、67 和 86 条断言。定向 codegen 同时覆盖 native/generic helper 的死参数与
  live-through 参数。
- SQLite `main/size1` 保持 2,114 roots、100% root/top-30 覆盖和零增长，
  `334,016 -> 333,865`（`-151`，`-0.045207%`），15 个 root 缩小；去除 timing 后 stdout 逐行一致。
  `__strcmp_sse42@0x505120` 从 `336 -> 328`，EqualAny 字符串 root 的主要一组各减少 10 条。
- smallpt `4 8 6` 保持 275 roots、`46,258` 条 host 指令和 canonical PPM，严格零变化。

本阶段没有运行长基准或压力测试，也没有保留 probe、日志、env 开关、硬编码 guest PC、临时源码
路径或兼容兜底。第 9 节已知 SSE4.2 vector helper 的 caller-save 大头至此闭合；后续字符串工作只保留
循环 fallthrough、共享调用控制流和热冷分支布局，不再用扩大 clobber 集掩盖 live-through 边界。

### 16.40 SSE4.2 packed result 的直接 publication

提交 `18aceb8` 将 index/flags 从 packed helper 结果的普通长 SSA 链收敛为同一指令事务。flags
publication 现在紧随 packed result，随后 index 以 `BitExtract(16,8)` 产生已经完整零扩展的值；因此
架构上的 ECX 写可等价发布为完整 RCX 写，现有 fixed-home coalescer 能直接生成到 x23。ARM64
`BitExtract` emitter 同时消费 spilled fixed publication 计划，`SetHostGPR` 不再重新 load/move。

spill definition transfer 增加 `Sse42Str` producer 和 `PublishSse42StrFlags` consumer，使 packed value
在 helper 返回后可由同一个 reload-region owner 同时服务 flags 与 index，不必先写 canonical spill
slot。publication 前后没有 memory、helper 或 external observer，fault 仍只可能发生在更早的 memory
operand load，未改变 x86 指令的可观察提交点。

短门禁结果：

- helper-effects、SSE4.2 scratch、Rosetta/SDM 差分和 spill 分组分别通过 25、512、16,255 和
  23,094 条断言；spill 分组共 15 个 case，新增用例验证 spilled `BitExtract` 直接发布到 fixed home。
- SQLite `main/size1` 保持 2,114 roots、100% root/top-30 覆盖和零增长，
  `333,865 -> 333,692`（`-173`，`-0.051817%`），16 个 root 缩小；去除 timing 后 stdout 逐行一致。
  `__strcmp_sse42@0x505120` 从 `328 -> 318`，EqualAny 主组从 `203 -> 192`。
- smallpt `4 8 6` 保持 275 roots、`46,258` 条 host 指令和 canonical PPM，严格零变化。
- 两组反向短配对分别为 `4.154s/4.219s` 与 `3.698s/3.713s`，只证明没有方向一致的明显回退，
  不声明动态性能收益。

同时复核并删除了两个不合格原型：只约束普通 FPR interval 跨 helper 避开 `q0-q7` 时，完整 SQLite
只减少 2 条，真实热点的保存对象不属于该可迁移集合；把 `ZeroExtend32To64` 全局纳入提前 fixed-home
publication 虽使前 295 个公共 root 减少 319 条，但 guest 在第 301 个 root 前退出，不能把字符串链的
零扩展证明外推到通用 W/X publication。

本阶段没有运行长基准或压力测试，也没有保留临时 planner probe、日志、env 开关、硬编码 guest PC、
临时源码路径或兼容兜底。剩余字符串大头已经从 packed result 搬运收窄为真实 helper frame 与循环控制流。

### 16.41 SSE4.2 helper frame 的 writeback folding

提交 `35de4f0` 将 helper frame 的独立 `sub sp`/`add sp` 融入首个保存和最后一个恢复。存在 GPR
live-through 时，offset 0 的首个 `stp` 使用 pre-index，最终对应 `ldp` 使用 post-index；没有 GPR
保存时由 x30 的 `str/ldr` 承担同一职责。其余 GPR、result slot 和 Q register 继续按既有对齐布局访问，
不改变 clobber、参数或返回 ABI。

短门禁结果：

- helper-effects、SSE4.2 scratch、Rosetta/SDM 差分和 CallLambda 分别通过 29、512、16,255 和
  67 条断言；定向 helper 用例覆盖无 GPR 保存、generic caller-clobber GPR 和 live-through FPR frame。
- SQLite `main/size1` 保持 2,114 roots、100% root/top-30 覆盖和零增长，
  `333,692 -> 333,630`（`-62`，`-0.018580%`），15 个 root 缩小；去除 timing 后 stdout 逐行一致。
  `__strcmp_sse42@0x505120` 从 `318 -> 314`，EqualAny 主组从 `192 -> 188`。
- smallpt `4 8 6` 保持 275 roots、`46,258` 条 host 指令和 canonical PPM，严格零变化。

本阶段没有运行长基准或压力测试，也没有保留 probe、日志、env 开关、硬编码 guest PC、临时源码
路径或兼容兜底。helper 现场剩余的栈指令均对应真实 live-through 值；下一步只评估能否由新的 leaf
return ABI 消除 x30 frame，而不把保存序列转移到共享 thunk 后增加动态 call/return。

### 16.42 native SSE4.2 EqualAny helper

提交 `183def8` 将 ARM64 `PCMPISTRI EqualAny` 的 shared helper 从 C wrapper 调用改为原生汇编实现。
helper 直接计算两个隐式长度、逐字节 EqualAny mask、最低匹配索引和 packed flags，不再建立第二层
C ABI frame。参数交换改用 helper 已声明破坏的 `q7`，EqualAny 的 FPR clobber 因此收敛为
`q3-q7`；GPR/FPR clobber、参数搬运和 packed result 继续由同一 `Sse42StrVectorCallABI` 描述。

短门禁结果：

- helper-effects、SSE4.2 scratch、Rosetta/SDM 差分、16-byte memory boundary 和 alias/REX 用例分别
  通过 29、512、16,255、4 和 27 条断言。
- SQLite `main/size1` 保持 2,114 roots、100% root/top-30 覆盖和零增长，
  `333,630 -> 333,604`（`-26`，`-0.007793%`）；去除 timing 后 stdout 逐行一致。
  `__strcspn_sse42@0x506d30` 从 `271 -> 245`。
- smallpt `4 8 6` 保持 275 roots、`46,258` 条 host 指令和 canonical PPM，严格零变化。
- 两组反向短配对的快慢方向交叉，只证明没有方向一致的明显回退，不声明动态性能收益。

本阶段没有运行长基准或压力测试，也没有保留 wrapper fallback、probe、日志、env 开关、硬编码
guest PC 或临时源码路径。

### 16.43 EqualAny 专用 host return ABI

提交 `f6b50a3` 为 native EqualAny helper 引入局部的 `x17` return ABI。caller 以 `ADR x17, resume`
准备返回地址，通过共享 thunk 的 `B` 或远目标的 `BR` 进入 helper；helper 以 `BR x17` 返回。普通
`0x1a` helper 仍使用标准 `BL`/`RET`，该合同不扩展到通用 helper、guest return 或 continuation ABI。

EqualAny 不再保存和恢复 x30。frame planner 在仍有 live-through GPR 时由首个 GPR save/restore
承担栈调整，只有 FPR 时由首个 Q register 承担，完全空 frame 不发射栈指令；奇数 GPR、result slot
和 FPR pair 保持原有对齐及对称恢复。x17 同时进入精确 clobber 集，因此 caller 中原有 live x17
仍会由现有 live-through 合同保存。

短门禁结果：

- helper-effects、SSE4.2 scratch、Rosetta/SDM 差分、16-byte memory boundary 和 alias/REX 用例分别
  通过 26、512、16,255、4 和 27 条断言。
- fresh same-path SQLite `main/size1` 保持 2,207 roots、100% root/top-30 覆盖和零增长，
  `343,738 -> 343,736`（`-2`，`-0.000582%`）；唯一变化是
  `__strcspn_sse42@0x506d30` `245 -> 243`。去除 timing 后 stdout 完全一致。
- smallpt `4 8 6` 保持 275 roots、`46,258` 条 host 指令和 canonical PPM，严格零变化。
- 两组反向 SQLite 短配对为 candidate/base `2.427s/2.394s` 与 base/candidate
  `2.445s/2.366s`，方向交叉，没有方向一致的明显回退。

此前评估的通用 leaf-return ABI 原型没有保留：宽泛省略 x29 会触发 guest trap；补齐 source/target
兼容合同后，省下的 x30 指令与安全 outgoing continuation 指令完全抵消，静态收益为零。当前实现只
保留由单一 native helper 明确定义、无需改变全局 return/continuation contract 的局部 ABI。

本阶段没有运行长基准或压力测试，也没有保留调试路径、probe、日志、env 开关、硬编码 guest PC、
临时源码路径或兼容兜底。
