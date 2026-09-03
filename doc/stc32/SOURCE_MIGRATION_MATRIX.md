# STC32/MCS-251 Source Mode 迁移矩阵

状态：MT-4A 已合并；本分支记录 MT-4B 的诊断实现，未表示 MT-4B 已关闭。

目标是把公开 C251/8051 C 源码迁移到 SDCC 的 MCS-251 Source Mode，降低源码
迁移成本；不承诺 Keil ABI、OMF-251、对象、库或工程文件兼容。规范写法是
SDCC 的双下划线扩展，用户目标名 `stc32` 与内部端口 `mcs251` 仍是同一工具链。
对象和调用边界遵守 `doc/stc32/ABI.md` ABI 1.0；本分支不修改 ABI 或 ISA，
只在 MCS-251 前端增加明确的 fail-closed 诊断。

## 分类含义

| 分类 | 含义 |
|---|---|
| 原生支持 | SDCC 双下划线写法由当前 MCS-251 前端/后端识别，并有本包正例或行为证据。 |
| 机械替换 | 旧源码的裸拼写不能直接当作目标关键字；在相同语法上下文把裸 token 替换为 SDCC 双下划线形式即可继续编译。它不是对裸拼写的编译器支持。 |
| 兼容头 | 需要项目作者自己提供一对一的地址/声明映射；本包只在测试源内演示，不新增公共兼容头。 |
| 明确不支持 | 没有合法且等价的 Source Mode 映射，必须拒绝或由另一个 Roadmap 任务处理；不得静默忽略。 |

“迁移分类”描述旧源码到本工具链的处理方式；“规范写法状态”描述对应的
双下划线形式。超出目标范围的向量、寄存器组和 `__naked` C 语句体必须由稳定
诊断拒绝，不能被正例通过掩盖。

## 逐项矩阵

| 旧源码项 | SDCC 规范写法 | 迁移分类 | 现状/边界 | 预期诊断与样例 | 行为验证 |
|---|---|---|---|---|---|
| `data` | `__data` | 机械替换 | `__data` 原生；裸 `data` 被当作普通标识符/错误上下文，不直接支持 | `bare-data`：编译失败，诊断 token `data` | `native-qualifiers` 的 direct DATA 对象；`native_behavior` 及 `cleanroom-multifile-project` 回读 |
| `idata` | `__idata` | 机械替换 | `__idata` 原生；不把 8051 裸拼写加入全局词法 | `bare-idata`：失败，诊断 token `idata` | `native-qualifiers` 编译；`native_behavior` 及 `cleanroom-multifile-project` 回读 |
| `xdata` | `__xdata` | 机械替换 | `__xdata` 原生，指针/对象遵守 ABI 1.0 的 24 位平铺表示 | `bare-xdata`：失败，诊断 token `xdata` | `native-qualifiers` 编译；两个 uCsim 行为样本的 XDATA 回读 |
| `code` | `__code` | 机械替换 | `__code` 原生；不能把裸 `code` 当成兼容关键字 | `bare-code`：失败，诊断 token `code` | `native-qualifiers` 编译；两个行为样本读取常量表 |
| `bit` | `__bit` | 机械替换 | `__bit` 原生，返回/状态语义仍受 ABI/实现边界约束 | `bare-bit`：失败，诊断 token `bit` | `native-qualifiers` 编译；两个行为样本 set/clear/read |
| `sbit` | `__sbit __at (address)` | 兼容头 | 需要项目地址声明映射；测试只用原创 `MIGRATION_SBIT(name,address)` 宏，不提交公共头 | `bare-sbit`：失败，诊断 token `sbit` | `compat-sfr-sbit` 目标编译；SFR 电气行为不由 uCsim/本包外推 |
| `sfr` | `__sfr __at (address)` | 兼容头 | 需要项目地址声明映射；测试只用原创 `MIGRATION_SFR(name,address)` 宏，不复制厂商头 | `bare-sfr`：失败，诊断 token `sfr` | `compat-sfr-sbit` 目标编译；真实寄存器行为仍需设备/板级证据 |
| `interrupt` | `__interrupt(vector)` | 机械替换 | STC32G12K128 的 49 个 Source Mode 中断源使用向量 `0..48`；旧项目的 `__interrupt 1` 还需把参数归一化为 `__interrupt (1)`；硬件 4 字节帧、RETI 和保存责任遵守 ABI 1.0 | `bare-interrupt`：失败，诊断 token `interrupt`；`__interrupt(64)`：非零退出，稳定诊断 token `__interrupt` | `native-interrupt` 与 `real-stc-diyclock-main` 生成 `reti` 的汇编检查；合法 ISR 的 uCsim 行为属于 ABI runner |
| `using` | `__using(bank)` | 机械替换 | Source Mode 使用四个 PSW 选择寄存器组 `0..3`；旧项目的 `__using 1` 还需把参数归一化为 `__using (1)` | `bare-using`：失败，诊断 token `using`；`__using(8)`：非零退出，稳定诊断 token `__using` | `native-interrupt-using` 与 `real-stc-diyclock-main` 编译/汇编；非法 bank 由稳定诊断拒绝 |
| `reentrant` | `__reentrant` | 机械替换 | 双下划线形式原生；SPX、递归、stack-auto 边界遵守 ABI 1.0，不承诺 Keil 栈 ABI | `bare-reentrant`：失败，诊断 token `reentrant` | `native-reentrant` 三模型目标编译；`cleanroom-multifile-project` 三模型跨文件调用；完整栈边界由 ABI runner 覆盖 |
| `naked` | `__naked` | 机械替换 | 仅支持显式 inline assembly/手工返回体；函数体中的 C 局部声明、表达式、控制流和标签均拒绝 | `bare-naked`：失败，诊断 token `naked`；含普通 C 语句的 `__naked`：非零退出，稳定诊断 token `__naked` | `native-naked-asm` 汇编体；`diagnostic-naked-c-body` 验证不生成被静默丢弃的 C 语句 |

## 明确不支持的输入

这些不是本包通过新增测试“补成支持”的功能：

| 输入 | 处理结论 |
|---|---|
| Keil `.OBJ`/`.LIB`、OMF-251 或其他厂商二进制 | 明确不支持；只接受 SDCC ASxxxx `.rel`/本工具链产物。 |
| Keil C251 ABI、库二进制互链、专有 runtime | 明确不支持；ABI 1.0 只保证本项目定义的 Source Mode ABI。 |
| Keil/STC 专有头文件、startup、linker 配置和反编译实现 | 明确不支持导入或复制；需要基于公开事实独立重写。 |
| 未授权真实项目源码、无法确认再分发范围的厂商包 | 明确不支持作为仓内样本来源；保留为待审计线索。 |
| `__naked` 中普通 C 语句、超出 `0..48` 的 `__interrupt`、超出 `0..3` 的 `__using` | 明确不支持；MT-4B 以非零退出和稳定属性 token 拒绝，不生成可被误用的目标代码。 |

## 诊断基线

`support/stc32/tests/migration/run_migration_tests.py` 对负例检查“非零退出码 +
预期 token”，忽略临时路径、行号、列号和版本化文本。这样固定的是可审计的
诊断类别，不把易变的完整英文错误句子当作 API。正例则必须完成编译；MT-4B
负例必须稳定拒绝，避免把此前未处理且后端无法安全实现的语法误标为支持。

## 仓内原创项目样本

`cleanroom-multifile-project` 对应
`support/stc32/tests/migration/cases/project/cleanroom_app/`。它不是从候选 GitHub
仓库提取，而是本任务直接编写的两个 C 翻译单元和一个公共头。runner 对每个
模型先分别生成 `.rel`，再链接、核对 ABI 控制区和 DSEG 起点，最后由 uCsim
检查 `abi_test_status == 0x55`。因此它补充的是项目形态的 E1/E2 证据，不产生
外部来源授权、E4 真板或 E5 跨平台结论。

## 外部真实项目最小派生样本

`stc-diyclock-derived-project` 对应
`support/stc32/tests/migration/cases/project/stc_diyclock/`，来源固定为公开仓库
`zerog2k/stc_diyclock` 的 `src/main.c`、commit
`beb3b5139fe64a4b8bfc9a24539ed5c0cc8c5fe3`。该项目是已归档的真实 STC15
SDCC 多文件应用；本地样例只保留去业务化的 `volatile __bit` 状态、
`__interrupt 1 __using 1` 入口（在样例中归一化为当前规范括号形式）和独立重写的状态转移。`main.c`、适配头和行为驱动
由本任务独立编写，不复制整个上游工程。

当前来源判定必须拆开记录：

| 项目 | 判定 |
|---|---|
| `REAL_PROJECT` | `YES`：来源项目是实际 STC15 应用，而非教学单例 |
| 仓库声明许可证 | `MIT / CONFIRMED`：根目录 `LICENCE` 已固定保存 |
| 可提取文件范围 | `USER_CONFIRMED / MIT_SCOPED`：用户已于 2026-09-01 确认固定 `src/main.c` 的最小派生和公共再分发范围 |
| `src/adc.c` | 排除：文件含 STC MCU International A/D demo attribution |
| 厂商/设备/外设/子模块/二进制 | 排除：不导入、不作为来源样本 |
| MT-4A 来源门禁 | `SATISFIED`：确认记录见样例目录 `AUTHORIZATION.md`；不等于主审关闭或法律审查通过 |

完整固定 revision、许可证、抽取边界和测试命令见该目录的 `PROVENANCE.md`。
这一派生样例提供 E1/E2 源码与行为证据，但不证明 STC32G12K128 真板、E4、E5
或上游外设行为。

## 来源、证据和未闭合项

- 来源规则和当前样本盘点见 [`doc/mcs251/official-example-plan.md`](../mcs251/official-example-plan.md)。
- 仓内现有语义测试见 `src/mcs251/tests/official/README.md`；它们是原创
  clean-room 测试，不是厂商源文件的再分发。
- 本包的迁移样例是 `support/stc32/tests/migration/` 下的新原创文件；其
  SFR/SBIT 映射只存在于测试源，不形成公共 compat header。
- 编译/汇编为 E1；uCsim 行为为 E2；本包不产生 E4 真板或 E5 跨平台结论。
- 当前已有 `stc_diyclock` 真实项目的固定来源、MIT notice、最小派生边界和用户
  确认记录。该确认于 2026-09-01 闭合 MT-4A 的真实样本来源输入，但不替代项目
  主审关闭、qualified legal review 或发布审核。
