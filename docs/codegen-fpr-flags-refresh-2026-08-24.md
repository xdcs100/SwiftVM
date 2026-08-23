# FPR publication 与 NZCV 密度优化

日期：2026-08-24

基线：`fe0dbab`

FEX 参照：`f2e35f3`，`FEX_HOSTFEATURES=disableavx`

## 当前差距

正式 smallpt 使用 `smallpt_wh_x64 8 128 96`。在 Orb 默认 region、无
`SVM_EXEC_PROF` 的生产指令口径下，优化前为 1,321,651,162 条 host 指令，spill 为 0。
剔除 density 入口计数与 cold path 后，动态责任表闭合误差为 0.0002%：

| 类别 | host 指令 | 占比 |
|---|---:|---:|
| work | 499,336,597 | 37.781% |
| boundary | 315,449,440 | 23.868% |
| move/width | 296,127,192 | 22.406% |
| uniform | 138,886,309 | 10.509% |
| flags IR | 71,849,500 | 5.436% |

`boundary` 中 terminal 为 146,626,738（11.094%），link 为 171,798,869
（12.999%）。`move/width` 中 FPR state 为 72,854,485（5.512%），其中
`SetHostFPR` 为 64,757,147（4.900%）。主要单 op 为 `GetOperand` 8.036%、
`StoreUniform` 6.244%、`VecFMulScalar64` 5.969%、`LoadMemory` 5.801% 和
`SetHostFPR` 4.900%。spill 不是当前差距来源。

c-ray 同口径为 134,666,060 条 host 指令：work 43.354%、move/width 25.503%、
boundary 22.116%、uniform 6.522%、flags IR 2.506%；FPR state 为 5.484%，
`SetHostFPR` 为 5.055%。

8 月 23 日静态表中的 smallpt 为 SVM/FEX 2.180×。本轮同 harness、同旧 TSV
权重的 RE=0 重采从 SVM 3.335622 host/guest 降至 3.267832；加入 RSB 返回目标复用后
按正式 host 权重折算约为 3.265287。以未变的 FEX 1.549 为分母，对应
2.153×→2.108×。旧表与这次重采的 unit 形成参数不完全相同，因此不直接覆盖原表；
按两种口径合看，当前正式 smallpt 距离 FEX 仍约 2.11–2.14×。

## 已落地

### Legacy scalar FPR publication

提交 `7110d20` 允许非 scalar-insert lowering 的完整 V128 scalar FP 结果直接拥有
resident XMM home。allocator 与 emitter 分别重证明；若 left 已占目标 home，则保留真实
publication，避免 legacy 多指令 lowering 覆盖仍需读取的高 lane。producer 与 publication
之间出现 memory/helper observer、冲突或额外存活值时仍拒绝。

这一项对正式 `smallpt_wh` 发码不变，但覆盖另一份固定 1024×768 smallpt：

- host 44,600,954,455→44,148,307,471，减少 452,646,984（1.015%）；
- 128 个共同 PC 只减不增，move 同量减少，spill 0→0；
- SwiftVM 两臂及 FEX PPM SHA-256 均为
  `fba9041fc8091204af5cccba5147d7fb84ac4d6abe2133b3e23ad2f2d1881a66`；
- c-ray 等 entries 口径减少 482,359 条，59 个共同 PC 只减不增，IDAT 不变。

### Full NZCV publication

提交 `7045d3b` 将完整 NZCV merge 从
`MRS + AND(flags) + AND(scratch) + ORR` 缩成三条。读取 `NZCV` 时除 [31:28]
外均为架构 RES0；完整请求下不再需要遮罩 scratch。部分请求继续走原四条路径。

| workload | 优化前 | 优化后 | 变化 |
|---|---:|---:|---:|
| CoreMark | 6,034,267,440 | 5,973,080,081 | −61,187,359（1.014%） |
| smallpt_wh | 1,321,651,162 | 1,303,939,990 | −17,711,172（1.340%） |
| c-ray 等 entries | — | — | −999,520 |

smallpt 有 1121 个共同 PC、c-ray 有 3909 个共同 PC 变小，均无增长 PC。move 与 spill
不变，说明收益精确来自 NZCV merge。

### VecZip resident publication

提交 `431be30` 把单条 `ZIP1/ZIP2` 纳入完整 resident FPR producer 集合。它与现有
`VecAnd/VecFAdd` 一样是单条三寄存器、完整 V128 写，沿用相同的存活、observer、冲突和
emitter 重证明。

- smallpt_wh 1,303,939,990→1,297,980,655，减少 5,959,335（0.457%）；
- 12 个共同 smallpt PC 只减不增；
- c-ray 等 entries 口径减少 124,308，29 个共同 PC 只减不增；
- CoreMark 仅运行尾部抖动，CRC final 保持 `0x382f`。

### Retained RSB return target

提交 `fa1768a` 让 dynamic `SetLocation` 穿过无副作用的 `PopRSB` 标记，并把仍驻留寄存器的
真实返回目标直接交给两种 RSB pop lowering。目标缺失、预测不符、空栈和 SMC 清空仍走原
dispatcher；终结段在 flags 合并前保留目标寄存器，避免 scratch 别名。

- smallpt_wh 1,297,980,655→1,296,969,640，减少 1,011,015（0.078%），328 个 PC
  只减不增，PPM 不变；
- `SVM_EXEC_PROF` 记录 8,080,989 次 RSB hit、248 次 miss，说明先前按静态块数估算的
  “仅 0.076% 上限”不是实际执行上限；
- CoreMark 等 entry 口径减少 25,442,605，327 个共同 PC 只减不增，CRC final 不变；
- call-dense workload 在 lean/default RSB frame 下均精确减少 64,000,000 条 host 指令，
  checksum 均为 `0xee79813b94536693`；
- c-ray 等 entry 口径减少 414,747，967 个共同 PC 只减不增，IDAT 不变。

四项合计使正式 smallpt 默认 region host 减少 24,681,522（1.867%）。

## 否决项

直接放开 Linux AFP scalar insert 曾使 c-ray host 134,666,060→132,076,475，
但 smallpt 输出与 FEX 分离；单独关闭 scalar tie 后仍是同一错误输出，证明问题在 scalar
insert 契约而非 RA tie。该原型已完整删除。

剩余完整 `SetHostFPR` 中 `VecFUnary` 仅 0.163%，其 lowering 不是单条完整写；不把
producer 白名单泛化到缺少原子性证明的 opcode。

## 验证

- 新 FPR 责任测试 3 cases / 10 assertions；resident XMM coalescing 1 case / 794
  assertions；完整 NZCV 结构测试 1 case / 3 assertions，全部通过。
- func_tests：FLAGS 0/1 × function/block/interpreter 六格均 rc=101，checksum
  `9f52b7d59285dbe5`。
- helper-fault 38 passed / 0 failed；clone futex/lock 在 FLAGS 0/1 下均 rc=0。
- function fingerprint 对阶段基线保持 1664 units / 11 guests，自一致且逐项匹配。
- RSB/indirect 结构测试 21 assertions；显式改写栈返回地址的临时 probe 在默认、
  `SVM_SHADOW_LEAN=0` 和 `SVM_FLAGS_REGS=0` 下均 rc=0，probe 已删除。
- `SWIFT_FUZZ_SEED=123456` 全套件最终为 183 passed / 35 个既有 failed cases；
  1,047,651 passed / 44 failed assertions。与前臂相比没有新增失败类别。
- smallpt_wh PPM SHA-256 两臂均为
  `fe96f7e48295b27c8df8236294052d138c3ed130b81d022739907fe6b2cde5aa`；
  c-ray IDAT MD5 两臂均为 `54256cb4b3c6313a65ea12ebb7b81e30`；STREAM
  `Solution Validates`。
- Mac `swift_runtime` 与 Orb 全目标构建通过。

## 下一步

1. 正式 smallpt 的 boundary 仍约 23%，其中 link 约 13%。返回目标回读已经移除；下一步
   分别审计 call push 与公开出口的具体子路径，只接受具有独立 fault/SMC/RSB 证明的形状。
   direct-link、region 扩窗与跨边 state forwarding 的历史 NO-GO 结论不因总桶较大而自动重开。
2. 剩余 `SetHostFPR` 约 5%，但完整写仅约 2.3%，其余主要是低/高 64-bit lane 的真实
   architectural publication。继续扩 producer 前先给出单指令完整写与目标别名证明。
3. CoreMark 的 8/16-bit truncation 仍要求 consumer-specific 物理高位证明，并保留 U16
   CallLambda 回归门。
4. SHA 必须先换成能真正进入 hashing 的合法 workload；不使用当前 PageFatal 前的路径计数。
