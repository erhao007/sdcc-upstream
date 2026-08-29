# 基于 SDCC 实现 macOS 原生编译 STC32G 的开源方案

> 项目建议名称：**sdcc-stc32 / OpenSTC32 Toolchain**
> 文档版本：v0.1
> 日期：2026-08-07
> 首要目标芯片：**STC32G12K128**
> 首要宿主平台：**macOS Apple Silicon / Intel**
> 最终目标：macOS、Linux、Windows 均可原生编译 STC32G 固件，不依赖 Keil C251。

---

## 1. 项目目标

本项目不是“从零重新实现一个 C251”，而是基于 SDCC 已有的 C 前端、中间表示、优化器、库体系、汇编/链接框架和 uCsim 模拟器，增加对 **STC32G / MCS-251 Source Mode** 的原生支持。

最终希望达到如下用户体验：

```bash
brew install sdcc-stc32

sdcc -mstc32 -c src/main.c -o build/main.rel
sdcc -mstc32 build/*.rel -o build/firmware.ihx
```

或者在后续加入包装器：

```bash
stc32-build
stc32-flash build/firmware.ihx
```

推荐第一阶段只解决“原生编译 + 链接 + HEX 输出”，烧录和在线调试作为独立子项目处理。

---

## 2. 当前事实基础

截至 2026-08：

1. SDCC 当前稳定版为 4.6.0，并且 Homebrew 已为 Apple Silicon 和 Intel macOS 提供原生包。
2. SDCC 官方功能请求 #1009 已明确讨论 MCS-251 / STC32G 支持。
3. SDCC 开发者 Philipp Klaus Krause 明确建议开发顺序：
   - 先实现 **MCS-251 assembler**；
   - 再实现 **uCsim MCS-251 simulator**；
   - 最后实现编译器 backend，并依靠模拟器做 regression testing。
4. SDCC Wiki 的 “8051 Variants” 页面已经把 `mcs251` 和 `stc32` 列为 Suggested backends。
5. SDCC 官方 “Adding a port” 文档明确给出了新架构端口所需模块：
   - assembler / linker；
   - simulator；
   - `src/<port>/main.c`；
   - register / stack allocator；
   - code generator；
   - `device/lib/<port>` runtime library；
   - regression tests。
6. STC 官方手册说明 STC32G 当前只支持 **251 Source Mode**，并建议 Keil 中使用 **4 Byte Interrupt Frame Size** 和 XSmall memory model。

这意味着该项目不是“完全未知方向的自研编译器”，而是一个已有上游设计路线、缺少实际实现者的架构移植项目。

---

## 3. 总体战略

### 3.1 不追求第一版兼容 Keil C251 ABI

第一版目标是：

> **同一份 C 源码可以使用 SDCC-STC32 重新编译并正确运行。**

不要求：

- 链接 Keil `.OBJ`；
- 链接 Keil `.LIB`；
- 与 Keil C251 二进制函数调用 ABI 完全一致；
- 复用 Keil runtime；
- 生成与 Keil 相同的机器码。

这样可以避免大量兼容负担，并降低知识产权风险。

### 3.2 第一版只支持 STC32G Source Mode

不做完整通用 MCS-251：

```text
支持：STC32G Source Mode
暂不支持：MCS-251 Binary Mode
暂不支持：其它历史 251 芯片的特殊模式
```

这是一个重要的范围控制措施。

### 3.3 正确性优先于代码效率

第一版目标：

```text
正确 > 稳定 > 可测试 > 性能 > 代码尺寸
```

先让：

```c
uint8_t
uint16_t
uint32_t
pointer
array
struct
function
interrupt
```

全部正确，再逐步利用 WR/DR 寄存器和 16/32 位原生指令优化性能。

---

## 4. 推荐项目结构

建议前期建立独立 Git 仓库，定期 rebase SDCC upstream；不要把大量试验代码直接混入正式 SDCC trunk。

```text
sdcc-stc32/
├── upstream/                # SDCC fork
│   ├── src/stc32/           # compiler backend
│   ├── device/lib/stc32/    # runtime library
│   ├── device/include/stc32/
│   ├── sim/ucsim/           # MCS251/STC32 simulator
│   └── sdas/                # MCS251 assembler
│
├── isa/
│   ├── mcs251.yaml          # 指令数据库
│   ├── registers.yaml
│   └── addressing_modes.yaml
│
├── tools/
│   ├── opcode_gen.py
│   ├── disasm.py
│   └── test_vector_gen.py
│
├── tests/
│   ├── asm/
│   ├── simulator/
│   ├── c-regression/
│   └── hardware/
│
├── devices/
│   └── stc32g12k128/
│       ├── stc32g12k128.h
│       ├── crt0.asm
│       ├── memory.inc
│       └── examples/
│
├── packaging/
│   └── homebrew/
│
└── docs/
    ├── ABI.md
    ├── ISA.md
    ├── MEMORY_MODEL.md
    ├── TESTING.md
    └── PORTING_NOTES.md
```

如果目标是最终合并 upstream，则正式代码最终仍应回到 SDCC 的标准目录结构中。

---

# 5. 开发阶段划分

## Phase 0：建立可重复的 macOS SDCC 开发环境

### 目标

在 Mac 上可以从源码构建未修改的 SDCC，并运行其 regression tests。

### 建议环境

```bash
brew install sdcc
brew install boost texinfo gputils readline zstd
```

Homebrew 当前稳定版为 SDCC 4.6.0，并支持 Apple Silicon / Intel Mac。

开发时建议另外使用 SDCC upstream trunk，因为新 backend 最终需要跟随上游代码，而不是永远停留在 4.6.0。

### 验收

```text
[ ] macOS arm64 能构建 SDCC
[ ] macOS x86_64 至少 CI 能构建
[ ] 原有 mcs51 regression tests 不被修改破坏
[ ] 建立 GitHub Actions / CI
```

---

# 6. Phase 1：建立 MCS-251 指令数据库

这是整个项目最重要的基础资产之一。

不要一开始把 opcode 分散硬编码在 assembler、disassembler、simulator 三处。

建议建立机器可读 ISA 数据：

```yaml
mnemonic: ADD
mode: source
operands:
  - WR
  - WR
encoding:
  bytes: ...
flags:
  - C
  - AC
  - OV
  - Z
cycles: ...
```

至少描述：

- mnemonic；
- operand 类型；
- register class；
- immediate 位宽；
- addressing mode；
- Source Mode opcode；
- instruction length；
- PSW flags；
- branch displacement；
- cycle 信息（可后补）；
- STC32G 是否支持。

### 为什么先做数据库

同一份数据可以生成：

```text
Assembler encoding table
Disassembler decoding table
Simulator dispatch table
Opcode regression tests
ISA documentation
```

从根本上减少三套实现之间不一致的问题。

### 验收

```text
[ ] 覆盖 STC32G 实际支持的全部 Source Mode 指令
[ ] 每个 opcode 唯一或明确标记别名
[ ] 自动检查 operand / opcode 冲突
[ ] 至少有 Intel/STC 手册样例对应测试
```

---

# 7. Phase 2：实现 sdas251 / MCS-251 Source Mode 汇编器

SDCC 上游已经明确建议，这是第一项真正必须完成的功能。

### 目标

```text
.asm
 ↓
sdas251
 ↓
.rel / ihx
```

支持：

- 8/16/32 位寄存器；
- R / WR / DR register aliases；
- immediate；
- direct / indirect addressing；
- branch / call / return；
- bit operations；
- Source Mode opcode encoding；
- relocation；
- symbol；
- section；
- 与 SDCC linker 接口兼容。

### 最初不要做的功能

- Binary Mode；
- Keil A251 完整语法兼容；
- Keil object format；
- 宏汇编全部高级特性。

### 必须同时做 disassembler

建议提供：

```bash
mcs251-disasm firmware.bin
```

用于：

```text
ASM → assembler → binary → disassembler
```

做 round-trip 测试。

### 验收

```text
[ ] 95%+ 指令拥有自动编码测试
[ ] branch offset 正/负边界全部测试
[ ] immediate 8/16/32 位边界测试
[ ] Source Mode A5 前缀相关指令测试
[ ] 能手写 GPIO/UART Hello World 并烧到 STC32G12K128 运行
```

---

# 8. Phase 3：实现 uCsim MCS-251 / STC32 CPU Core

这是编译器正确性的基础设施，不应跳过。

### 第一版模拟范围

只做 CPU，不做完整 MCU：

```text
R0-R15
WR aliases
DR aliases
ACC / B
PSW / PSW1
DPTR
SP
PC
CODE
DATA / EDATA / XDATA 基础映射
CALL / RET / RETI
interrupt frame
Source Mode instruction decoder
```

暂时不模拟：

```text
CAN
ADC
PWM
USB
复杂定时器
```

### 每条指令测试

每个 opcode 至少验证：

```text
输入寄存器
输入内存
输入 flags
↓
执行 1 instruction
↓
输出寄存器
输出内存
输出 flags
PC/SP
```

### 验收

```text
[ ] assembler 的全部基础指令可被 simulator 执行
[ ] CALL / RET / stack 通过嵌套测试
[ ] interrupt entry/return 通过测试
[ ] arithmetic flags 通过边界测试
[ ] 10万~100万组随机 ALU 测试可连续通过
```

---

# 9. Phase 4：确定 STC32 SDCC ABI

在正式写 C backend 前，ABI 必须形成文档，不允许边写边猜。

建议建立 `docs/ABI.md`。

必须明确：

### 基本类型

```text
char      8 bit
short    16 bit
int      建议 16 bit（尽量遵循 SDCC 小 MCU 传统）
long     32 bit
pointer  按 memory space 决定
```

具体 `int` 宽度需要与 SDCC 端口机制及实际实现评估后最终冻结；一旦发布稳定 ABI 不应随意更改。

### Endianness

MCS-251 原生 16/32 位寄存器和多字节数据行为需要按官方架构定义处理。

正式 native backend 建议采用与 MCS-251 原生宽指令一致的对象表示，以便后续直接使用 WR/DR 指令。

必须测试：

```c
uint16_t
uint32_t
struct
union
bitfield
pointer cast
memcpy
```

### 函数调用

定义：

```text
参数传递顺序
返回值位置
caller-saved registers
callee-saved registers
stack alignment
struct return
varargs
function pointer
```

### 中断 ABI

STC32G 官方指出中断采用 4-byte push/pop frame，必须独立实现 ISR prologue / epilogue 并进行硬件验证。

### ABI 冻结原则

在 v0.1/v0.2 可以变化；到 v1.0 后禁止破坏 ABI，除非提升 major version。

---

# 10. Phase 5：实现最小 C backend

建议 target 名先使用：

```text
stc32
```

而 assembler / simulator 内部仍把架构称为：

```text
mcs251
```

这样可以明确：第一代 compiler backend 是针对 STC32G 的 Source Mode，不承担所有历史 MCS-251 兼容责任。

### SDCC 官方新 port 目录

核心会涉及：

```text
src/stc32/main.c
src/stc32/ralloc.h
src/stc32/ralloc.c
src/stc32/ralloc2.cc
src/stc32/gen.h
src/stc32/gen.c

device/lib/stc32/
```

### 第一版寄存器策略

不要一次使用全部寄存器。

建议第一阶段只开放：

```text
R0-R7
WR0 / WR2 / WR4 / WR6
DR0 / DR4
```

先解决 overlapping registers：

```text
DR0 = R0,R1,R2,R3
WR0 = R0,R1
WR2 = R2,R3
```

寄存器分配器必须知道这些 alias 冲突。

第一版可以少用寄存器、增加 spill，只要结果正确。

### Codegen 最初覆盖顺序

建议按以下顺序：

```text
1. uint8_t move/load/store
2. 8-bit add/sub/and/or/xor
3. compare + branch
4. function call/return
5. stack locals
6. uint16_t
7. pointer
8. uint32_t
9. struct/array
10. switch
11. function pointer
12. interrupt
13. mul/div
14. bit operations
15. optimization
```

### 第一版不要重点追求

```text
全部 R8-R15
全部 WR/DR
极致 peephole
极致 code size
Keil 性能对标
```

---

# 11. 可选 Bootstrap PoC：先让受限 C 尽早跑起来

为了缩短“第一段 C 程序上硬件”的时间，可以研究一个临时路径：

```text
SDCC mcs51 front/backend
        ↓
生成传统 8051 风格 assembly
        ↓
sdas251 以 Source Mode 重新编码
        ↓
STC32G
```

MCS-251 的 Source Mode 仍保留 MCS-51 来源的指令，只是部分 opcode 会通过前缀重新编码，因此从“汇编源兼容”的角度存在可行性。

但这一方案只适合作为 PoC：

- 不应作为最终 ABI；
- 先禁止中断；
- 先禁止依赖多字节硬件指令；
- 必须验证 CALL/RET/stack；
- 必须验证多字节对象表示；
- 必须避免把 mcs51 小端假设错误带入 native backend。

如果 PoC 很快成功，它可以让项目在正式 compiler backend 完成前就拥有“基础 C → STC32G”的演示成果。

---

# 12. Phase 6：STC32G12K128 Device Support Package

编译器 backend 与具体 MCU 外设不要混在一起。

建立：

```text
device/include/stc32/stc32g12k128.h
device/lib/stc32/crt0.asm
device/lib/stc32/startup.c
```

内容包括：

- SFR 地址；
- bit registers；
- interrupt vector；
- reset/startup；
- DATA/EDATA/XDATA 初始化；
- stack 初始化；
- C runtime 初始化；
- main() 跳转；
- 可选 printf / UART runtime。

### 重要法律/工程原则

不要直接复制 Keil C251 的库、头文件或 object。

SFR 定义应从：

```text
STC 官方公开寄存器手册
```

自行生成。

可以写一个 `svd/register-json → header` 生成器，降低手工错误。

---

# 13. Phase 7：C Runtime / libc

最先实现：

```text
memcpy
memset
memcmp
strlen
strcmp
strcpy
uint16/uint32 helper
mul/div helper（如果 codegen 暂未直接支持）
```

原则：

- 尽量先用 C 实现；
- 只有热点函数再用 MCS-251 汇编；
- runtime 必须有独立 regression tests；
- 不依赖 Keil 库。

后续再针对 WR/DR 优化：

```text
memcpy
memset
32-bit arithmetic
multiply/divide
```

---

# 14. 测试体系：这是项目成败的核心

编译器项目最怕：

```text
能编译
≠
生成的代码永远正确
```

必须建立四层测试。

## A. Opcode Test

```text
assembly → expected bytes
```

用于 assembler。

## B. CPU Instruction Test

```text
initial CPU state
→ execute instruction
→ expected CPU state
```

用于 simulator。

## C. C Differential Test

生成大量不依赖硬件的纯 C：

```c
uint32_t test(uint32_t a, uint16_t b)
{
    return (a + b * 17u) ^ (a >> 3);
}
```

参考端：

```text
Clang/GCC host build
```

测试端：

```text
SDCC-STC32 → uCsim
```

输入相同随机数据，比较返回结果。

测试内容包括：

```text
integer promotion
signed/unsigned
overflow semantics
shift
pointer arithmetic
array
struct
union
function calls
nested calls
recursion（可后期）
```

## D. Hardware Regression

在 STC32G12K128 开发板执行：

```text
GPIO
UART
Timer
Interrupt
EDATA/XDATA
IAP
CAN1
CAN2
```

测试固件通过 UART 输出自动化 PASS/FAIL。

---

# 15. Keil C251 在项目中的正确角色

Windows 上的 Keil C251 非常有价值，但只作为：

```text
硬件验证基线
性能参考
黑盒结果交叉验证
```

不要：

```text
反编译 C251
复制 proprietary runtime
复制 Keil object format 实现
复制其内部优化代码
```

可以针对自己写的测试函数比较：

```text
Keil 程序运行结果
vs
SDCC-STC32 程序运行结果
```

也可以比较：

```text
code size
cycle count
```

但自己的实现必须以公开 ISA / STC 手册 / SDCC 源码为依据。

---

# 16. STC32G 项目重点技术难点

## 16.1 Overlapping registers

```text
R0-R15
WRx
DRx
```

存在别名关系，是 register allocator 的核心风险。

## 16.2 Endianness

现有 SDCC mcs51 backend 的多字节实现假设与 MCS-251 原生宽数据行为不同，不能简单复制代码生成逻辑。

## 16.3 Memory spaces

必须正确处理：

```text
DATA
EDATA
XDATA
CODE
SFR
BIT
```

以及不同 pointer 类型。

## 16.4 Stack / CALL / interrupt frame

尤其 STC32G 的 4-byte interrupt frame 必须用硬件测试确认。

## 16.5 SDCC 新 register allocator

SDCC 上游自己也指出 mcs51 backend 需要切换到新的 register allocator。STC32 backend 建议直接参考新式 port（特别是 Rabbit r4k 等），不要投入大量代码去扩展即将重构的老式 mcs51 ralloc。

## 16.6 编译器正确性

一个很小的 signed compare / carry / endian bug，都可能变成极难定位的固件随机故障，因此 simulator + regression testing 不能省略。

---

# 17. macOS 原生开发体验设计

最终用户不应该知道内部编译器复杂度。

### 安装

第一阶段提供 Homebrew Tap：

```bash
brew tap <org>/stc32
brew install sdcc-stc32
```

成熟后再尝试 upstream / Homebrew main formula。

### 项目模板

```text
stc32-project/
├── src/
│   └── main.c
├── include/
├── Makefile
├── stc32.json
└── .vscode/
```

### Makefile

目标：

```bash
make
make clean
make flash
make monitor
```

其中 `flash` 可以调用后续确认支持 STC32G 的烧录工具；在烧录支持成熟前允许用户手工使用 Windows STC-ISP，不阻塞编译器主项目。

### VS Code

提供：

```text
syntax / completion
build task
serial monitor task
problem matcher
```

如果 clangd 无法识别 SDCC 扩展关键字，可在编辑器侧定义仅供索引使用的 compatibility macros。

---

# 18. CI 设计

建议 GitHub Actions 至少包含：

```text
macOS arm64
macOS x86_64（可通过现有 runner/构建策略）
Linux x86_64
```

每次提交执行：

```text
build compiler
assembler tests
simulator tests
C regression tests
existing SDCC regression tests
format/static checks
```

夜间任务：

```text
random differential tests
100k~1M generated cases
```

另外建议保留一台实物 STC32G board 作为 hardware CI，但不要把它作为最初阶段的硬性条件。

---

# 19. 版本路线

## v0.1 — Assembler Preview

```text
sdas251
mcs251-disasm
manual assembly Hello World
```

## v0.2 — Simulator Preview

```text
uCsim MCS251 core
basic instruction regression
```

## v0.3 — C Experimental

```text
uint8_t
basic arithmetic
branch
function
```

## v0.4 — Real Firmware

```text
uint16/uint32
pointer
arrays
struct
stack
UART
GPIO
```

## v0.5 — Interrupt + Memory Spaces

```text
EDATA/XDATA
interrupt
function pointer
runtime library
```

## v0.6 — STC32G12K128 usable

```text
CAN
Timer
IAP
actual application builds
```

## v0.8 — Optimization

```text
full WR/DR use
R8-R15
peephole optimization
runtime assembly optimization
```

## v1.0 — Stable ABI

```text
ABI frozen
macOS packaging
Linux packaging
hardware regression
public documentation
```

---

# 20. 工作量预估

一个人、第一次真正参与编译器 backend、同时使用 AI 编码辅助的大致工程量：

| 阶段 | 粗略时间 |
|---|---:|
| 环境/源码理解 | 1~3 周 |
| ISA 数据库 | 1~3 周 |
| assembler + disassembler | 1~2 月 |
| uCsim core | 1~3 月 |
| 最小 C backend | 2~5 月 |
| 可开发普通 STC32 固件 | 6~12 月累计 |
| 性能/代码尺寸逐步成熟 | 1~2 年持续迭代 |

这不是承诺工期，而是说明项目规模。

如果多人协作并得到 SDCC upstream 开发者指导，时间会明显缩短。

---

# 21. 风险与应对

| 风险 | 级别 | 对策 |
|---|---|---|
| ISA opcode 理解错误 | 高 | ISA 数据库 + 官方手册 + 自动测试 |
| register alias 分配错误 | 高 | 最初只开放少量寄存器；新 ralloc；随机测试 |
| endian bug | 高 | ABI 文档 + differential tests |
| stack/interrupt 错误 | 高 | simulator + 实机 ISR tests |
| 上游 SDCC 架构变化 | 中高 | 基于 trunk；小步提交；尽早与 upstream 沟通 |
| assembler 与 simulator 不一致 | 中 | 共享 ISA 数据源 |
| 只在开发者机器能构建 | 中 | macOS/Linux CI + Homebrew |
| 性能比 C251 差 | 中 | v1 前不作为阻塞；后期 WR/DR 优化 |
| 烧录工具不完整 | 中低 | 编译与烧录解耦；先用 STC-ISP 验证 |
| 在线调试不支持 | 低（对编译器主目标） | 先 UART debug；后续另建 GDB/Link1 项目 |

---

# 22. 第一阶段具体任务清单（建议立即执行）

## Week 1：环境与基线

```text
[ ] Fork SDCC upstream
[ ] macOS 从源码 build
[ ] 运行现有 mcs51 tests
[ ] 创建 stc32 branch
[ ] 阅读 Adding a port
[ ] 阅读 r4k backend 的 ralloc/codegen 结构
[ ] 建 docs/ARCHITECTURE.md
```

## Week 2：ISA 数据

```text
[ ] 整理 R/WR/DR register alias
[ ] 整理 addressing modes
[ ] 整理 Source Mode opcode map
[ ] 建 mcs251.yaml
[ ] 写 opcode 数据校验器
```

## Week 3~4：assembler skeleton

```text
[ ] sdas251 skeleton
[ ] MOV / ADD / SUB / logic
[ ] branch / call / ret
[ ] register parsing
[ ] immediate parsing
[ ] assembler unit tests
```

## Week 5~6：可运行汇编固件

```text
[ ] linker / IHX output
[ ] STC32G12K128 crt0 最小版本
[ ] 手写 UART hello
[ ] 实机运行
[ ] disassembler
```

到这里就是项目的第一个真正里程碑：

> **完全不需要 Keil，Mac 可以把 MCS-251 汇编源码变成可运行 STC32G 固件。**

之后再进入 simulator 和 C backend。

---

# 23. 给 Codex / 编码智能体的约束

建议仓库根目录放 `AGENTS.md`：

```text
Project goal:
Add native STC32G / MCS-251 Source Mode support to SDCC.

Rules:
1. Base implementation on public Intel MCS-251 documentation, public STC32G documentation and SDCC source only.
2. Never copy or reverse engineer Keil C251 proprietary implementation.
3. Source Mode only in the first implementation.
4. Correctness before optimization.
5. Every new opcode requires an automated test.
6. Assembler, disassembler and simulator should share one canonical ISA definition where practical.
7. Every compiler codegen feature requires a simulator regression test.
8. Do not break existing SDCC ports/tests.
9. Keep changes upstream-friendly and small.
10. Document ABI decisions before relying on them.
11. Avoid large refactors unrelated to STC32.
12. Build and run tests after every non-trivial change.
```

---

# 24. 与当前双 CAN 项目的关系

建议两条路线并行：

```text
路线 A：Windows + Keil C251
        ↓
验证 STC32G12K128
双 CAN 性能 / RAM / CAN 驱动

路线 B：Mac + SDCC-STC32
        ↓
assembler
simulator
compiler backend
```

这两条线互不阻塞。

Windows/Keil 项目还可以提供非常重要的硬件“金标准测试程序”：

```text
UART test
CAN1 test
CAN2 test
Timer test
Interrupt test
Memory test
```

等 SDCC-STC32 后端能生成 C 程序后，再用同一套测试在 Mac 编译版本上运行，做结果对照。

---

# 25. 最终判断

该项目值得做，但正确的定位不是：

> “重写 Keil C251。”

而是：

> **“为 SDCC 增加一个 upstream-friendly 的 STC32/MCS-251 Source Mode port，并建立 assembler + simulator + regression test + device support 的完整开源链路。”**

最重要的三个第一目标是：

```text
① sdas251：Source Mode assembler
② uCsim-mcs251：可自动回归测试的 CPU simulator
③ sdcc -mstc32：最小、正确的 C backend
```

当这三项完成以后，macOS 原生开发 STC32G 就已经从“工具拼凑”变成真正独立的开源工具链。

---

# 参考资料

- SDCC Feature Request #1009 — Is it possible to add support for MCS-251?
  https://sourceforge.net/p/sdcc/feature-requests/1009/
- SDCC Wiki — 8051 Variants
  https://sourceforge.net/p/sdcc/wiki/8051%20Variants/
- SDCC Wiki — Adding a port
  https://sourceforge.net/p/sdcc/wiki/Adding%20a%20port/
- SDCC Wiki — Assemblers and Linkers
  https://sourceforge.net/p/sdcc/wiki/Assemblers%20and%20Linkers/
- SDCC Wiki — Rabbits
  https://sourceforge.net/p/sdcc/wiki/Rabbits/
- Homebrew Formula — SDCC
  https://formulae.brew.sh/formula/sdcc
- STC32G Series Technical Manual
  https://www.stcmicro.com/datasheet/stc32g-cn.pdf
- Intel 8XC251SB Embedded Microcontroller User’s Manual (archival mirror)
  https://www.bitsavers.org/components/intel/MCS251/272617-001_Intel_8XC251SB_Embedded_Microcontroller_Users_Manual_1997.pdf
