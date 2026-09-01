# MT-4A migration tests

这是 MT-4A 的最小迁移测试包。它只验证公开源码层的 SDCC 双下划线写法、
裸拼写的机械替换路径和当前已知诊断缺口；不提供公共 compatibility header，
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
- 链接 `native_behavior.c` 并用 uCsim 检查 `abi_test_status == 0x55`，验证
  DATA/IDATA/XDATA/CODE/BIT 的最小读写语义；
- 编译三个 `DIAGNOSTIC_GAP` 样例，并明确记录它们当前仍被接受，不能视为
  支持。正式拒绝应由 MT-4B 更新测试契约。

没有 uCsim 时可以传 `--skip-behavior` 做词法/编译诊断开发，但该结果只能标记
为 SKIP，不能作为 MT-4A 关闭证据。完整来源和 clean-room 规则见
[`doc/mcs251/official-example-plan.md`](../../../../doc/mcs251/official-example-plan.md)
和 [`doc/stc32/SOURCE_MIGRATION_MATRIX.md`](../../../../doc/stc32/SOURCE_MIGRATION_MATRIX.md)。
