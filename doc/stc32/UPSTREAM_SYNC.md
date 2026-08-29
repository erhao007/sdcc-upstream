# Upstream 同步方案记录(可行性验证产物)

> 日期:2026-08-07 · 验证阶段结论,供 `FEASIBILITY.md` 引用。

## 事实基础(已核实)

1. **SDCC 官方源码托管在 SourceForge SVN**,不是 Git:
   - `https://sourceforge.net/p/sdcc/code/` — trunk 最新 r16755(2026-08-07),4.6.0 tag 为 r16639。
   - 官方无 GitHub 仓库。
2. **SourceForge 官方 git mirror 已停止同步**:`/p/sdcc/git-mirror/` 页面显示 "No (more) commits",
   仅剩 trunk / provisional_trunk 分支,不可作为长期上游来源。
3. **本机无 svn / git-svn**:`svn` 不存在,`git svn` 报 "not a git command"。
4. **本环境无法通过 brew 安装新包**:`/opt/homebrew` 写入被拒(`Operation not permitted`,EPERM,
   目录属主与权限正常,判定为环境沙箱限制),`brew install subversion` 不可行。

## 当前采用的方案:tarball 导入(已验证)

- 下载官方 release tarball:`sdcc-src-4.6.0.tar.bz2`(25.3MB)
- sha256 与 Homebrew formula 官方值一致:`5fd6a93e5997ce01756868fe35e441095cfb637894a80c262514a634094973b6`
- 解压为 `upstream/`(SDCC 源码根),内部 `git init` + 初始导入提交(9587 文件)
- 主仓库 `git submodule add ./upstream upstream`,gitlink 160000 @ 6962481

### 可复现性验证结果

```bash
# 主仓库内直接 update --init:通过
git submodule update --init

# 全新 clone 场景:git 2.50 默认禁止 file 传输协议,需显式允许
git clone <repo> /tmp/x && cd /tmp/x
git -c protocol.file.allow=always submodule update --init   # 成功,submodule 完整 checkout
```

注意点:`.gitmodules` 中 `url = ./upstream`(本机相对路径)。推送远端前应改为
真实 fork 地址(见下)。

## 长期方案评估

| 方案 | 可行性 | 说明 |
|---|---|---|
| git-svn 完整历史同步 | 当前环境不可行 | 需 `brew install subversion`(沙箱只读无法安装);SF svn 速度慢;历史庞大(>16k 修订)。若后续环境放开权限可启用 |
| GitHub fork + submodule URL 指向 fork | 推荐(长期) | fork SDCC 镜像到 GitHub 后,把 `.gitmodules` url 改为 fork 地址,`git submodule sync` 即可;tarball 导入的本地仓库可直接 push 为 fork |
| 保持 tarball 快照导入 | 可接受(当前) | 升级时下载新 tarball,对比导入;代价是丢失逐提交历史与 diff 粒度 |

## 结论

验证阶段采用 **tarball 导入 + 本地 submodule**,机制已验证可复现。
进入正式开发(Week 1)时,建议第一步把本地 `upstream` 仓库推送到 GitHub fork,
并把 `.gitmodules` URL 切到 fork,以获得可推送、可多人协作的 submodule。

## 切换到 GitHub fork 的操作步骤(待执行,需 GitHub 凭据)

前提:SDCC 官方没有 GitHub 仓库,无法直接 fork。方案是把本地 `upstream` 仓库
推到一个新建的 GitHub 仓库(即"自建 upstream 镜像 fork")。

1. 在 GitHub 创建空仓库 `<user>/sdcc-upstream`(不要勾选 README/gitignore,保持为空)。
2. 推送本地 upstream(stc32 分支与 main 都推):

   ```bash
   cd upstream
   git remote add origin git@github.com:<user>/sdcc-upstream.git
   git push -u origin main stc32
   ```

3. 主仓库 `.gitmodules` 切换 URL:

   ```bash
   cd ..   # 仓库根
   git config -f .gitmodules submodule.upstream.url https://github.com/<user>/sdcc-upstream.git
   git submodule sync
   git add .gitmodules && git commit -m "Point upstream submodule at GitHub fork"
   ```

4. 推主仓库到 GitHub 仓库 `<user>/sdcc-stc32`,然后在新机器验证:

   ```bash
   git clone git@github.com:<user>/sdcc-stc32.git
   cd sdcc-stc32 && git submodule update --init
   ```

5. CI 切换:**已完成**。`sdcc-upstream` 已设为**公开**(SDCC 为 GPL 开源项目,镜像无敏感信息),
   公开 submodule 无需凭据,`ci.yml` 已用 `actions/checkout@v4` + `submodules: true`,
   并删除 tarball 下载步骤。

6. 日常与上游同步(可选,在 fork 仓库内):添加官方 SVN 为远程并 `git svn` 同步,
   或在 SDCC 发布新版本时用新 tarball 在 fork 上做一次导入提交;小步 rebase 上游改动。
