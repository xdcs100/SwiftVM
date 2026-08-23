# SwiftVM 与 FEX 代码生成质量差距复核

日期：2026-08-23

## 结论

SwiftVM 已经关闭 spill、NaN guard、直接链接、基础 region、NZCV 驻留和一部分
fixed-home 发布开销，但与 FEX 的剩余差距仍然主要来自三种结构性成本：

1. FEX 的 SRA 把 16 个 guest GPR、PF、AF 和 16 个 XMM 都绑定到固定 host 家，
   LoadRegister/StoreRegister 与这些家做反向亲和，成功后不发指令。SwiftVM 目前只覆盖
   部分 GPR/XMM 家和局部发布点，32 位 W 视图、跨发布快照和边界同步仍产生大量桥接。
2. SwiftVM 已把 NZCV 保留在 PSTATE，但 PF/AF 仍通过 `x26` 位域发布；FEX 用独立 GPR
   保存 PF/AF。SwiftVM 的 fault、signal、dispatcher 和 region 外边仍必须维护可恢复的
   canonical flags 状态。
3. FEX 默认 multiblock 可跨更大的前向窗口摊薄边界。SwiftVM 的 bounded-64 region 已明显
   降低 CoreMark 成本，但 SHA 等超块语料仍受函数边界、公开入口和 fault map 粒度限制。

8 月 14 日同 guest、RE=0 的历史静态 blow-up 表仍适合说明差距形状，但不能当成本轮精确
比值：之后 SwiftVM 已默认启用 FLAGS_REGS、扩大 region 窗口并连续落地两轮 fixed-home
合并。历史 SVM/FEX 比值为 CoreMark 2.305x、smallpt 2.223x、c-ray 2.423x、7-Zip
2.783x、SHA-256 3.030x；STREAM 为 0.925x，AES-GCM 为 1.213x。要刷新绝对比值，需要在
当前 FEX 与当前 SwiftVM 上重跑同一 guest PC 的静态块采集。

## 当前可复核账目

本次续轮在 Orb Linux 默认配置下禁用 JIT disk cache 和 `SVM_EXEC_PROF`，CoreMark 使用
`0x0 0x0 0x66 20000 7 1 2000`：

| 指标 | 优化前 | 本轮优化后 | 变化 |
|---|---:|---:|---:|
| host dynamic | 6,250,517,196 | 6,210,114,894 | -40,402,302 (-0.646%) |
| move dynamic | 2,155,441,643 | 2,115,039,290 | -40,402,353 |
| move 占比 | 34.484% | 34.058% | -0.426 pp |
| spill dynamic | 0 | 0 | 0 |

按两臂共同 PC 的较小 entries 重算，44 个 PC 变小、0 个变大，精确减少 40,402,351 条
host/move 指令。收益最大的五个 PC 为 `0x402d48`、`0x402de8`、`0x402218`、
`0x403980`、`0x402d40`。

其他确定性语料的共同 PC 结果：

| workload | 共同 PC 减少 | 增长 PC | 动态 host/move 变化 |
|---|---:|---:|---:|
| STREAM | 22 | 0 | -89，接近中性 |
| smallpt | 24 | 0 | -2,362,455 (-0.0053%) |

本次 c-ray 缺少 scene stdin，两臂都按既有错误路径返回 255，不作为收益证据。7-Zip 和
OpenSSL speed 在当前 Linux launcher 下仍会进入既有 signal/timer 退出路径，也不计入。

## 本轮优化

热点 `0x402de8` 的典型旧形态包含：

```text
lsr w8, w22, #0
lsr w6, w29, #0
mul w22, w8, w6
```

`w22` 是 guest fixed home 的 W 视图，旧值在 `BitExtract` 处最后使用，`mul` 的 U32
结果又已经被现有 W-alpha 证明可直接发布回同一个 fixed home。新合并只在以下条件同时
成立时把 `BitExtract` 的物理寄存器移交给结果家：

- 低 32 位 `BitExtract` 只有一个 U32 `Add/Sub/And/AndNot/Or/Xor/Mul` consumer；
- source 在 bridge 处最后使用，bridge 在 consumer 处最后使用；
- consumer 已由现有 fixed-home 写事务映射到同一 target，后续 `SetHostGPR` 已被证明为
  零发射发布；
- allocator 重新执行 bridge 与 consumer 的 scratch/active-register 校验；
- emitter 独立重放 source、consumer、publication、owner 和 clobber 证明。

这不是把 pinned X 误当成 high-zero X。省略 bridge 只表示 consumer 读取同一个 W view；
真正的 U32 consumer 仍会写 Wtarget，并在那一刻清零 X 的高 32 位。需要旧操作数计算 AF
的 `add` 等形态会被现有校验拒绝，本轮没有扩大 faulting producer 白名单，也没有新增开关、
日志或回退路径。

## 续轮优化

CoreMark 的剩余热点包含以下形态：

```text
lsr w6, w0, #0
mov w29, w6
```

对应 IR 是相邻的 `BitExtract(source, 0, 32) -> ZeroExtend32To64`。扩展值可能继续参与地址
计算，不能把整个链误当成单用途 `SetHostGPR` 发布。新合并只移除低 32 位 identity bridge，
保留扩展节点的一条 `mov Wdst, Wsrc` 和它的全部后续使用：

- bridge 是 U32、`lsb=0`、`bits=32`，只有一个使用且该使用就是紧邻的扩展节点；
- source 与扩展结果都在 GPR，物理寄存器不同，且两者都不属于既有 width component；
- allocator 在提交 reference mapping 前扩展 source 的 active mask，并重新验证 bridge 与
  wrapper 的 scratch 契约；
- emitter 独立重放 opcode、source id、相邻关系、共享寄存器和 active-mask 证明；
- `BitExtract` 零发射，`ZeroExtend32To64` 强制保留 W move，因此 source 不被破坏、Xdst 高位
  仍按 x86 32 位写语义清零，后续 publication/fault 时序不变。

同时补齐既有 full-width `LoadMemory` 直接发布的 emitter producer 复核；allocator 的 faulting
白名单没有扩大。宽度链证明中三处从不同临时 `GetValues()` 容器取 begin/end 的未定义行为
改为持有同一个 values 容器后遍历，长链测试不再受对象布局影响。

## 正确性收紧

- flags carry-test 折叠在读取 `And/Or` 参数前先验证 opcode，非预期 IR 形态直接拒绝折叠。
- region successor-cover 不再把 `CondSet`、`CondSelect`、`Adc`、`Sbb` 当成 flags-transparent；
  `SetOverflow` 也不再进入 transparent fallback。

曾验证过把 successor-cover 扩成完整 fault/AdvancePC 闸门；它使 CoreMark 从约 6.300B
回退到 6.551B（约 +4.0%），因此撤销，只保留上述精确 observer 修正。

## 验证

- Orb 全量构建通过。
- 新 low32 copy 测试 6 assertions；GPR coalescing 353、width-chain 23、resident fault 21、
  W/X 高半部 17，全部通过。
- func_tests：FLAGS 0/1 × function/block/interpreter 六格均为 rc=101，checksum
  `9f52b7d59285dbe5`。
- helper-fault：38 passed，0 failed；clone futex/lock 在 FLAGS 0/1 下均 rc=0。
- smallpt 两臂输出 SHA-256 均为
  `3b1cb33bb83161a73fa45a23d92bcf3c690a75fbe631024bcc3e1962b3eed30e`。
- 相对精确优化前二进制的 function fingerprint：1664 units / 11 guests，PASS。
- 固定 `SWIFT_FUZZ_SEED=123456` 的 Orb 全套件：基线为 40 failed cases / 53 assertions，
  候选为 39 / 52，并新增 1 个通过的 test case。两条既有 width-chain 断言随临时容器 UB
  修复转绿；剩余差异仍是同类 VIXL 尾部反汇编自一致性抖动，没有新增语义失败类别。

## 下一阶段

1. PF/AF 独立家仍是 flags 主差距，但必须先给出 signal/fault/dispatcher 的 canonical
   park 配方和 FLAGS_REGS=0 rollback，不接受只在热路径删 BFI 的实现。
2. SHA 的主要差距是超块摊薄和公开入口状态义务。下一刀应先按当前 bounded-64 重新采集
   SHA 热块边界账，再决定扩大跨函数 region 还是做更细 fault map。
3. 用当前 FEX/SwiftVM 重新生成同 guest PC 的静态 blow-up 表，替换 8 月 14 日历史比值。
