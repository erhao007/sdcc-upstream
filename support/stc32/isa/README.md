# ISA 数据库(isa/)说明

本目录是 SDCC-STC32 项目的机器可读 ISA **契约数据库**。当前
`tools/opcode_check.py` 与测试矩阵直接消费这些数据；assembler(sdas251)、
disassembler 和 simulator(ucsim-mcs251)仍是手写实现，尚未由 YAML 生成。
因此修改 opcode 时必须同时更新实现与自动测试。让三者逐步共享同一生成源
仍是项目计划 §6 的目标，不能把数据库校验通过等同于模拟器语义已完整覆盖。

## 文件

| 文件 | 内容 |
|---|---|
| `mcs251.yaml` | 指令数据库:65 个指令族 / 269 个合法操作数形式,含 Source 与 Binary 双 opcode 编码 |
| `registers.yaml` | 寄存器文件:R0-R15 / WRj / DRk 重叠别名,ACC、B、PSW、DPTR、SP 等 |
| `addressing_modes.yaml` | 寻址模式定义(operand token → 语义) |
| `README.md` | 本说明 |

## 数据来源与许可

- **权威基准**:Intel 8XC251SB Embedded Microcontroller User's Manual(公开手册,
  附录含全部指令编码表)与 STC32G 官方手册(Source Mode)。
- **机器可读底稿**:`zevorn/sdcc-c251`(GPL-2,SDCC 派生)的
  `sdas/as251/tests/instruction-forms.tsv`——其矩阵逐条标注 Intel 手册页码。
  本仓库的 `mcs251.yaml` 由该 TSV 经 `tools/tsv_to_yaml.py` 生成,编码数据
  本身是 Intel/STC 公开指令表的事实性数据。
- 依据项目规则(AGENTS.md §Rules 1/2),不包含任何 Keil 专有实现;最终硬件
  验证仍需以官方手册为 oracle。

## mcs251.yaml schema(v1)

```yaml
schema_version: 1          # 数据格式版本
arch: mcs251
mode: source               # 本项目目标:Source Mode;binary 列保留作对照
source: "..."              # 数据来源说明
instructions:              # 269 项
  - id: add_a_rn           # 稳定标识(TSV id)
    mnemonic: ADD          # 助记符(大写)
    family: add            # 指令族(小写助记符,65 族)
    operands: [A, Rn]      # 操作数 token 列表
    assembly: "add a,r6"   # 规范汇编形式(示例,含具体寄存器/立即数)
    source_bytes: [0xa5, 0x2e]   # Source Mode 编码(可含 A5 前缀)
    binary_bytes: [0x2e]   # Binary Mode 编码;null 表示无 Binary 形式
    length: 2              # Source 编码字节数(= len(source_bytes))
    flags: [CY, AC, OV, N, Z]    # 影响的 PSW 标志;无则 []
    reference: "Intel A-19/A-28" # 手册出处
```

## 生成与校验

```bash
# 从 TSV 重新生成 mcs251.yaml(TSV 来自 /tmp/sdcc-c251 参考克隆)
python3 tools/tsv_to_yaml.py /tmp/sdcc-c251/sdas/as251/tests/instruction-forms.tsv

# 校验 YAML 一致性/唯一性/与 TSV 同步
PYTHONPATH=tools/pylib python3 tools/opcode_check.py
```

`tools/pylib/`(pyyaml)为本机沙箱下的安装目录,不入库;CI 用系统/apt 的 python3-yaml。
