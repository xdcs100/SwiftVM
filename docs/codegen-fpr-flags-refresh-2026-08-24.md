# FPR publication、NZCV 与边界密度优化

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
权重的 RE=0 重采从 SVM 3.335622 host/guest 降至 3.267832；加入 RSB 返回目标复用、
静态出口 direct-link、默认 return-L1、cycle successor layout、indirect-L1 状态成对加载和
live resident-FPR publication 后，按正式 host 权重折算约为 2.989612。以未变的 FEX
1.549 为分母，对应 2.153×→1.930×。
旧表与这次重采的
unit 形成参数不完全相同，
因此不直接覆盖原表；按两种口径合看，当前正式 smallpt 距离 FEX 约 1.93–1.97×。

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

### Static SetLocation direct link

提交 `729b826` 把 `SetLocation(imm) + ReturnToDispatch` 的同 module 静态出口接入现有
tracked direct-link。`JitContext` 只保留一份 site 发射与 flags-audit 逻辑；终结段继续在
成环边的 site 前发 acquire poll，并传递条件臂 `LinkSiteKind`。direct-link region 不可用、
BlockLink 关闭、自环或跨 module 时，仍走原 inline L2 或 dispatcher 路径。

- smallpt_wh 1,296,969,640→1,246,900,800，减少 50,068,840（3.860%）；998 个
  共同 PC 只减不增，entries 与 units 完全一致，PPM SHA-256 不变；
- smallpt 的 inline L2 `link_hit` 12,191,734→31,816、`link_miss` 227→0；direct、
  indirect、call、ret、RSB、dispatcher 和 region-edge 执行计数逐项不变；
- CoreMark 等 entry 减少 210,324,020，966 个共同 PC 只减不增，CRC final 为 `0x382f`；
- c-ray `scene.json -j 1 -s 64 -d 320x240` 等 entry 减少 574,050,830，4017 个
  共同 PC 只减不增，IDAT MD5 为 `d0c71130abf3544a86b64417bc488c21`；
- STREAM 总量减少 18,740，859 个共同 PC 只减不增，`Solution Validates`；
- call-dense 3,344,000,774→3,104,000,764，减少 240,000,010，checksum 仍为
  `0xee79813b94536693`。

### Default returns through inline L1

提交 `24f9d49` 让 `indirect_l1` 模块的 call 不再生成 RSB frame，`ret` 直接复用已保留的
真实目标并走现有 inline L1。L1 快路径自带 `LDAR/TBNZ` signal safepoint，key mismatch
与 SMC-invalid value 仍退回 dispatcher/L2。若目标寄存器不可保留则直接 `Ret`；只有显式
`SVM_INDIRECT_L1=0` 的模块继续生成并消费 RSB frame，避免跨 module 留下 stale frame。

FEX `f2e35f3` 的两条 shadow-stack push 依赖 4 MiB call-ret mapping 和 guard-page fault
恢复；SwiftVM 当前只有 64-frame 普通数组，不能安全照抄删边界检查。默认 return-L1 则在
不引入异常恢复和 host-PC frame 的前提下移除整段 push，并缩短 pop。

- smallpt_wh 1,246,900,800→1,201,575,549，减少 45,325,251（3.635%）；881 个
  共同 PC 只减不增，entries 与 units 完全一致，PPM 不变；
- return 与普通间接出口合计 L1 profile 为 8,978,713 hit / 420 miss（99.9953%）；生产
  EXEC 中 RSB 8,080,989/248→0/0，仅增加 100 次 L2 hit/dispatcher，其他 exit、region 和
  guest-state 计数逐项不变；
- CoreMark 等 entry 减少 169,232,701（总量减少 169,232,746），892 个共同 PC
  只减不增，CRC final 为 `0x382f`；
- 64-spp c-ray 等 entry 减少 566,348,759（总量减少 566,459,798），3450 个共同 PC
  只减不增，IDAT 不变；
- STREAM 等 entry 减少 5,480、823 个共同 PC 只减不增，`Solution Validates`；
- call-dense 3,104,000,764→2,752,000,752，减少 352,000,012（11.340%），checksum
  不变；`SVM_INDIRECT_L1=0` 两臂均为 3,104,000,764，逐指令一致。

### Cycle-polled successor layout

提交 `b3998d5` 区分物理 successor layout 与真实 fallthrough。条件边的 then 目标若正好是
下一个 region block、同时承担 direct cycle poll，旧形态需要先跳到 then stub，并在非 then
臂再发一条无条件跳转。新形态反转条件直接跳向另一臂，随后保留原 `LDAR/CBNZ + B target`。
每块 cold stub 仍紧跟 hot body，因此 poll 后不得真实 fallthrough；signal、SMC 和冷出口均未
移动或删减。

- smallpt_wh 1,201,575,549→1,201,372,215，减少 203,334（0.0169%）；127 个共同 PC
  各少一条、0 个增长，entries 与 units 完全一致，PPM 不变；
- smallpt 静态 region local branch bytes 5,732→5,140，cycle edges/poll bytes 保持
  518/4,144；生产 EXEC 的 exit、region edge、cycle poll 和 fallthrough 逐项一致；
- CoreMark 等 entry 减少 51,081,278，122 个共同 PC 只减不增，CRC final 为 `0x382f`；
- 64-spp c-ray 等 entry 减少 328,719，424 个共同 PC 只减不增，IDAT 不变；
- STREAM 等 entry 减少 1,412，112 个共同 PC 只减不增，`Solution Validates`。

### Paired indirect-L1 state load

提交 `3ec9582` 将 `exit_request` 与 `indirect_l1_code_cache` 放在 `State` 的首个 16-byte
pair 中。production inline L1 用一条 `LDP` 同时取得请求字和 L1 基址，再用 `TBNZ`
筛出 signal；命中 signal 时先返回共享 trampoline，由其 offset-zero `LDAR` 确认请求并
返回 `Signal`。profile 路径保持原来的独立 L1 基址加载，不改变其计数语义。快路径从九条
降为八条，cache key/value 与 SMC-invalid 检查保持不变。

- smallpt_wh 1,201,372,215→1,199,466,420，减少 1,905,795（0.1586%）；376 个共同
  PC 各少一条、0 个增长，entries、units 和 PPM 均一致；
- CoreMark 等 entry 减少 31,825,037，373 个共同 PC 只减不增，CRC final 为 `0x382f`；
- 64-spp c-ray 等 entry 减少 91,110,798，1,137 个共同 PC 只减不增，IDAT 不变；
- STREAM 等 entry 减少 879，341 个共同 PC 只减不增，`Solution Validates`；
- call-dense 等 entry 精确减少 64,000,000，6 个共同 PC 只减不增，checksum 不变；
  scale-10 十次 wall-time 中位数 1.045108s→1.044901s，未出现 `LDAXP` 原型的退化。

### Live resident-FPR publication

提交 `e74e734` 允许完整 V128 producer 在 `SetHostFPR` 后仍有普通 SSA use 时继续占用
目标 resident home。publication 前仍拒绝 fault/helper observer、固定家读写和重叠 live
interval；publication 后若同一目标在 producer 最后 use 前再次写入也拒绝。一个 SSA 一旦
占用 resident home，不能再被后续 publication 改绑到另一个 home。emitter 独立复算同一
窗口，`SetHostFPR` 只在两侧证明一致时消失。

- smallpt_wh 1,199,466,420→1,187,471,711，减少 11,994,709（1.0000%）；79 个共同
  PC 缩短、0 个增长，entries、units、spill 和 PPM 均一致；
- 删除量主要来自完整 `LoadMemory` 4,704,551、`VecFMul` 1,550,057、`VecFAdd`
  1,390,954、scalar FP 1,661,934、`VecXor` 851,875、`VecZip` 833,261 和
  `LoadUniform` 696,744；
- 320×240、64-spp c-ray 等 entry 减少 136,043,687，174 个共同 PC 缩短、0 个增长，
  IDAT MD5 保持 `d0c71130abf3544a86b64417bc488c21`；
- STREAM 等 entry 减少 199，26 个共同 PC 缩短、0 个增长，`Solution Validates`；
  CoreMark 在当前口径基本中性，CRC final 保持 `0x382f`。

九项合计使正式 smallpt 默认 region host 减少 134,179,451（10.152%）。

## 否决项

直接放开 Linux AFP scalar insert 曾使 c-ray host 134,666,060→132,076,475，
但 smallpt 输出与 FEX 分离；单独关闭 scalar tie 后仍是同一错误输出，证明问题在 scalar
insert 契约而非 RA tie。该原型已完整删除。

剩余完整 `SetHostFPR` 中 `VecFUnary` 仅 0.163%，其 lowering 不是单条完整写；不把
producer 白名单泛化到缺少原子性证明的 opcode。

## 验证

- 新 live-publication 窗口测试 3 cases / 9 assertions；既有 FPR 责任测试 3 cases / 10
  assertions、resident XMM coalescing 794 assertions、scalar fixed-home tie 90 assertions、
  resident fault/snapshot 27 assertions，Mac 与 Orb 全部通过。
- func_tests：FLAGS 0/1 × function/block/interpreter 六格均 rc=101，checksum
  `9f52b7d59285dbe5`。
- helper-fault 38 passed / 0 failed；clone futex/lock 在 FLAGS 0/1 下均 rc=0。
- function fingerprint 对阶段基线保持 1664 units / 11 guests，自一致且逐项匹配。
- RSB/indirect 结构测试 26 assertions，覆盖八指令 L1 快路径、无 push 和无目标
  dispatcher 路径；显式改写栈返回地址的临时 probe 在默认、L1-off 两种 RSB frame、
  FLAGS-off 和 interpreter 下均 rc=0，probe 已删除。
- 新增 production inline-L1 signal 测试 6 assertions；FLAGS=0 下 direct-link production
  全标签 11 cases / 395 assertions，默认 SMC 子集 5 cases / 268 assertions。静态
  SetLocation 的跨 module/BlockLink-off fallback 为 36 assertions，反复摘链/重编译为
  142 assertions，Mac 与 Orb 均通过。
- region edge、direct cycle signal 和 region flags 专项分别通过 42、30、46 assertions；
  cycle successor layout 的 function fingerprint 对基线保持 1664 units / 11 guests 一致。
- 200 轮 `smc_mt_stress` 为 host_fails/guest_lost/timeouts = 0/0/0。
- Catch 与 fuzz seed 同为 424242 时，候选为 187 passed / 35 个既有 failed cases、45 个
  失败断言，仍是既有 VIXL 尾部反汇编自一致性和 jit-cache/lazy-flags 配置类别。同一
  222-case 注册表上的旧实现为 185 / 37、47 个失败断言，其中两个正是新增窗口测试；
  候选没有新增失败类别。
- smallpt_wh PPM SHA-256 两臂均为
  `fe96f7e48295b27c8df8236294052d138c3ed130b81d022739907fe6b2cde5aa`；
  原 equal-entry c-ray sample 的 IDAT MD5 两臂均为
  `54256cb4b3c6313a65ea12ebb7b81e30`，64-spp 正式 harness 两臂均为
  `d0c71130abf3544a86b64417bc488c21`；STREAM `Solution Validates`。
- Mac `swift_test` target 与 Orb 全目标构建通过。

## 下一步

1. live FPR publication 后，正式 smallpt 的已覆盖 link 约 6.09%。region/cycle
   link tail 约 1.92%，其中 acquire poll 与跨本块 cold stub 的目标跳转不可直接删除；
   八条 return-L1 静态序列约 1.39%，其中 `AND + ADD + LDP + CMP + CSEL + BR` 没有
   明确的基础 ISA 融合机会。公开 host exit 仍仅 139 次。
2. 剩余 `SetHostFPR` 为 46,803,103（3.941%），其中完整写仅 12,355,778（1.041%）。
   最大两桶是 low-64 `LoadMemory` 16,927,380 和 high-64 zero 16,379,255；下一步先确认
   它们是否为同目标 scalar-load/zero-high pair，再审计能否由一条写 D 寄存器的 load 覆盖。
3. CoreMark 的 8/16-bit truncation 仍要求 consumer-specific 物理高位证明，并保留 U16
   CallLambda 回归门。
4. SHA 必须先换成能真正进入 hashing 的合法 workload；不使用当前 PageFatal 前的路径计数。
