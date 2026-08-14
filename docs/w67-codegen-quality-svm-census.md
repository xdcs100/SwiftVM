# SVM 热块代码质量普查：8 语料 RE=0 blow-up 底账

## 0. 结论先行

八语料两轮采集完成。最终 TSV 只保留两轮都出现、且
`host_static/guest_inst/move_static/spill_static` 四个静态字段逐位一致的 PC。
最终共交付 **58,473 个热块**；每个 TSV 都按第一轮 `entries` 降序，覆盖率均
大于 99.99998%，严格高于“至少 top-10 且累计 entries ≥95%”门。

本报告不含墙钟结论。机器受载，所有比较只使用 RE=0 的确定性计数。

| 语料 | 最终热块数 | 达 95% entries 所需块数 | 最终 TSV 对第一轮 entries 覆盖 | top-1 PC | top-1 host/guest |
|---|---:|---:|---:|---:|---:|
| coremark | 1,698 | 106 | 99.999999918% | `0x402680` | 17/6 = 2.833333333 |
| stream | 1,436 | 2 | 100.000000000% | `0x416980` | 42/14 = 3.000000000 |
| smallpt | 1,766 | 176 | 100.000000000% | `0x41ef00` | 48/8 = 6.000000000 |
| sqlite | 21,456 | 2,456 | 100.000000000% | `0x4a5149` | 34/5 = 6.800000000 |
| cray | 8,272 | 265 | 99.999986228% | `0x402fc0` | 26/11 = 2.363636364 |
| zip7 | 5,961 | 314 | 100.000000000% | `0x42d0b0` | 12/5 = 2.400000000 |
| osslsha | 8,931 | 332 | 100.000000000% | `0x8ba580` | 723/170 = 4.252941176 |
| osslaes | 8,953 | 244 | 99.999999361% | `0x634960` | 72/51 = 1.411764706 |

## 1. 交付文件

每个 TSV 的列固定为：

```text
pc  entries  host_static  guest_inst  host_per_guest  move_static  spill_static
```

文件：

- `codegen-quality-svm-coremark.tsv`
- `codegen-quality-svm-stream.tsv`
- `codegen-quality-svm-smallpt.tsv`
- `codegen-quality-svm-sqlite.tsv`
- `codegen-quality-svm-cray.tsv`
- `codegen-quality-svm-zip7.tsv`
- `codegen-quality-svm-osslsha.tsv`
- `codegen-quality-svm-osslaes.tsv`

`host_per_guest` 对每行按 `host_static / guest_inst` 独立计算并保留 9 位小数；
没有用汇总比值回填单块数据。

## 2. guest_inst 定义

### 2.1 精确定义

`guest_inst` 是该 guest basic block 中，x86 decoder **成功 lower 并 commit**
的架构指令数：

- raw/CET/VEX/SHA 等 predispatch 路径返回 `Handled` 时加 1；
- distorm 路径仅在 `DecodeDistormInstruction()` 成功后加 1；
- fetch、非法指令或 PAGE_FATAL 导致的未完成 decode 不计；
- terminal guest 指令完成 lower 后计入；
- 计数发生在 `AdvancePC` folding、HIR optimization、RA 和 emitter 之前。

因此它不是 IR instruction 数，也不会把一条 x86 指令展开出的多个
LoadUniform/SetHost/flags IR 节点重复计数。

采集时在 `source/runtime/frontend/x86/decoder.cc` 的
`DecodePipeline::Run` 成功路径维护临时 block-local counter，并在现有
`SVM_RA_HOT_COALESCE_ALL=1` 门下打印：

```text
[svm-guest-inst] pc=0x... guest_inst=N end=0x...
```

每次运行中 `[svm-hot-all]` PC 集合与 `[svm-guest-inst]` PC 集合严格一一
对应；八语料第一轮分别是 1,702/1,436/1,766/21,456/8,286/5,961/
8,931/8,960 个 PC，没有缺 join。

### 2.2 osslsha 0x8ba580 自洽验证

两轮 decoder 都得到：

```text
[svm-guest-inst] pc=0x8ba580 guest_inst=170 end=0x8ba85a
```

独立使用宽格式 guest 反汇编复算：

```sh
objdump -dw --start-address=0x8ba580 --stop-address=0x8ba85a \
  /Users/swift/CLionProjects/SwiftVM-bench/bin/openssl_x64
```

得到 **170 条**真实指令行，与 decoder 完全一致。普通非 wide objdump 会把
8 条八字节 `movdqa disp32` 的最后一个 `00` 换行显示，机械数地址行会误报
178；本报告明确使用 `-w` 消除该格式陷阱。

该块最终 TSV 行为：

```text
0x8ba580  4085043  723  170  4.252941176  183  0
```

即当前 SVM RE=0 形态为 **723 host / 170 guest = 4.252941176×**。这给后续
FEX 同 PC 汇合提供了可直接复算的 guest 分母。

## 3. 统一采集口径

每臂均使用：

```sh
env -u SVM_JIT_CACHE -u SVM_EXEC_PROF \
  SVM_REGION_EDGES=0 SVM_PROF=2 SVM_DENSITY_PROF=1 \
  SVM_RA_HOT_COALESCE=<run>/hot.log SVM_RA_HOT_COALESCE_ALL=1 \
  build/source/translator/linux/svm_translator_linux <guest> <args>
```

参数：

| 语料 | 参数 |
|---|---|
| coremark | `0 0 0x66 150000 7 1 2000` |
| stream | 无参数 |
| smallpt | `smallpt_wh_x64 8 320 240` |
| sqlite | `--size 100 --testset main,orm/25,cte/20,json,fp/3,parsenumber/25,star,app <fresh-db>` |
| cray | `scene.json -j 1 -s 8 -d 160x120 --no-sdl` |
| zip7 | `b -mmt1 -md=16m` |
| osslsha | `speed -seconds 1 -bytes 8192 -evp sha256` |
| osslaes | `speed -seconds 1 -evp aes-128-gcm`，按 harness 跑完整 size 表 |

osslaes 曾试跑 `-bytes 8192`，该 OpenSSL/GCM 组合输出 0.00，已判为无效臂、
完全排除；最终 TSV 来自 harness 原形完整 size 表的两次新跑。

原始日志位于 `/private/tmp/codegen-census/runs/<bench>/<1|2>/`。

## 4. 双跑 digit-exact 验证

| 语料 | run1 PC | run2 PC | 静态逐位相同的公共 PC | 最终有 entries 行 | 被排除的第一轮 entries |
|---|---:|---:|---:|---:|---:|
| coremark | 1,702 | 1,705 | 1,698 | 1,698 | 8 |
| stream | 1,436 | 1,436 | 1,436 | 1,436 | 0 |
| smallpt | 1,766 | 1,766 | 1,766 | 1,766 | 0 |
| sqlite | 21,456 | 21,456 | 21,456 | 21,456 | 0 |
| cray | 8,286 | 8,276 | 8,272 | 8,272 | 47 |
| zip7 | 5,961 | 5,961 | 5,961 | 5,961 | 0 |
| osslsha | 8,931 | 8,932 | 8,931 | 8,931 | 0 |
| osslaes | 8,960 | 8,954 | 8,953 | 8,953 | 8 |

公共 PC 的以下 tuple 全部逐位相等，没有一个 mismatch：

```text
(host_static, guest_inst, move_static, spill_static)
```

时间/signal 引入的极冷 PC 集合差异不进入最终 TSV。被排除的第一轮权重
在 coremark/cray/osslaes 分别只有 8/47/8 entries，因此最终覆盖仍近似
100%，且 top-10 全部经过双跑复证。

## 5. entry-weighted 全局底账

以下只用于给各语料一个便于比较的总体尺度，计算式为：

```text
weighted host/guest = Σ(entries × host_static) / Σ(entries × guest_inst)
move_pct = Σ(entries × move_static) / Σ(entries × host_static)
spill_pct = Σ(entries × spill_static) / Σ(entries × host_static)
```

| 语料 | entry-weighted host/guest | move_pct | spill_pct |
|---|---:|---:|---:|
| coremark | 4.165675196 | 35.883467% | 0.000000002% |
| stream | 3.086609958 | 21.094471% | 0.000000000% |
| smallpt | 3.443106173 | 29.534856% | 0.001659681% |
| sqlite | 5.192483660 | 29.146139% | 0.195018353% |
| cray | 3.915723018 | 34.285761% | 0.060843539% |
| zip7 | 3.517977615 | 39.263660% | 0.169211701% |
| osslsha | 4.321100471 | 25.378631% | 0.000003439% |
| osslaes | 3.080984795 | 19.954868% | 0.000000548% |

这些是 RE=0 block-quality 口径，不与 region 默认态动态密度混用。

## 6. Oracle 与清理

两轮均 `rc=0`，并观察到：

- coremark：`Correct operation validated`，`crcfinal=0x25b5`；
- stream：`Solution Validates`；
- smallpt：`image.ppm` 完整生成；
- sqlite：完整 `TOTAL`；
- cray：`Finished render in` 且 `rendered_0000.png` 存在；
- zip7：`Tot:`；
- osslsha：`sha256` 行；
- osslaes：完整 AES-128-GCM 六段表行（本构建 16B 列为 0.00，其余五列有吞吐；不影响本单静态计数）。

临时 decoder counter 已机械移除，`decoder.cc/decoder.h` 中不存在
`svm-guest-inst` 或 `census_guest_instructions`，随后全量增量构建成功：

```text
[100%] Built target swift_test
```

未修改默认发码、FeatureSet/env、docs、golden、harness、linker 或 w68。
最终新增文件仅本报告与八份 TSV；本任务没有生产源码 tracked diff。
