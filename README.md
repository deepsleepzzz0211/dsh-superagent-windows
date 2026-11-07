# dsh-superagent (Windows adaptation)

[![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11%20x64-0078D4)](https://github.com/deepsleepzzz0211/dsh-superagent-windows/releases)
[![License](https://img.shields.io/badge/license-MIT-blue)](#license)

DeepSeek 官方 [dsh-superagent 原型](https://github.com/deepseek-ai/dsh-superagent-prototype)的 **Windows 适配发行版**：开箱支持 DeepSeek Harness Desktop（Electron 托管 profile）与飞书 Source，安装后无需 POSIX 环境（无 WSL/tmux 依赖）。

> 研究原型，非官方产品（与上游一致的免责声明，见 [docs/UPSTREAM-README.md](docs/UPSTREAM-README.md)）。基于上游 v0.1.0 + 社区 [windows-compat 补丁](https://github.com/deepseek-ai/deepseek-harness/discussions/11769) + 本仓库的额外修复。

## 相对上游的 Windows 修复

| # | 问题 | 修复 |
|---|---|---|
| 1 | stateDir 校验只接受 `/` 开头路径 | 接受盘符/UNC 绝对路径（来自 11769） |
| 2 | worker probe 以 `env`/`sh` 前缀 spawn，Windows 无此程序 | win32 直接传 argv（11769） |
| 3 | orchestrator preset 限制 shell 工具名，Windows 下静默丢失挂载 | 按平台/实际暴露取名（11769） |
| 4 | prepare 命令写死 `sh -c` | win32 直接传 argv（11769） |
| 5 | **worker git 丢 `http.sslBackend=schannel`**（`GIT_CONFIG_NOSYSTEM=1` 同时跳过系统配置），TLS 报 `CRYPT_E_NO_REVOCATION_CHECK` | worker-gitconfig 在 win32 追加 `sslBackend = schannel` + `schannelCheckRevoke = false`（本仓库） |
| 6 | 测试套件无 Windows 覆盖 | Windows CI 维度 + 平台分支断言（11769） |

测试：1172 passed / 36 skipped（win32）。

## 安装（Windows）

前置：Node.js 22.19+ 或 24+、git 2.32+、GitHub CLI 2.48+、DeepSeek Harness（DSH Desktop 或 CLI）。

```sh
# 第 1 步（必须先做）：从 web 模板创建 profile —— 直接 `dsh plugin add` 新建的是最小
# profile（没有 web 应用/webserver），superagent 会永远等待服务不激活
dsh superagent --from-default-profile web --dump-config > /dev/null

# 第 2 步：安装插件（二选一）
dsh plugin --profile superagent add "github:https://github.com/deepsleepzzz0211/dsh-superagent-windows"
dsh plugin --profile superagent add <path-to>\dsh-superagent-windows.tgz   # Release 下载
```

然后在 profile 的 `cordis.patch.yml` 里加（**stateDir 必须是盘符绝对路径**）：

```yaml
- id: dsh-superagent
  config:
    stateDir: C:/Users/<you>/.dsh/state/superagent
# 飞书 Source 可选：
- id: dsh-superagent-feishu
  disabled: false
```

启动后按面板提示完成 Source/Policy/Executor 配置。完整图文步骤（agent 专用 GitHub 账号、`GH_CONFIG_DIR` 隔离、飞书应用创建与 `lark-bot` wrapper、排障速查）见 **[docs/WINDOWS-SETUP.md](docs/WINDOWS-SETUP.md)**。

## 飞书 Source（可选）

Windows 上飞书 Source CLI 需要一个可 spawn 的 exe wrapper（隔离 bot 身份、修 MSYS 路径转换、自持 stdin 管道、修正命令行引号）。本仓库 `wrappers/lark-bot/` 提供：

- **源码** `larkbot.c`（gcc 一行编译）
- **预编译 exe**：见 [Releases](https://github.com/deepsleepzzz0211/dsh-superagent-windows/releases)（无需 MinGW）

配置（环境变量，随 dsh-superagent 进程启动前设置，可放 profile `.env`）：

```sh
LARK_BOT_CLI=C:/path/to/lark-cli.exe        # 必填
LARK_BOT_CONFIG_DIR=C:/Users/<you>/.lark-cli-dsh-superagent   # 默认即此
LARK_MSYS_ROOT=D:/Git/                      # 可选；DSH 经 Git Bash 启动命令时必填
```

## 端到端实测（Windows 11 / DSH Desktop 0.2.2-rc.1）

| 通道 | 触发 → 确认 | Ticket → 交付 |
|---|---|---|
| GitHub（issue 评论 @bot） | < 1 min | ~1 min ✅ |
| 飞书（群内 @bot） | 首条轮询即命中（~12 s） | ~3 min ✅ |

详细踩坑记录：[deepseek-harness discussion #11769](https://github.com/deepseek-ai/deepseek-harness/discussions/11769) 与 [#12524](https://github.com/deepseek-ai/deepseek-harness/discussions/12524)。

## License

MIT（与上游一致）。第三方组件许可同上游。
