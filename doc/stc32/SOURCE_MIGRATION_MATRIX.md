# STC32/MCS-251 Source Mode 迁移矩阵

状态：MT-4A 工作矩阵草案；不是 Tier 0/1 契约，也不表示 MT-4A 已关闭。

目标是把公开 C251/8051 C 源码迁移到 SDCC 的 MCS-251 Source Mode，降低源码
迁移成本；不承诺 Keil ABI、OMF-251、对象、库或工程文件兼容。规范写法是
SDCC 的双下划线扩展，用户目标名 `stc32` 与内部端口 `mcs251` 仍是同一工具链。
对象和调用边界遵守 `doc/stc32/ABI.md` ABI 1.0；本包不修改 ABI、前端或 ISA。

## 分类含义

| 分类 | 含义 |
|---|---|
| 原生支持 | SDCC 双下划线写法由当前 MCS-251 前端/后端识别，并有本包正例或行为证据。 |
| 机械替换 | 旧源码的裸拼写不能直接当作目标关键字；在相同语法上下文把裸 token 替换为 SDCC 双下划线形式即可继续编译。它不是对裸拼写的编译器支持。 |
| 兼容头 | 需要项目作者自己提供一对一的地址/声明映射；本包只在测试源内演示，不新增公共兼容头。 |
| 明确不支持 | 没有合法且等价的 Source Mode 映射，必须拒绝或由另一个 Roadmap 任务处理；不得静默忽略。 |

“迁移分类”描述旧源码到本工具链的处理方式；“规范写法状态”描述对应的
双下划线形式。无效向量、寄存器组等当前已知问题另列为 `DIAGNOSTIC_GAP`，
不能被正例通过掩盖。

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
| `interrupt` | `__interrupt(vector)` | 机械替换 | 合法向量的双下划线形式原生；硬件 4 字节帧、RETI 和保存责任遵守 ABI 1.0 | `bare-interrupt`：失败，诊断 token `interrupt`；`__interrupt(64)` 当前接受，记录为 `DIAGNOSTIC_GAP`，转 MT-4B | `native-interrupt` 生成 `reti` 的汇编检查；合法 ISR 的 uCsim 行为属于 ABI runner |
| `using` | `__using(bank)` | 机械替换 | 合法寄存器组的双下划线形式原生；当前 `__using(8)` 接受但未形成稳定拒绝 | `bare-using`：失败，诊断 token `using`；`__using(8)` 为 `DIAGNOSTIC_GAP`，转 MT-4B | `native-interrupt-using` 编译/汇编；非法 bank 只记录 gap，不宣称行为正确 |
| `reentrant` | `__reentrant` | 机械替换 | 双下划线形式原生；SPX、递归、stack-auto 边界遵守 ABI 1.0，不承诺 Keil 栈 ABI | `bare-reentrant`：失败，诊断 token `reentrant` | `native-reentrant` 三模型目标编译；`cleanroom-multifile-project` 三模型跨文件调用；完整栈边界由 ABI runner 覆盖 |
| `naked` | `__naked` | 机械替换 | 仅对显式汇编/手工返回体作迁移建议；普通 C 语句体当前可能接受但不能视为有语义 | `bare-naked`：失败，诊断 token `naked`；含普通 C 语句的 `__naked` 为 `DIAGNOSTIC_GAP`，转 MT-4B | `native-naked-asm` 汇编体；`gap-naked-c-body` 保留为待修复诊断缺口 |

## 明确不支持的输入

这些不是本包通过新增测试“补成支持”的功能：

| 输入 | 处理结论 |
|---|---|
| Keil `.OBJ`/`.LIB`、OMF-251 或其他厂商二进制 | 明确不支持；只接受 SDCC ASxxxx `.rel`/本工具链产物。 |
| Keil C251 ABI、库二进制互链、专有 runtime | 明确不支持；ABI 1.0 只保证本项目定义的 Source Mode ABI。 |
| Keil/STC 专有头文件、startup、linker 配置和反编译实现 | 明确不支持导入或复制；需要基于公开事实独立重写。 |
| 未授权真实项目源码、无法确认再分发范围的厂商包 | 明确不支持作为仓内样本来源；保留为待审计线索。 |
| `__naked` 中普通 C 语句、当前未拒绝的 `__interrupt(64)`、`__using(8)` | 当前实现分别记录为 `DIAGNOSTIC_GAP`，不当作可迁移行为；正式稳定拒绝路由至 MT-4B。 |

## 诊断基线

`support/stc32/tests/migration/run_migration_tests.py` 对负例检查“非零退出码 +
预期 token”，忽略临时路径、行号、列号和版本化文本。这样固定的是可审计的
诊断类别，不把易变的完整英文错误句子当作 API。正例则必须完成编译；gap 样例
当前预期仍能编译，以防止本阶段误把现状写成已拒绝。未来 MT-4B 增加稳定拒绝后，
应同步更新该 gap 的测试契约，不在本包顺手修改前端。

## 仓内原创项目样本

`cleanroom-multifile-project` 对应
`support/stc32/tests/migration/cases/project/cleanroom_app/`。它不是从候选 GitHub
仓库提取，而是本任务直接编写的两个 C 翻译单元和一个公共头。runner 对每个
模型先分别生成 `.rel`，再链接、核对 ABI 控制区和 DSEG 起点，最后由 uCsim
检查 `abi_test_status == 0x55`。因此它补充的是项目形态的 E1/E2 证据，不产生
外部来源授权、E4 真板或 E5 跨平台结论。

## 来源、证据和未闭合项

- 来源规则和当前样本盘点见 [`doc/mcs251/official-example-plan.md`](../mcs251/official-example-plan.md)。
- 仓内现有语义测试见 `src/mcs251/tests/official/README.md`；它们是原创
  clean-room 测试，不是厂商源文件的再分发。
- 本包的迁移样例是 `support/stc32/tests/migration/` 下的新原创文件；其
  SFR/SBIT 映射只存在于测试源，不形成公共 compat header。
- 编译/汇编为 E1；uCsim 行为为 E2；本包不产生 E4 真板或 E5 跨平台结论。
- MT-4A 关闭前仍必须由用户/权利人提供或确认至少一组可合法提取最小样本的
  真实 C251/8051 项目。该项是当前 P3 未闭合阻碍；不因仓内原创种子通过而消失。
