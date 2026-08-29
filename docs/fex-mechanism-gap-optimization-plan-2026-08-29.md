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
