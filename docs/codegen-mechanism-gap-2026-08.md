# SVM vs FEX 机制级差距分解(2026-08-14)

承接 docs/codegen-quality-vs-fex-2026-08.md 的 blow-up 表(几何均值 2.01×),
把每条 guest 指令的 host 指令差值拆到机制类别。双侧均为确定性静态计数,
无任何墙钟依赖。

## 数据基础

- **SVM 侧**:RE=0 gap-op census(/private/tmp/svm-gapop/),逐 IR op 的发码
  字节/4 = host 指令数,按机制桶归类;move 行做 exclusive 处理(overlap =
  功能桶内已含的 mov,从 move_static 扣除),每桶精确闭合到 host_static。
- **FEX 侧**:测量构建 FEX_BLOCKDISASM 直出 vixl 反汇编(orb /tmp/fex-disasm/),
  按 mnemonic 分类;literal pool/udf 填充已剔除(非执行代码)。
- 匹配与加权同 blow-up 表:SVM 热 PC 落入 FEX 块区间,entries 加权;
  FEX_HOSTFEATURES=disableavx 对齐 guest 路径。

## 主表:机制桶 h/g 对照(entries 加权,exclusive)

| 机制桶 | coremark SVM/FEX | sqlite SVM/FEX | zip7 SVM/FEX | osslsha SVM/FEX |
|---|---|---|---|---|
| **alu+vector(含 flags 打包算术)** | 2.50 / 0.46 | 2.72 / 0.40 | 1.81 / 0.56 | 3.20 / 0.69 |
| **publish_commit**(SetHost*/StoreUniform) | 0.39 / 0 | 0.66 / 0 | 0.51 / 0 | 0.40 / 0 |
| **state_read**(GetHost*/LoadUniform) | 0.21 / 0 | 0.24 / 0 | 0.39 / 0 | 0.15 / 0 |
| move/transport(排他) | 0.68 / 0.37 | 0.92 / 0.34 | 0.45 / 0.20 | 0.13 / 0.28 |
| guest_memory | 0.22 / 0.43 | 0.42 / 0.61 | 0.23 / 0.29 | 0.27 / 0.25 |
| imm | 0.14 / 0.06 | 0.16 / 0.17 | 0.09 / 0.03 | 0.07 / 0.08 |
| flags 专项(SaveFlags/TestFlags 等) | 0.03 / 0.04 | 0.06 / 0.02 | 0.04 / 0.01 | 0.07 / 0.01 |
| control/exit(FEX 侧;SVM 未入 gap-op 账) | 0 / 0.33 | 0 / 0.40 | 0 / 0.14 | 0 / 0.14 |
| **合计** | **4.17 / 1.69** | **5.19 / 1.95** | **3.52 / 1.23** | **4.36 / 1.45** |

smallpt/cray 同型(smallpt 3.44/1.46、cray 3.92/1.50,alu 行分别 +1.40/+1.59)。

## 三大机制差距

### ① ALU/发射经济性:+1.2 ~ +2.5 h/g(总差距的 60~85%)

FEX 每条 guest 指令只摊 0.4~0.7 条 host ALU;SVM 摊 1.8~3.2。SVM alu 桶
内部构成(op 级普查,全块未加权):Sub 10.5%、And 6~8%、Or/Xor 各 2~4%——
大头不是"真计算",而是:

- **窄 flags 路径的打包算术**:packed PF/AF 位构造经普通 And/Or/Xor IR op
  发射(计在 alu 桶,不在 flags 专项桶);Sub 居首=窄宽 flags 路径多发码;
- **fallback mov+alu 对**:RA 未融合的运算先拷后算(FEX 同形态被
  KillMove/coalescing 吃掉);
- **width bridge 残余**:32/64 位宽度衔接的掩码与移位。

FEX 对应优势:SRA 下操作数恒在家无需搬运、RA coalescing、shifted-operand
融合、RFCE 消除冗余 flags 计算。

### ② 状态访问模型(publish + state_read):+0.5 ~ +1.1 h/g,FEX 恒为 0

SVM 25~30% 的 host 指令在读写 guest 寄存器状态(op 级:SetHostGPR
9.7~10.6%、GetOperand 7.6~9.2%、StoreUniform 4.2~4.7%、LoadUniform ~3%)。
FEX 的 SRA(静态寄存器映射)让 guest GPR 恒驻 host 寄存器,这类代码
**一条都不发**。SVM 侧已证不可局部消除:fixed-home commit 是 fault 可见性
义务(osslsha 双归零审计),重开需 per-fault FPR/GPR recovery recipe 基建。

### ③ 块边界固定税:AdvancePC 8.6~9.5% + SetLocation 4~6.3% + PushRSB 4.2~8.6%

SVM 每块/每调用的固定成本(op 级,host inst 占比):
- **AdvancePC**:每块 PC 推进发码;
- **SetLocation**:逐 op 的位置/元数据簿记;
- **PushRSB**:软件返回栈(写栈 + L2 dispatch slot 预留);FEX 用 guest
  返回地址 + block link 预测,**不维护软件 RSB**。

FEX 超块(171~2604 guest inst/块,coremark 均 171、zip7 达 1605、osslsha
超块 2604)把同类成本摊薄一个量级;SVM 16-block region 切割下固定税
按块重现。这是同一基建问题:块/region 成形跨度。

## SVM 不吃亏的项

- **guest_memory**:SVM 各语料低 0.06~0.21 h/g——访存翻译路径比 FEX 精简;
- **flags 专项**(显式 flags op):双侧均 <0.1 h/g,lazy NZCV 与 FEX deferred
  flags 同量级;SVM 的 flags 真实成本藏在 ① 的打包算术里;
- **stream 语料整体反胜 0.925×**(blow-up 表):简单密集向量循环上
  SVM 直译已优于 FEX。

## 结论

2.25× 差距 = **状态访问模型(②)+ 块边界摊薄(③)+ 发射融合度(①)**,
三者同根:FEX 的 SRA + 超块让"每条 guest 指令"几乎只剩语义本体,而 SVM
每块要为状态可见性、PC 簿记、RSB、fault 恢复各付一遍固定税。局部 peephole
已证触不到这些(方向空间封存),重开路径即在封存清单的前置条件:
per-fault recovery recipe(解②)、更长 region/超块成形(解③)、
SRA 形态"值从第一行起在家"(解①②的共同基建)。
