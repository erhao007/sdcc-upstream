# OpenSTC32 工具链

[English](README.md) | 简体中文

[![STC32 toolchain CI](https://github.com/erhao007/sdcc-upstream/actions/workflows/ci.yml/badge.svg?branch=stc32)](https://github.com/erhao007/sdcc-upstream/actions/workflows/ci.yml)
[![STC32 platform builds](https://github.com/erhao007/sdcc-upstream/actions/workflows/platforms.yml/badge.svg?branch=stc32)](https://github.com/erhao007/sdcc-upstream/actions/workflows/platforms.yml)
[![STC32 toolchain release](https://github.com/erhao007/sdcc-upstream/actions/workflows/release.yml/badge.svg)](https://github.com/erhao007/sdcc-upstream/actions/workflows/release.yml)

OpenSTC32 是一个面向上游贡献的 SDCC 衍生工具链端口，为 Intel MCS-251 架构和
STC32G12K128 单片机提供原生 C 编译器、汇编器、链接器、模拟器、设备头文件及
运行时库，不依赖 Keil C251。

这是独立开发仓库，并非 SDCC 或 STC 官方发行版。首期实现仅支持
**Source Mode**。用户参数 `-mstc32` 只是内部唯一 `mcs251` 端口的别名，
不是另一套 backend 或 ABI。

## 当前发行状态

| 门禁 | 当前公开发行证据 |
| --- | --- |
| Linux x86_64 构建与测试 | GitHub Actions Ubuntu 24.04 已通过 |
| Linux x86_64 发行包 | 已发布，并附 SHA-256 和完整安装清单 |
| macOS 发行包 | 当前公开标签尚未发布 |
| Windows x86_64 发行包 | 当前公开标签尚未发布 |
| STC32G12K128 真板验证 | 项目存在历史证据，但尚未重新执行并绑定当前公开发行身份 |

最新已验证产物见
[GitHub Releases](https://github.com/erhao007/sdcc-upstream/releases/latest)。
发行说明和文件名会明确每个包覆盖的宿主平台。模拟器或宿主构建通过不能替代真板证据。

每个 PR 和默认分支更新都会在 GitHub 托管的 macOS 15 arm64 与 Windows Server
2025 x86_64 runner 上构建，并执行完整产品门禁。任务会在 runner 临时目录生成并
校验发行包，但不会上传。带标签的发行流程仍有独立的合资格法律审查前置门禁；只有
审查允许二进制分发后，Linux、macOS 与 Windows 三个平台全部通过，才会统一发布
一份完整 Release。

## 下载 Linux x86_64 发行包

已安装 GitHub CLI 时可执行：

```sh
tag=stc32-mt3-support-20260830-r7
gh release download "$tag" \
  --repo erhao007/sdcc-upstream \
  --pattern '*-linux-x86_64.tar.gz' \
  --pattern '*-linux-x86_64.tar.gz.sha256'
sha256sum --check ./*.sha256
mkdir openstc32-toolchain
tar -xzf ./*-linux-x86_64.tar.gz -C openstc32-toolchain
openstc32-toolchain/bin/sdcc -mstc32 --version
```

安装树包含：

- `share/openstc32/toolchain.json`：将构建绑定到源码提交；
- `share/openstc32/toolchain-artifacts.json`：记录每个安装文件的路径、大小和
  SHA-256；
- 四种 MCS-251 运行时库及 STC32G12K128 设备头文件。

## 从源码构建

Ubuntu 24.04 可安装与 CI 相同的依赖：

```sh
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  bison build-essential flex libboost-dev libboost-graph-dev \
  libreadline-dev libzstd-dev python3 zlib1g-dev
```

然后从干净检出构建并安装：

```sh
git clone --branch stc32 https://github.com/erhao007/sdcc-upstream.git
cd sdcc-upstream
env -u COMPILER_PATH -u SDCC_HOME \
  bash support/stc32/scripts/build-toolchain.sh
build/install/bin/sdcc -mstc32 --version
```

可通过 `BUILD_DIR`、`PREFIX` 和 `JOBS` 覆盖默认的 `build/`、
`build/install/` 与自动检测的并行数。在 MSYS2 UCRT64 中，版本化的 Windows
原生入口为：

```sh
bash support/stc32/scripts/build-windows.sh --gates
```

该命令是源码构建入口，不代表当前 Windows 发行包已经通过发布验收。

## 编译固件

下面是最小的编译和链接形式。真实固件还必须提供目标板所需的启动代码、内存布局、
设备初始化和应用代码：

```sh
mkdir -p build/firmware
build/install/bin/sdcc -mstc32 -c src/main.c \
  -o build/firmware/main.rel
build/install/bin/sdcc -mstc32 build/firmware/main.rel \
  -o build/firmware/firmware.ihx
```

ABI 与内存模型契约见 [`doc/stc32/ABI.md`](doc/stc32/ABI.md)，架构和端口注册细节见
[`doc/stc32/ARCHITECTURE.md`](doc/stc32/ARCHITECTURE.md)。

## 执行工具链门禁

完成构建后：

```sh
export STC32_TOOLCHAIN_ROOT="$PWD/build/install"
python3 support/stc32/tools/opcode_check.py
PYTHONPATH=tools/pylib python3 support/stc32/tools/run_isa_semantics.py
python3 support/stc32/tools/run_runtime_tests.py
python3 support/stc32/tools/run_abi_tests.py
PYTHONPATH=tools/pylib python3 support/stc32/tools/ucsim_isa_probe.py --strict
python3 support/stc32/tools/ucsim_unknown_mode_probe.py
make -C build/src/mcs251 check
```

每个新增 opcode 都必须带自动化测试；编译器代码生成变更必须增加模拟器回归，并保持
SDCC 既有端口不回退。

## 仓库结构

- `src/mcs251/` — SDCC MCS-251 C backend；
- `sdas/as251/` 与 `sdld/` — 汇编器和链接器支持；
- `sim/ucsim/src/sims/mcs251.src/` — MCS-251 模拟器；
- `device/include/mcs251/` 与 `device/lib/mcs251/` — 设备头文件和运行时；
- `support/stc32/isa/` — Source Mode 规范 ISA 数据；
- `support/stc32/tests/` 与 `support/stc32/tools/` — 自动化门禁；
- `doc/stc32/` — ABI、架构、来源、依赖和上游同步记录。

## 范围与贡献规则

- 仅使用公开 Intel MCS-251 文档、公开 STC32G 文档和 SDCC 源码；
- 不复制、反编译或逆向 Keil C251 专有材料；
- 保持补丁小而清晰，便于后续提交上游审查；
- 正确性优先于优化，始终让 `-mstc32` 归一化到唯一 `mcs251` 端口；
- 报告缺陷时附上精确宿主平台、命令、编译器版本、最小复现和完整错误输出。

可通过 [GitHub Issues](https://github.com/erhao007/sdcc-upstream/issues) 提交可复现
缺陷或改进建议。上游同步记录见
[`doc/stc32/UPSTREAM_SYNC.md`](doc/stc32/UPSTREAM_SYNC.md)。

## 许可证与来源

SDCC 由采用多种自由软件许可证的组件构成。仓库级 [`COPYING`](COPYING)、
[`LICENSE`](LICENSE)、原始 [`doc/README.txt`](doc/README.txt) 以及逐文件声明保持
权威。STC32 专属源码和依赖来源记录在
[`doc/stc32/SOURCE_PROVENANCE.json`](doc/stc32/SOURCE_PROVENANCE.json) 与
[`doc/stc32/THIRD_PARTY.yml`](doc/stc32/THIRD_PARTY.yml)。供合资格法律审查使用的
工程输入记录在
[`doc/stc32/LEGAL_REVIEW_SCOPE.json`](doc/stc32/LEGAL_REVIEW_SCOPE.json) 与
[`doc/stc32/LEGAL_REVIEW_CHECKLIST.md`](doc/stc32/LEGAL_REVIEW_CHECKLIST.md)；
该审查目前仍未完成。

不要把单一仓库许可证标签理解为替代组件或逐文件条款。二进制再分发者应保留相应的
许可证与版权声明，并按适用许可证提供完整对应源码。本节仅为工程提示，不构成法律意见。
