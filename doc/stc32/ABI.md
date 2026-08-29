# STC32（mcs251）ABI 1.0

> 状态：Tier 1 强契约，ABI 1.0 已冻结。
>
> 生效日期：2026-08-23。
>
> 证据边界：自动化为 E2（SDCC/sdas251/sdld/uCsim）；真实
> STC32G12K128 已完成 high CODE、4 字节中断帧、嵌套 ISR 以及
> small/large/stack-auto/large+stack-auto 四模式矩阵 E4。该 E4 不等于
> 其他设备、全部外设时序或跨平台发布 E5 已完成。
>
> 依据：公开 Intel 8XC251SB User's Manual、公开 STC32G 手册、SDCC 源码和
> 本仓库 ABI 自动化测试。本项目不追求 Keil C251 ABI、OMF-251 或库二进制兼容。

## 1. 身份、版本与混链

| 项 | 约定 |
|---|---|
| Target | `-mmcs251`；`-mstc32` 是同一端口的用户别名 |
| 架构/端口名 | `mcs251` |
| Source Mode | 唯一支持的指令映射 |
| 文档 ABI 版本 | 1.0 |
| 编译器内部 ABI revision | `PORT` 值 2，对外宏 `__SDCCCALL=2` |
| 对象格式 | SDCC ASxxxx `.rel`，最终 Intel HEX |
| C 调用/返回 | `ECALL` / `ERET`，24 位代码地址 |
| 对象端序 | 大端 |

`__SDCCCALL=2` 是 SDCC 选择头文件/调用约定路径的整数，不是本文档 1.0 的
版本号，两者不能互相替代。

### 1.1 对象 ABI 签名

MCS-251 `.rel` 对象必须通过 `.optsdcc` 的 `O` 记录携带下列规范签名：

```text
stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 model=small stack-auto=0 xstack=0 intlong-reent=0 float-reent=0 reg-params=1 all-callee-saves=0 sdcccall=2 regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1
```

规范化规则如下：

- 字段与顺序必须与上述示例一致，使用单个 ASCII 空格分隔，不得有首尾
  空白、未知键或重复键；总长度不得超过 240 字节；
- `target` 必须是规范化后的 `mcs251`；`-mstc32` 不产生第二种签名身份；
- `model` 仅可为 `small` 或 `large`；六个开关字段仅可为 `0` 或 `1`；
- `reg-params` 表示首参数寄存器槽是否启用；`all-callee-saves` 表示全局
  `--all-callee-saves` 模式。单函数 `#pragma callee_saves` 不改变全局签名，其声明
  必须在所有编译单元中一致；
- `regset=r0-r9,r12-r15` 冻结当前 allocator 可用字节寄存器集；R16..R31
  不开放给 C allocator，手写汇编若使用它们必须自行承担保存/恢复，不得
  假定 C 活动值或 ISR 兼容集会覆盖这些寄存器；
- `compiler-build=mcs251-abi1.0-r1` 是由契约维护者提升的 ABI 兼容构建
  身份，不是时间戳、源码哈希、优化等级或本地构建目录。

链接器对 ABI 1.0 签名执行完整字符串精确比较；任意字段不同必须拒绝。
生产强制已经启用：缺少 `O` 记录、未签名的手写汇编、v0.2/v0.3 对象与旧
runtime/archive 均 fail closed。ABI 1.0 迁移必须全量重建 C 对象、crt0、四套
runtime library 及手写汇编；不得把 v0.3 对象重新标记为 1.0 或绕过链接门禁。
回滚到 ABI v0.3 时也必须整体回滚 compiler/runtime/对象集合，不能与 1.0 混链。

## 2. 标量和对象表示

| C 类型 | 大小 |
|---|---:|
| `char` / `_Bool` | 1 字节 |
| `short` / `int` | 2 字节 |
| `long` / `float` | 4 字节 |
| `long long` | 8 字节 |
| `double` / `long double` | 4 字节（当前端口降级为 `float`，产生 warning 93） |
| `size_t` / `uintptr_t` | 4 字节 |
| `__bit` | 1 bit |

多字节标量在内存中最高有效字节位于最低地址。WR/DR 别名同样以最低编号字节
寄存器保存最高有效字节，例如 WR6 的高字节在 R6、低字节在 R7。MCS-251 ABI
与 `-mmcs51` 的小端对象不兼容。

`--double-8` 当前不是 mcs251 的可用 ABI 选项：编译器必须发出 warning 117
（unknown option），随后仍按 4 字节 `float` 语义处理 `double`。在 ABI 版本更新前
不得把 8 字节 double 对象与当前 runtime 混用。

## 3. 指针表示

| 指针种类 | 大小 | 表示/限制 |
|---|---:|---|
| `__data` / `__idata` | 1 字节 | page-zero 字节地址 |
| `__pdata` | 1 字节 | 分页数据偏移；不与 24 位平铺指针等宽 |
| `__xdata` / `__far` | 3 字节 | 平铺 24 位数据地址 |
| `__code` | 3 字节 | 平铺 24 位代码地址 |
| 无限定/通用指针 | 3 字节 | 当前端口的平铺 24 位表示，无地址空间 tag |
| 函数指针 | 3 字节 | 平铺 24 位代码地址 |

3 字节指针对象按地址升序保存 bits 23:16、15:8、7:0。指针算术和解引用必须在
全部 24 位上进位。转换到 `uintptr_t` 时零扩展到 32 位；转换回来保留低 24 位。
`uintptr_t` 与指针对象不等宽，因此不保证通过 union 叠加实现指针往返。

间接函数调用把 24 位目标装入 DR28，再发射 `ECALL @DR28`。

## 4. 参数与返回值

首个可放入寄存器的标量参数和标量返回值使用同一字节槽：

| 字节数 | 从最低有效字节到最高有效字节的槽 |
|---:|---|
| 1 | DPL |
| 2 | DPL, DPH |
| 3 | DPL, DPH, B |
| 4 | DPL, DPH, B, A |
| 5 | DPL, DPH, B, A, R4 |
| 6 | DPL, DPH, B, A, R4, R5 |
| 7 | DPL, DPH, B, A, R4, R5, R6 |
| 8 | DPL, DPH, B, A, R4, R5, R6, R7 |

因此按通常的高到低书写，4 字节值为 A:B:DPH:DPL；3 字节指针为
B:DPH:DPL。7 字节值不得物化 R7，8 字节值必须使用 R7。位返回值使用 CY。

- ordinary 非重入函数：不能进入首寄存器槽的参数使用静态 overlay 参数区；
  ordinary `__bit` 后续参数使用对应函数的 BSEG `*_PARM_n`。
- `__reentrant` 与 `--stack-auto`：非寄存器参数和自动对象使用向高地址增长的
  SPX 栈；重入位参数使用 b0 等位寄存器槽。
- varargs：可变参数全部从 SPX 栈读取，并遵守 C 默认参数提升；固定参数仍遵守
  普通首参数规则。
- 聚合返回：当前所有受测结构体均使用 DR28 中的隐藏目标指针，callee 写入调用方
  缓冲区；不得依赖小结构体碰巧从标量槽返回。
- 参数不能完整进入寄存器槽的间接调用必须使用 `__reentrant` 函数指针/callee，
  或全程序采用 `--stack-auto`。

所有 C 函数调用使用 24 位 `ECALL`/`ERET`，不因当前链接地址低于 64 KiB 而改用
16 位返回帧。汇编可显式使用 LCALL/LJMP，但其区域限制不属于 C ABI 互操作保证。

## 5. 调用破坏与保存责任

默认调用约定为 caller-saved：

- allocator 管理的 R0..R9、R12..R15 和 b0..b7；
- 参数/返回及原始临时 ACC、B、DPL、DPH、DPXL、DR20、DR24、DR28；
- CY、N、Z 以及未在本契约中明确冻结的算术状态位。

调用方若在调用后仍需要 allocator 寄存器值，必须在调用点保存并严格 LIFO 恢复。
`#pragma callee_saves function` 只把该函数实际使用的 allocator 寄存器保存责任转移给
callee；它不把参数/返回槽或所有原始临时变成 callee-saved。

任何普通 callee 必须在返回前恢复调用边界的 SPX 值，并保持调用前已存在的栈帧
内容不变；只可使用调用边界之上新增长度，并在返回前平衡。

## 6. 中断 ABI

STC32G Source Mode 硬件中断帧为 4 字节：PSW1、PC.23:16、PC.7:0、PC.15:8
按入口实现顺序压栈；RETI 以匹配顺序恢复 PSW1 和 24 位 PC。编译器不修改
`AUXR2.CPUMODE`，ISR 最终必须用 RETI 返回。

强不变量是：ISR 返回后，被中断上下文的所有活动状态必须恢复。当前 legacy backend
对调用其他函数的 bank-0 ISR 使用以下 24 项兼容性保存集合，并严格逆序恢复：

```text
bits, ACC, B, DPL, DPH, DPXL, DR20, DR24, DR28, PSW,
R8, R9, R12, R13, R14, R15, (0+0)..(0+7)
```

DR20/DR24 被 `setjmp` runtime 使用，DR24/DR28 也被 far pointer、隐藏聚合返回和
间接调用路径作为原始临时，因此必须覆盖。DR16 当前只在中断启用前的 startup XINIT
拷贝使用；DR60 是 SPX 别名，均不额外列入上述集合。

24 项序列是 legacy backend 的兼容门禁，不把未来 ralloc2 绑定到相同指令顺序；任何
替代实现仍必须以运行测试证明完整现场恢复。MT-2A 已在真实
STC32G12K128 上观测到内外层 4 字节硬件帧、24 位高 CODE PC/PSW1 恢复、
嵌套 ISR 返回、SPX 平衡及 DR20/DR24/DR28 保持；证据边界见
`docs/deliveries/MT-2A.md`。

## 7. 内存模型、栈与 runtime 边界

- `--model-small`：普通对象位于 page-zero direct/extended data；
- `--model-large`：普通对象位于 XSEG，默认平铺起点 `0x010000`；
- `--stack-auto`：自动对象和非寄存器参数使用 16 位 SPX 栈；
- startup 将 SPX 初始化为 `__start__stack - 1`，栈向高地址增长；
- ECALL 使用 3 字节返回地址，ERET 恢复完整 24 位 PC；
- `setjmp`/`longjmp` 必须保存和恢复 SPX、返回位置及其公开 `jmp_buf` 状态。

具体设备 RAM 容量、栈上限和中断余量属于 device/linker/真实板配置，不由本 ABI
文档用模拟器结果推导。

## 8. 自动化符合性映射

所有下列运行测试由 `tools/run_abi_tests.py` 在 small、large、stack-auto 中执行；
汇编门禁只能辅助定位，运行断言才是 E2 行为证据。

| 契约行为 | 自动化证据 |
|---|---|
| 标量大小、double=4、大端对象 | `test_abi_types.c`、`negative_double8_gate` |
| 指针大小、布局、解引用、uintptr_t 往返 | `test_abi_pointers.c` |
| 1..8 字节槽、位参数/返回、C/ASM 互操作 | `test_abi_cross_asm.c`、`abi_asm_stubs.s`、`asmop_width_gate` |
| 直接调用、callee_saves、默认 caller-save | `test_abi_calling.c`、`caller_save_gate` |
| 聚合传参与隐藏目标返回 | `test_abi_struct_return.c` |
| reentrant、递归和 SPX 自动对象 | `test_abi_reentrant.c` |
| varargs 与默认参数提升 | `test_abi_varargs.c` |
| 间接函数指针/DR28 | `test_abi_funcptr.c` |
| setjmp/longjmp 与 SPX 恢复 | `test_abi_setjmp.c` |
| ISR、4 字节帧、DPXL/DR20/DR24/DR28 保存 | `test_abi_isr.c` 与函数作用域汇编门禁 |
| Region 1 间接调用 | `test_abi_high_code.c`、`high_code_callee.s` |

ABI runner 的 IRAM 控制块固定在 `0x30..0x37`，所有可执行 ABI 镜像从
`--data-loc 0x38` 分配普通 direct data；runner 在执行前校验精确控制符号和 DSEG
起点，避免测试诊断区与寄存器 bank、bit bank 或测试全局对象相互覆盖。

## 9. ralloc2 不可改变项

ralloc2 可以改变寄存器选择、spill、保存顺序和等价指令，但不得静默改变：

- 本文第 2、3 节的对象大小、端序和指针表示；
- 1..8 字节参数/返回槽与位参数/返回通道；
- ordinary/reentrant/stack-auto/varargs/aggregate-return 的边界；
- caller/callee 保存责任、SPX 平衡和 ISR 完整恢复不变量；
- ECALL/ERET 24 位调用、4 字节硬件中断帧和 RETI 边界；
- `setjmp`/`longjmp`、runtime helper 和手写汇编互操作行为。

R16..R31 按第 1.1 节裁决为不进入 allocator。ABI 对象签名的生产发签和
链接期缺失签名拒绝已由 MT-2B 实现；后续 ralloc2 或优化任务不得绕过门禁或
静默改变签名字段。

## 10. 版本规则与未验证项

ABI 1.0 是当前强契约。MT-2B 已完成生产发签、缺失 `O` 拒绝和四套库重建门禁；
MT-2C 已完成四模式板级矩阵，不能以早期 MT-2A 聚焦 E4 单独替代该结论。

ABI 1.0 之后，参数/返回通道、寄存器集合、对象表示、保存责任、memory model、
runtime helper 互操作或混链策略的破坏性变化必须提升 ABI major，并由契约维护者
先说明迁移、旧对象处置、回滚和符合性测试。兼容性澄清或不改变现有二进制契约的
补充可以提升 minor，但仍必须保持对象签名、实现和测试一致。

尾调用优化尚未成为公开 ABI 保证，当前程序不得依赖其发生；这不改变普通调用路径
已经冻结的参数、保存和返回规则。其他设备、完整外设时序以及 macOS/Linux/Windows
发布复现仍分别需要对应 E4/E5，不属于 ABI 1.0 本身的完成证明。
