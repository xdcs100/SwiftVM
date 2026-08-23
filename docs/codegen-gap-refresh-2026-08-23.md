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

本轮 Orb Linux 默认配置、禁用 JIT disk cache 和 `SVM_EXEC_PROF`，CoreMark 使用
`0x0 0x0 0x66 20000 7 1 2000`：

| 指标 | 优化前 | 本轮优化后 | 变化 |
|---|---:|---:|---:|
| host dynamic | 6,299,957,565 | 6,250,517,460 | -49,440,105 (-0.785%) |
| move dynamic | 2,204,881,795 | 2,155,441,690 | -49,440,105 |
| move 占比 | 34.998% | 34.484% | -0.514 pp |
| spill dynamic | 0 | 0 | 0 |

按优化前 entries 对共同 PC 重算，129 个 PC 变小、0 个变大，精确减少 49,440,109 条
host/move 指令。收益最大的五个 PC 为 `0x402df8`、`0x402de8`、`0x403390`、
`0x402d58`、`0x402d48`。

其他确定性语料的共同 PC 结果：

| workload | 共同 PC 减少 | 增长 PC | 动态 host/move 变化 |
|---|---:|---:|---:|
| STREAM | 95 | 0 | -296，接近中性 |
| smallpt | 117 | 0 | -23,934,754 (-0.284%) |
| c-ray | 483 | 0 | -4,410,627（约 -0.62%） |

7-Zip 和 OpenSSL speed 在当前 Linux launcher 下仍会进入既有 signal/timer 退出路径；
7-Zip 的相同早退样本减少 254 条，OpenSSL 优化臂可能超时，均不作为完整收益证据。

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

## 正确性收紧

- flags carry-test 折叠在读取 `And/Or` 参数前先验证 opcode，非预期 IR 形态直接拒绝折叠。
- region successor-cover 不再把 `CondSet`、`CondSelect`、`Adc`、`Sbb` 当成 flags-transparent；
  `SetOverflow` 也不再进入 transparent fallback。

曾验证过把 successor-cover 扩成完整 fault/AdvancePC 闸门；它使 CoreMark 从约 6.300B
回退到 6.551B（约 +4.0%），因此撤销，只保留上述精确 observer 修正。

## 验证

- macOS 与 Orb 构建通过。
- 新定向测试 6 assertions，GPR coalescing 353、width-chain 23、resident fault 21，全部通过。
- func_tests：FLAGS 0/1 × function/block/interpreter 六格均为 rc=101，checksum
  `9f52b7d59285dbe5`。
- helper-fault：38 passed，0 failed；clone futex/lock 在 FLAGS 0/1 下均 rc=0。
- c-ray `-s 8 -j 1 -d 160x120` 完整输出，smallpt 与 STREAM 完整输出。
- 相对精确优化前二进制的 function fingerprint：1664 units / 11 guests，PASS。
- 固定 RNG 的 Orb 全套件：优化前后均为 36 个既有失败 case；定向新增 case 通过，未观察到
  新失败类别。该套件当前仍受既有配置默认值和 VIXL 尾部反汇编测试失败影响。

## 下一阶段

1. 对 `BitExtract -> ZeroExtend32To64 -> SetHostGPR` 的跨家 copy 做独立 census。它可把
   两条 `lsr + mov` 收成一条 W move，但必须复用同样的 publication/fault 事务，不能从
   本轮 destructive handoff 机械放宽。
2. PF/AF 独立家仍是 flags 主差距，但必须先给出 signal/fault/dispatcher 的 canonical
   park 配方和 FLAGS_REGS=0 rollback，不接受只在热路径删 BFI 的实现。
3. SHA 的主要差距是超块摊薄和公开入口状态义务。下一刀应先按当前 bounded-64 重新采集
   SHA 热块边界账，再决定扩大跨函数 region 还是做更细 fault map。
4. 用当前 FEX/SwiftVM 重新生成同 guest PC 的静态 blow-up 表，替换 8 月 14 日历史比值。
