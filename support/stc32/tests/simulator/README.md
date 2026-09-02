# 模拟器指令级测试(Phase 3 迭代)

## 运行方式

```bash
# 1) 汇编 + 链接(用 build/ 的 sdas251 / sdld)
build/bin/sdas251 -plosg tests/simulator/regress2.asm   # 在 tests/simulator/ 内运行
build/bin/sdld -nf regress2.lk                          # 见下方 .lk 内容

# 2) 模拟执行 45 步并检查
build/sim/ucsim/src/sims/s51.src/ucsim_51 -t 251 -c - regress2.ihx <<< "s 45 / rd / dump iram 0x30 0x40 / dump iram 8 15 / quit"
```

`regress2.lk`:

```
-i regress2.ihx
-b MCS251CODE = 0x0000
regress2.rel
```

## 期望结果(全部通过)

| 检查点 | 期望 | 实际 |
|---|---|---|
| Stop at | spin 自旋地址 | ✓ |
| R0 | 0x40(mov r0,#0x40) | ✓ |
| R2 / R3 | 0x11 / 0x11 | ✓ |
| WR4 / WR6(R4-R7 大端) | 0x1234 / 0x1234 | ✓ |
| DR8(R8-R11) | 0x0000abcd(#0data16) | ✓ |
| DR12(R12-R15) | 0x0000abcd(mov dr,dr) | ✓ |
| iram[0x30] / [0x31] / [0x40] | 0x11 / 0x11 / 0x11 | ✓ |
| iram[0x33] | 0xdd(算术链结果) | ✓ |
| ACC | 0xff(clr a + cpl a) | ✓ |
| PSW CY | 1(cpl c) | ✓ |

## 覆盖指令

MOV(A/Rn/Rm/dir8/@Ri/WRj/DRk 各形式)、ADD/ANL/ORL/XRL/SUBB(A,#data/dir8/Rn)、
INC/DEC(A/Rn/dir8)、CLR/CPL(A/CY)、WR/DR 大端元组、#0data16/#1data16 装载、
寄存器间移动(7C/7D/7F)、A5 前缀全功能、sjmp 自旋。

## 注意

- 模拟器解码规则与汇编器编码(`support/stc32/isa/mcs251.yaml`)的对应关系记录在
  `sim/ucsim/src/sims/s51.src/uc251cl.h` 头部注释。
- 测试程序刻意把算术结果存入内存后再做 WR/DR 操作,避免 DR0(R0-R3)
  重叠寄存器干扰断言。

## SPX 寻址回归(spx2.asm)

覆盖:`mov spx,#imm`(7E F8)、`mov @spx,a` / `mov a,@spx`(7A/7E FB)、
16 位 `mov wr4,@spx-0x0001`(69 2F)、`inc spx,#2`(0B FD)、`push acc`(C0 E0)、
`mul ab`(A4)、R11=ACC 别名。

期望:Stop at spin;xram[0x100]=0x42;WR4=0x1042;iram[0x08]=0x42(push);
ACC=0x00(mul 0x42*0)。

## 严格 ISA 探测(269 形式,自动生成)

`support/stc32/tools/ucsim_isa_probe.py` 从 `support/stc32/isa/mcs251.yaml` 逐条生成
`<form> + sjmp self` 最小镜像,对 uCsim(-t251)检查四件事:

1. **反汇编**:`dc 0` 的助记符与 YAML 一致;
2. **长度**:反汇编消耗字节数 == YAML `length`;
3. **执行**:严格单步后 PC 前进 == length(控制流类除外);
4. **受控失败**:未实现形式必须以 `Invalid instruction`/unknown-code
   停止(退出码 106),崩溃或错解为其他指令记为 FAIL。

```bash
PYTHONPATH=tools/pylib python3 support/stc32/tools/ucsim_isa_probe.py --strict
# PASS 268  GAP 0  SKIP 1  FAIL 0  (total 269)
```

- **GAP 是唯一权威缺口清单**(报告:`isa_decode_report.md`),
  RELEASE_NOTES/发布材料的 ISA 状态从它生成,不要手工维护。
- 退出码:默认模式仅 FAIL(崩溃/错长度/错 PC)非零,GAP 不算失败;
  `--strict` 模式下 GAP 也返回非零(结构性 SKIP 仍只报告)。
- 前置:先构建 `build/sim/ucsim/src/sims/s51.src/ucsim_51`。

## ISA 语义断言(isa_semantics3.asm)

覆盖 2026-08-15 补齐的指令族的**执行语义**(不只是解码):
A9 扩展位操作(setb/clr/cpl/anl/orl CY 含 /bit、mov CY/bit、JB/JNB/JBC
三路径)、XCH/XCHD A,@Ri、DA A 边界(0x9A→0x00 CY=1 / 0x45 不变)、
SRA 符号保持与移出 CY、CMP、MOVH 高半装载、dir16 store/load 往返、
ADD Rm,@WRj(3 字节修正形式)、LJMP @WRj、ACALL/RET，以及 native
PUSH/POP 的立即数、WR、DR 字节序往返。

```bash
build/bin/sdas251 -plosg isa_semantics3.asm
build/bin/sdld -nf isa_semantics3.lk
build/sim/ucsim/src/sims/s51.src/ucsim_51 -t251 -c - -S in=/dev/null,out=- \
    isa_semantics3.ihx <<< "set opt selfjump_stop 0 / step 500 vclk / dump iram 0x50 0x6b / quit"
```

期望(全部断言通过):`0x50..0x6a = 90 00 03 ab 55 aa 00 01 a5 f8
01 01 01 01 10 01 be ef 0f ab cd 12 34 12 34 56 78`,R2=0x77(ACALL/RET 往返),R4:R5=0xbeef
(dir16 读回)。**注意**:251 程序必须先初始化 SPX(测试用
`mov spx,#0x0200`),否则 ACALL 压栈会砸中 IRAM 低地址的 R1/R2 映射。

## ISA 语义断言 v4(isa_semantics4.asm)

依据 Intel 8XC251SB 用户手册原文核对的语义(例题逐字验证):

- **MUL/DIV Rmd,Rms**(UM A-8/A-101/A-55):结果对存放在包含 Rmd 的字寄存器,
  md 偶 → Rmd=积高/余数、Rmd+1=积低/商;md 奇 → Rmd-1=积高/余数、Rmd=积低/商。
  手册例题:R1=0x50×R0=0xA0 → R0:R1=0x32:0x00;R1=251÷R5=18 → R1=13、R0=17。
- **宽位 ALU 标志**(UM A-13):OV=最高两位进位异或;AC 仅 8 位操作影响。
- **JZ/JNZ 测累加器 A**(STC32G 手册 1632 页)；不要把它与基于 PSW1 的有符号条件跳转混淆。
- **TRAP 按 NOP 执行**(STC32G 手册 1672 页)；本目标芯片不采用 Intel 8XC251 的 0FF007BH 中断向量模型。

期望 dump:`0x60..0x72 = 00 32 00 01 00 e1 0d 11 04 1c 00 01 01 00 01 01 01 01 01`。


## ISA 语义断言 v5(isa_semantics5.asm)

全部八个 A5 索引 MOV 形式的有效地址计算:

- **DRk 基址族**(0x29/0x69 载入、0x39/0x79 存储,EA 为 24 位):含跨
  64 KiB 进位用例(基址 0x00FFFE + 2 → 0x010000),可区分"真 24 位加法"
  与"只保留基址高 8 位"两种实现;
- **WRj 基址族**(0x09/0x49 载入、0x19/0x59 存储,EA 为 16 位 edata 窗口):
  0x59 曾被错误按 DR(idx*4) 基址执行并反汇编为 @DRk,此族防止回归。

读回一律走**无位移** `@DRk` 形式(独立路径,不会掩盖被测形式的错误;
WR 族的读回 DR 指向同一扁平地址,错基址执行无法往返)。iram[0x00]/[0x02]
金丝雀捕捉错基址存储。期望 dump:`0x30..0x3c =
5a ab cd 3c be ef 77 31 55 66 77 88 99`,iram[0x00]=0x00、iram[0x02]=0xaa。

同一 runner 还包含 **idx16-disass 门禁**:对八个形式的完整反汇编操作数
文本(基址寄存器 + 位移,不只助记符)逐一断言,防止 0x59 一类
"助记符正确、操作数错寄存器"的静默回归。

```bash
PYTHONPATH=tools/pylib python3 tools/run_isa_semantics.py
```

## ISA 形式 → 独立语义断言覆盖表

strict probe 的 PASS 只证明**解码、长度、助记符和受控 PC 前进**;
"268 PASS"不等于 268 个形式都有执行语义断言。下表按 65 个指令族
明确两级证据的边界("族出现"不等于该族全部形式都有状态断言):

### 有独立状态语义断言的族(64 个可执行指令族均已有代表性状态语义用例覆盖，ESC 族为结构性 N/A)

| 证据来源 | 覆盖形式与范围说明 |
|---|---|
| isa_semantics3.asm | setb/clr/cpl、anl/orl(bit 与寄存器)、jb/jnb、xch/xchd、da、sra/srl/sll、cmp、movh、dir16 mov、push/pop(立即数/WR/DR 字节序)、ljmp@wr、acall、sjmp、cjne、部分 mov (族内代表性形式) |
| isa_semantics4.asm | mul/div(Rmd,Rms 手册例题)、add/addc/sub/subb/xrl(标志与结果)、inc、宽移位、je/jz/jnz/jg/jle/jsl/jsle 条件跳转、rl、部分 mov/anl/orl/cmp (族内代表性形式) |
| isa_semantics5.asm + idx16-disass | 八个索引 MOV 形式(0x09/0x49/0x29/0x69/0x19/0x59/0x39/0x79)
    的状态语义与完整反汇编操作数、inc/dec dr、mov/movh 构建 DR |
| jmpmod(run_isa_semantics.py) | jmp @a+dptr(16 位模加,PC.23:16 取 DPXL) |
| isa_semantics6.asm(ST-1S-A) | rlc/rr/rrc/swap(结果、全字节 PSW1 断言:
    受影响标志更新 + 未受影响的 CY/AC/OV 保持)、movs/movz(符号/零扩展,
    全部标志保持;依据 STC32G p.553 PSW1 布局与 p.1650/1651 标志表) [6 形式] |
| isa_semantics7.asm(ST-1S-B) | movc(@a+dptr / @a+pc 常数表读取与 pc+a 即时断言)、
    movx(@dptr / @r0 / @r1 读写往返、高低 XDATA 寻址、防别名金丝雀)、XDATA 与
    IRAM 空间隔离金丝雀、全 6 形式单步标志位保持断言(依据 Intel 8XC251SB User's Manual,
    doc 272617-001, February 1995 pp.A-100..A-101/A-103..A-105 与 STC32G 印刷页 814..815;
    uCsim 的 xram[Ri] 属于 E2 模拟器简化模型,ABI.md 中 __pdata 活动 MXAX:P2 页寻址保持不变,属于 E4 板级边界) [6 形式] |
| isa_semantics8.asm(ST-1S-C) | lcall(@wrj / addr16 在 Region 0 与 Region 1 内部双重覆盖、Callee 内部独立断言 2 字节返回帧小端字节序与 ret 弹栈恢复 SPX 及 64KB 区域保持)、
    ecall(@drk / addr24 跨区域调用至 Region 1 并在 Callee 内部独立断言 3 字节返回帧小端字节序与 eret 弹栈恢复 SPX)、
    独立预置合成帧断言 ret(Region 1 保持) 与 eret(跨区域返回)、ajmp 在 Region 1 内部验证非零 2KB 页号(基址 0x010900, PC.15:11!=0)与高位保持、
    ejmp(@drk / addr24 跨区域绝对与间接跳转)、全跳转/返回指令紧随防 fallthrough 独立停机陷阱 (0x30=0xEx)、reti(4 字节帧弹栈跳至 Region 0, 恢复 PC、SPX 与 frame PSW1)、
    全调用 Callee 入口 PSW1 即时捕获、全跳转目标处 SPX 即时捕获与全程 SPX(0x0200) 高低字节完整断言、XRAM 物理栈边界金丝雀(0x0200=0x55)断言(依据 Intel 8XC251SB User's Manual,
    doc 272617-001, February 1995 pp.A-31/A-50..A-53/A-77..A-78/A-128..A-129 与 STC32G 印刷页 814..815 / p.158 / p.1701) [10 形式] |
| isa_semantics9.asm(ST-1S-D) | jc/jnc(CY 条件跳转与 fallthrough)、jne(Z 条件跳转与 fallthrough)、
    jsg((N^OV)\|Z==0 有符号大于)、jsge(N^OV==0 有符号大于等于)、jbc(51 格式与 251 扩展格式位测试并清零、fallthrough 保持 0)、
    djnz(Rn 与 dir8 循环减 1 跳转、减至 0 fallthrough 退出、全过程标志位不变量保持;修复 ucsim 中 djnz 错误修改 N/Z 缺陷)、
    全分支防 fallthrough 独立停机陷阱(0x10=0xEx)、全过程 SPX(0x0200) 与 XRAM 金丝雀(0x0200=0x55)断言(依据 Intel 8XC251SB User's Manual,
    doc 272617-001, February 1995 pp.A-48..A-49/A-67..A-74 与 STC32G 印刷页 814..815) [9 形式] |
| isa_semantics10.asm(ST-1S-E) | nop(空操作, 控制流顺序执行 fallthrough, PSW1 与 R1..R4 通用寄存器完好保持; 1 字节长度与 PC+1 由 as251/strict probe 保证)、
    trap(操作码 0xB9, 依据 STC32G 手册 p.1672 NOP 语义顺序执行 fallthrough, PSW1 与 R1..R4 寄存器完好保持; 1 字节长度由 as251/strict probe 保证)、
    全过程 SPX(0x0200) 与 XRAM 金丝雀(0x0200=0x55)断言(依据 Intel 8XC251SB User's Manual,
    doc 272617-001, February 1995 pp.A-107/A-132 与 STC32G 印刷页 815 / p.1672) [2 形式] |

### 仅解码/结构性 SKIP 的族与形式(1 族 / 1 形式)

esc(结构性前缀字节 SKIP / N/A, 0xA5，作为 Source/Binary 模式指令前缀)

(ST-1S 按子任务 A-E 逐批闭环：历史 decode-only 清单中的全部 33 个可执行形式已全部完成逐形式独立状态语义预言机覆盖，ESC 归档为结构性 SKIP/N/A；历史 decode-only 形式清单清零)

补充:其中 ejmp/mov dr28,dpx/ecall/eret 在**编译器回归 lane**
(E2,如 check-small-switch-run.py 与 4 字节中断帧测试)以代码生成路径
间接执行过,但这不是逐形式独立 ISA 预言机,不得据此提升该形式的语义
证据等级。MOVC/JMP 高区(FE:/FF:)行为保持 provisional,待真实板裁决。
