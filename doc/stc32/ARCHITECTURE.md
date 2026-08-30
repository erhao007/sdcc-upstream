# SDCC-STC32 架构笔记

> 日期:2026-08-07 · Week 1 产物 · 依据:SDCC Wiki "Adding a port"、SDCC 4.6.0 源码(r4k/z80 端口)、项目计划文档。

## 1. 项目定位

为 SDCC 增加 **STC32G / MCS-251 Source Mode** 的 upstream-friendly port（内部端口名
`mcs251`，用户目标别名 `stc32`）。第一版只做 Source Mode、不追求 Keil ABI
兼容、正确性优先。工具链闭环:
assembler(sdas251) → simulator(ucsim-mcs251) → C backend(sdcc -mstc32)。

## 2. SDCC 编译流水线(相关部分)

```text
C 源码 → 前端(SDCpp/parse) → iCode(中间表示) → 寄存器分配(ralloc2)
       → 代码生成(gen.c, 生成汇编) → peephole 优化(peeph.def)
       → 汇编器(sdas, asxxxx fork) → 链接器(sdld) → .ihx/.hex
正确性验证:uCsim 模拟器 + regression tests
```

## 3. 端口注册机制(SDCC 如何知道一个新端口)

一个"端口" = 一个 `PORT` 结构体实例,分散在 4 处注册:

| 文件 | 作用 |
|---|---|
| `configure.ac` | `AC_DO_PORT(<name>, <srcdir>, <NAME>, <desc>)` 声明端口;`<srcdir>` 指定共用哪个 backend 目录 |
| `src/port.h` | `TARGET_ID_*` 枚举 + `extern PORT <name>_port;` |
| `src/SDCCmain.c` | 端口表(`&<name>_port` 项) |
| `src/<srcdir>/main.c` | `PORT <name>_port = { ... }` 定义(target id、命令行、model、汇编/链接命令、peephole 回调) |
| `device/lib/Makefile.in` | `TARGETS += model-<port>` 注册 runtime 库构建 |
| `support/regression/ports/<port>/` | regression 规格(spec.mk) |

### 变体 vs 全新(重要决策)

- **变体模式**:一个 backend 目录承载多个 PORT。例:Rabbit 系列(r2k/r2ka/r3ka/**r4k**/r5k/r6k/r800)全部共享 `src/z80/` 目录——`z80/main.c` 里逐个定义 `r4k_port` 等,`gen.c`/`ralloc.c`/`ralloc2.cc` 共用,靠 `port->id` 区分;configure.ac 里 `AC_DO_PORT(r4k, z80, R4K, ...)` 的第二个参数就是目录名。
- **全新模式**:独立 `src/<port>/` 目录 + 独立 gen/ralloc。

**STC32 决策:以 `mcs251` 为唯一真实端口，`stc32` 仅是用户别名**。不创建
平行的 `src/stc32/` 或 `TARGET_ID_STC32`。理由:
- MCS-251 有全新 16/32 位指令、WR/DR 重叠寄存器、A5 前缀、EDATA 等,mcs51 的 gen.c 无法复用(且 mcs51 多字节表示与 251 原生宽数据行为不同,见项目计划 §16.2)。
- 与 z80 系列也无指令/寄存器共性。
- 因此使用独立的 `src/mcs251/` backend；`-mstc32` 在 `SDCCmain.c` 中归一化为
  `-mmcs251`。同时参考新式端口的结构与流程，尤其 r4k 所用 `ralloc2.cc`，不要沿用
  mcs51 的老式 ralloc（计划 §16.5）。

## 4. 新端口必需组件(wiki "Adding a port")

| 组件 | 路径 | 说明 |
|---|---|---|
| 汇编器/链接器 | `sdas/`(asxxxx fork)新增 mcs251 后端 | 或外部工具;SDCC 上游建议**先做 assembler** |
| 模拟器 | `sim/ucsim/` 新增 mcs251 CPU | 或外部;**第二步**,供 regression |
| 端口基础设施 | `src/mcs251/main.c` | `mcs251_port` 定义、命令行；`stc32` 为别名 |
| 寄存器/栈分配接口 | `src/mcs251/ralloc.h`、`ralloc.c`、`ralloc2.cc` | 使用新式 ralloc2 |
| 代码生成 | `src/mcs251/gen.h`、`gen.c` | iCode → MCS-251 汇编 |
| peephole 接口 | `src/mcs251/peep.h`、`peep.c` | 初期可最小化 |
| peephole 规则 | `src/mcs251/peeph.def` | **后期再投入**(先 ralloc/codegen 内做优化) |
| runtime 库 | `device/lib/mcs251/` | 尽量 C 实现(参考 noasm2 思路) |
| regression | `support/regression/ports/mcs251*` | spec.mk + mcs251 模拟器 |

### 当前端口注册点

1. `configure.ac` 保持 `AC_DO_PORT(mcs251, mcs251, MCS251, ...)`；不要注册第二个
   `stc32` PORT。
2. `src/port.h` 使用 `TARGET_ID_MCS251` 和 `extern PORT mcs251_port;`。
3. `src/SDCCmain.c` 的端口表只加入 `&mcs251_port`；`-mstc32` 在此归一化为
   `-mmcs251`。
4. 端口实现位于现有 `src/mcs251/`（main.c/gen.c/gen.h/ralloc*.*/peep.* 等），由现有
   Makefile 体系构建。
5. `device/lib/Makefile.in` 使用 `TARGETS += model-mcs251`。
6. regression 使用 `support/regression/ports/mcs251*`（参考 mcs51-small）。

## 5. PORT 结构体要点(参考 r4k_port,src/z80/main.c:2118)

```c
PORT r4k_port = {
  TARGET_ID_R4K, "r4k", "Rabbit 4000", NULL,  // id / 名称 / 描述
  { glue, true, MODEL_SMALL|MODEL_MEDIUM|MODEL_LARGE, MODEL_SMALL, NULL }, // 模型
  { _r2kAsmCmd, NULL, "-plosgffwy", "-plosgffw", 0, ".asm" },  // 汇编器命令
  { _z80LinkCmd, NULL, NULL, ".rel", 1, _crt, _libs_r4k },    // 链接器命令/启动/库
  { /* peephole 回调: defaultRules, instructionSize, ... */ },
  ...
};
```

STC32 的 PORT 需定义:模型(model small 起步,对应 XSmall memory model)、
汇编命令(指向 sdas251)、链接命令(sdld + mcs251)、crt、lib 列表、peephole 回调。
中断帧 4-byte 与 memory spaces 相关选项在 main.c/命令行处理中实现。

## 6. 新式寄存器分配器(ralloc2.cc)要点

- 接口来自 `src/SDCCralloc.hpp` / `SDCCsalloc.hpp`(与端口无关的通用框架)。
- 端口侧职责:`ralloc2.cc` 定义寄存器集合(如 z80 的 `REG_A ... REG_IYH` 编号宏)与
  **成本函数**(`default_operand_cost` 等),框架做基于 tree decomposition 的最优分配。
- 目标:让框架理解 R0-R15 / WRx / DRx 的**重叠关系**(DR0 ⊇ WR0 ⊇ R0-R3),
  并给出移动/复制成本。文档计划 §16.1 的 overlapping registers 是核心难点。
- 第一版可只开放 R0-R7 / WR0,WR2,WR4,WR6 / DR0,DR4,增加 spill 保正确。

## 7. 代码生成(gen.c)建议覆盖顺序

按项目计划 §10:uint8_t move/load/store → 8-bit 算术 → 比较/分支 → 函数调用 → stack locals
→ uint16_t → pointer → uint32_t → struct/array → switch → function pointer → interrupt
→ mul/div → bit ops → 优化。每条新 codegen 能力都要求有 simulator regression。

## 8. STC32 特有技术难点(在代码中对应位置)

| 难点 | 影响位置 |
|---|---|
| WR/DR/R 重叠寄存器 | ralloc2.cc 寄存器定义与成本函数 |
| endianness(251 原生宽数据) | ABI 文档 + gen.c 多字节访问 |
| memory spaces(DATA/EDATA/XDATA/CODE/SFR/BIT) | gen.c 寻址 + pointer 类型系统 |
| 4-byte interrupt frame | gen.c ISR prologue/epilogue + 硬件验证 |
| Source Mode A5 前缀编码 | sdas251 汇编器 + simulator 解码 |

## 9. 建议开发顺序(与项目计划一致)

1. **sdas251 + disassembler**(先有 ISA 数据库 `isa/mcs251.yaml`,assembler/disassembler/simulator 共享)
2. **ucsim mcs251 CPU core**(逐指令状态机测试)
3. **ABI 冻结文档**(docs/ABI.md)
4. **最小 C backend**(src/mcs251/,uint8_t 起步；用户仍可用 -mstc32)
5. device support + runtime + regression 全量

## 10. 参考端口速查

- `src/z80/main.c` + `ralloc2.cc`:新式 ralloc + 多 PORT 共享(变体模式范例)
- `src/mcs51/`:高度非统一地址空间、老式 ralloc(仅参考寻址处理,勿复制 allocator)
- `src/stm8/`:栈相对寻址 + 统一地址空间(文档推荐)
- `support/regression/ports/mcs51-small/spec.mk`:regression 规格范例
- 完整步骤:SDCC Wiki "Adding a port" (https://sourceforge.net/p/sdcc/wiki/Adding%20a%20port/)
