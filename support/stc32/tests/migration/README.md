# MT-4A/MT-4B migration tests

这是 MT-4A/MT-4B 的最小迁移测试包。它只验证公开源码层的 SDCC 双下划线写法、
裸拼写的机械替换路径和 fail-closed 诊断；不提供公共 compatibility header，
也不导入 Keil/STC 的源文件或二进制。

从仓库根目录使用当前安装树运行：

```sh
python3 support/stc32/tests/migration/run_migration_tests.py \
  --sdcc /path/to/toolchain-install-ac57313/bin/sdcc \
  --ucsim /path/to/toolchain-install-ac57313/bin/ucsim_51
```

runner 默认执行 `small`、`large`、`stack-auto` 三个模型。它会：

- 编译双下划线正例，并对 ISR/naked 样例检查生成汇编中的可观察标记；
- 编译每个裸拼写负例，要求非零退出码和 manifest 中的稳定诊断 token；
- 在临时文件中只做 token 级机械替换，要求替换结果在三个模型编译成功；
- 将旧式 `sfr name = address` / `sbit name = address` 单独归类为兼容头路径，
  要求旧声明被拒绝、测试内原创地址映射在三个模型生成非空目标文件；
- 链接 `native_behavior.c` 并用 uCsim 检查 `abi_test_status == 0x55`，验证
  DATA/IDATA/XDATA/CODE/BIT 的最小读写语义；
- 分别编译并链接原创 `cleanroom_app` 的两个 C 翻译单元，在三个模型下验证
  头文件接口、跨文件调用、`__reentrant`、地址空间对象和 CODE 查表行为；
- 编译并链接 `stc_diyclock` 的去业务化真实项目派生样例，在三个模型下验证
  `__bit`、`__interrupt 1 __using 1`、跨文件调用和稳定状态转移；该样例只保留
  经 provenance 记录的最小源代码形态，不包含厂商头、外设实现或二进制；
- 对该项目原始的无括号属性后缀执行语法归一化测试：旧后缀要求非零退出和稳定
  token，规范括号后缀要求三个模型编译成功；这不是编译器前端修复；
- 编译三个 MT-4B 负例，要求非零退出码和分别对应 `__interrupt`、`__using`、
  `__naked` 的稳定诊断 token；它们不能静默生成目标文件。

没有 uCsim 时可以传 `--skip-behavior` 做词法/编译诊断开发，但该结果只能标记
为 SKIP，不能作为 MT-4A 关闭证据。完整来源和 clean-room 规则见
[`doc/mcs251/official-example-plan.md`](../../../../doc/mcs251/official-example-plan.md)
和 [`doc/stc32/SOURCE_MIGRATION_MATRIX.md`](../../../../doc/stc32/SOURCE_MIGRATION_MATRIX.md)。

`cases/project/cleanroom_app/` 是本任务直接编写的去业务化小项目，不来自任何
GitHub、厂商 SDK 或 Keil 工程。`cases/project/stc_diyclock/` 是基于固定
`zerog2k/stc_diyclock` `src/main.c` 的最小派生样例，许可证和排除范围见其
`PROVENANCE.md`，用户确认见 `AUTHORIZATION.md`。该确认闭合 MT-4A 的真实样本
来源输入，但不能把测试通过解释为项目主审关闭或法律审查通过；MT-4B 是否已授权以
项目控制面单独签发的 `CONTINUE MT-4B` 令牌为准。
