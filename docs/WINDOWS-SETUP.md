# Windows 安装与配置指南（dsh-superagent-windows）

本文是 Windows 上的完整实操手册。领域概念（Trigger/Ticket/Source/Policy）见上游 docs/001。

## 1. 安装插件

**必须先从 web 模板创建 profile**——`dsh plugin add` 新建的 profile 是最小模板（只有 dsh-base，没有 web 应用/webserver），superagent 会一直等待服务、永不激活：

```sh
dsh superagent --from-default-profile web --dump-config > /dev/null
```

然后安装插件（二选一）：

```sh
dsh plugin --profile superagent add "github:https://github.com/deepsleepzzz0211/dsh-superagent-windows"
dsh plugin --profile superagent add <path-to>\dsh-superagent-windows.tgz   # 从 Releases 下载
```

在 profile 的 `cordis.patch.yml` 追加：

```yaml
- id: dsh-superagent
  config:
    stateDir: C:/Users/<you>/.dsh/state/superagent   # 盘符绝对路径，不要写 /Users/...
# 可选：启用飞书 Source
- id: dsh-superagent-feishu
  disabled: false
```

**注意**：stateDir 用 POSIX 风格 `/Users/...` 时，配置仓库前一切正常，配置后 orchestrator 会在 `Sessions.ensure` 处静默崩溃（面板 404 无报错）。必须用盘符路径。

## 2. Agent 专用 GitHub 账号

1. 注册一个专用 GitHub 账号（agent 的所有评论/分支/PR 都署它的名）。
2. 仓库 Settings → Collaborators → 邀请该账号（Write 权限），并用该账号接受邀请。
3. 本机为它单独登录 gh（设备码流程，不动自己的登录态）：

```sh
GH_CONFIG_DIR="C:/Users/<you>/.config-gh-agent" gh auth login -h github.com -p https -w
GH_CONFIG_DIR="C:/Users/<you>/.config-gh-agent" gh api user --jq .login   # 应输出 agent 账号名
```

4. 在 profile 目录建 `.env`（DSH 启动时加载，仅影响该 profile）：

```
GH_CONFIG_DIR=C:/Users/<you>/.config-gh-agent
```

5. 面板 GitHub Source：仓库填 `owner/name`，Bot CLI 填 gh 的**绝对路径**（如 `C:/Program Files/GitHub CLI/gh.exe`；不能填 .cmd 包装）。

> 自己 @ 自己不产生 GitHub 通知——专用账号是必须的，不是可选项。

## 3. Executor（工单工作目录）

准备一个本地克隆，面板 Execution 里填 prepare 命令（worktree 模板，路径换成你的克隆）：

```sh
git -C C:/Users/<you>/src/<repo> fetch --no-tags "https://github.com/$DSH_SUPERAGENT_GITHUB_REPOSITORY.git" "${DSH_SUPERAGENT_GITHUB_PR_HEAD:-HEAD}:refs/dsh-superagent/$DSH_SUPERAGENT_TICKET" &&
git -C C:/Users/<you>/src/<repo> worktree add -b "agent/$DSH_SUPERAGENT_NAME" "$DSH_SUPERAGENT_DIR" "refs/dsh-superagent/$DSH_SUPERAGENT_TICKET"
```

## 4. 飞书 Source（可选）

### 4.1 创建飞书应用（开放平台 https://open.feishu.cn/app ）

1. 创建企业自建应用 → 添加**机器人**能力。
2. 开通权限（六项，缺一会各自在不同阶段报错）：
   - `im:message`（收发消息）
   - `im:message.group_at_msg:readonly`（**接收群聊 @机器人**——接活的命脉）
   - `im:message.p2p_msg:readonly`（接收单聊）
   - `im:message.group_msg`（**获取群组中所有消息**——敏感权限；离线补收读历史必须，缺它报 `230027 access denied`）
   - `im:chat:readonly`（群信息）
   - `im:chat.members:read`（群成员——白名单同步）
3. 事件与回调 → 订阅方式选**长连接** → 添加事件 `im.message.receive_v1`。
4. 版本管理与发布 → 创建版本并发布。

### 4.2 绑定 lark-cli（bot 专用配置）

```sh
lark-cli config init --new        # 后台运行，输出 verification URL；浏览器打开 → 选择已有应用 → 确认
LARKSUITE_CLI_CONFIG_DIR="<bot配置目录>" lark-cli config strict-mode bot --global
```

（用 URL 流程绑定，App Secret 不经过终端；`--app-id/--app-secret-stdin` 方式需要 TTY。）

### 4.3 lark-bot wrapper

飞书 Source 的 CLI 必须是可直接 spawn 的 exe。用 Releases 里的预编译 `lark-bot.exe`（或 `gcc -O2 -o lark-bot.exe larkbot.c` 自编），并在 profile `.env` 里设：

```
LARK_BOT_CLI=C:/path/to/lark-cli.exe
LARK_BOT_CONFIG_DIR=C:/Users/<you>/.lark-cli-dsh-superagent
LARK_MSYS_ROOT=D:/Git/    # 若 PATH 里的 sh 来自 Git Bash（DSH 经它启动命令）则必填
```

面板 Feishu Source：Bot CLI 填 `lark-bot.exe` 的绝对路径，添加群（mentions 模式），Policy 里把群设为白名单同步来源。

## 5. 排障速查

| 症状 | 原因与处置 |
|---|---|
| 配置仓库后面板 404、无报错 | stateDir 写成了 `/Users/...`；改盘符路径（见第 1 节） |
| 插件疑似没加载但面板无报错 | Desktop 模式吞掉激活日志；复制 profile 用 `dsh --profile <副本> --no-open --port <port>` 启动看 `entries did not activate` |
| Ticket prepare 报 `CRYPT_E_NO_REVOCATION_CHECK` | 本仓库已内置修复；若自编译请确认 environment.ts 的 win32 `[http]` 段 |
| 自己 @ 自己无通知 | 用专用 agent 账号派活（第 2 节） |
| `feishuNoBot` 但手工调用正常 | wrapper 的 `LARK_MSYS_ROOT` 未设（MSYS 把 `/open-apis/...` 改写成本地路径） |
| 飞书消费者 `0s 退出 (reason: signal)` 循环 | wrapper 的自持 stdin 管道缺失（预编译 exe 已含） |
| 回复报 `Unexpected end of JSON input` | 旧版 wrapper 的引号问题，换 Releases 里的最新 exe |
| 飞书补收报 `230027` | 开通 `im:message.group_msg` 并发布新版本（第 4.1 节第 2 条） |
| 重启后 orchestrator 不恢复 | 清 `<stateDir>` 同级的 `superagent.lock` 空目录（强杀残留） |
