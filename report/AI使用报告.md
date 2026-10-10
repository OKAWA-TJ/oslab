# AI 使用报告 —— lab1

## 一、基本信息

| 项目 | 内容 |
| --- | --- |
| 实验 | lab1：比麻雀更小的麻雀（最小可执行内核） |
| 日期 | 2026-09-22 |
| 组员 | HouDachuan |
| AI 编程工具 | Claude Code（VS Code 扩展） |
| 使用模型 | DeepSeek 系列（通过 cc-switch 配置，会话中途由 `deepseek-v4-pro` 切换为 `deepseek-v4-flash`） |
| 操作系统 | WSL2 / Ubuntu（Linux 6.18.33.2-microsoft-standard-WSL2） |
| 项目仓库 | https://github.com/OKAWA-TJ/oslab |
| 本地路径 | `/opt/riscv/oslab/labcodes/lab1` |

> 说明：本报告记录的是**本次会话中与 AI 协作完成的工作**，内容由 AI 依据会话过程整理，经本人核对后提交。

---

## 二、本次会话的目标

本次会话并未直接进入 lab1 的代码练习，而是完成了**开始写代码之前的所有准备工作**：

1. 排查 QEMU 无法启动的问题；
2. 解读实验指导书中 lab1 的要求；
3. 规划并建立 Git 仓库结构；
4. 完成首次提交并把代码推送到 GitHub。

---

## 三、AI 协作过程

### 阶段 1：QEMU 启动报错排查

**现象**：执行启动命令时报错。

```bash
$ qemu-system-riscv64 \
  --machine virt \
  --nographic \
  --bios default
$: command not found
```

**AI 诊断**：命令开头的 `$` 是 shell 提示符，并非命令的一部分。把它一起复制进终端后，bash 会把 `$` 当作一条命令去查找，因而报 `$: command not found`。

**结论**：去掉开头的 `$` 即可，QEMU 本身安装正常（二进制位于 `/usr/local/bin/qemu-system-riscv64`，源码编译产物位于 `/opt/riscv/qemu-4.1.1/riscv64-softmmu/`）。

**评价**：这是一个典型的"教程复制粘贴陷阱"。AI 快速区分了"命令找不到"与"命令执行失败"两类报错，避免了往工具链方向做无效排查。

### 阶段 2：解读实验指导书

AI 抓取并提炼了指导书的以下页面，形成结构化摘要：

| 页面 | 提炼出的关键信息 |
| --- | --- |
| lab1_5_requirement | 实验报告要求：整体逻辑线、核心函数理解、知识点对照、原理中未覆盖的部分；报告须放在 `labcodes/lab1` 下，并回答所有练习问题 |
| lab0/3_startdash | 环境搭建：`$RISCV` 环境变量、QEMU 编译安装、OpenSBI 固件 |
| lab1_2_2_file | 项目组成：`kern/`、`libs/`、`tools/`、`Makefile`；启动流程 `entry.S → kern_init()` |
| lab1_2_1_exercise | 练习 1（分析 `entry.S` 中 `la sp, bootstacktop` 与 `tail kern_init`）、练习 2（用 GDB 跟踪加电到 `0x80200000`） |
| lab1_3_4_makeit | 构建流程：`make qemu` 生成 `bin/kernel` 与 `bin/ucore.img` |

**评价**：这一步价值较高。指导书内容分散在多个页面，AI 一次性把与 lab1 相关的内容汇总成表格，省去了逐页翻阅的时间。但需注意：**摘要不能替代原文**，练习 2 涉及 GDB 的具体操作仍需回看原始页面。

### 阶段 3：规划 Git 仓库结构

**AI 给出的关键判断**：

- Git 只应保存**实验源码**和**实验报告**这两类"自己产出的、无法自动重新生成的内容"；
- 不应提交：编译产物（`bin/`、`*.o`、`*.d`、`*.img`）、第三方工具（QEMU 源码与编译产物、预编译工具链、62 MB 的 `qemu-4.1.1.tar.xz`）、凭据（API Key、`.env`）；
- 仓库根目录应设在 `labcodes` 的**上一层**，以便后续 lab2、lab3 直接加入同一仓库。

**最终目录结构**：

```
/opt/riscv/oslab/            ← 仓库根
├── .gitignore
└── labcodes/
    └── lab1/
        ├── Makefile
        ├── kern/  libs/  tools/
        └── ai报告/          ← 本报告所在目录
```

**评价**：`.gitignore` 中"忽略什么"的判断是 AI 给出的，原则是"能否由 `make` 重新生成"。这一原则同样适用于后续实验。

### 阶段 4：建立本地仓库并提交

代码由本人从 oslab 网站下载后拷入 `labcodes/lab1`。AI 在执行提交前做了三项检查：

1. 目录层级是否正确（确认不是 `lab1/lab1` 的套娃结构）；
2. 是否混入编译产物（检查 `*.o`、`*.d`、`bin/`，结果为空）；
3. 暂存区内容是否合理（22 个文件，全部为源码与配置文件）。

确认无误后完成首次提交：

```
04e052e lab1: 初始代码与实验报告目录
 22 files changed, 2862 insertions(+)
```

随后将默认分支由 `master` 改名为 `main`（与 GitHub 默认分支保持一致）。

### 阶段 5：推送 GitHub 与网络排错

**首次尝试（SSH）失败**。`git push` 长时间无输出，最终超时：

```
Connection to 20.205.243.166 port 22 timed out
fatal: Could not read from remote repository.
```

**AI 的排查过程**（这一步体现了"分层验证"的价值）：

| 检查项 | 结果 | 推断 |
| --- | --- | --- |
| DNS 解析 `github.com` | 正常（20.205.243.166） | 非 DNS 问题 |
| TCP 22 端口连通性 | 通 | 非端口封锁 |
| `ssh -T git@github.com` | 曾成功返回 `Hi OKAWA-TJ!` | 密钥配置正确 |
| SSH 字符串重测 4 次 | 0/4 成功 | 连接**间歇性**中断 |
| `git ls-remote https://...` | 秒回 | **HTTPS 路径可用** |

**定位**：TCP 层能建连，但 SSH 协议握手阶段被中断 —— 属于典型的网络干扰，而非本地配置错误。

**解决方案**：改用 HTTPS + Personal Access Token。

```bash
git remote set-url origin https://github.com/OKAWA-TJ/oslab.git
git config --global credential.helper store
git push -u origin main
```

推送成功，远程与本地 commit hash 一致：

```
04e052e3082b77fe5fde082701b305c05b139b5b   refs/heads/main
```

**评价**：这是本次会话中 AI 价值最明显的一段。若没有先分层验证，很容易误判为"密钥没配对"而反复重新生成 SSH key，浪费大量时间。

---

## 四、遇到的问题汇总

| # | 问题 | 原因 | 解决方式 |
| --- | --- | --- | --- |
| 1 | `$: command not found` | 把 shell 提示符 `$` 当成命令一起执行 | 去掉开头的 `$` |
| 2 | Git 仓库可能被编译产物污染 | 仓库根目录若设在 `/opt/riscv` 会扫入 QEMU 源码、`.o` 文件和 62 MB 压缩包 | 仓库根设在 `/opt/riscv/oslab`，并添加 `.gitignore` |
| 3 | GitHub SSH 推送超时 | TCP 可连但 SSH 握手被中断，连接质量不稳定 | 改用 HTTPS + Personal Access Token |
| 4 | 提交邮箱未在 GitHub 验证 | `13666309500@163.com` 未加入 GitHub 账号 | 待处理（不影响推送，仅影响提交与账号的关联显示） |

---

## 五、AI 使用经验与反思

**有效的做法：**

1. **先让 AI 分层验证，再下结论。** 阶段 5 中，AI 依次测试了 DNS、TCP、SSH 握手、HTTPS 四条路径，把"网络问题"和"配置问题"区分开。这种排查方式值得在后续实验中沿用。
2. **让 AI 做机械性、可验证的工作。** 例如解析实验指导书、生成 `.gitignore`、执行 `git` 命令。这些工作的结果都能立刻核对，出错成本低。
3. **提交前坚持人工检查。** AI 在 `git add` 之后先跑 `git status` 并逐行确认，再执行 `commit`。这一步挡住了编译产物混入仓库的风险。

**需要警惕的地方：**

1. **AI 的初始方案未必最优。** 会话开头 AI 建议使用 SSH 密钥，并据此生成了密钥对；实际推送时才发现 SSH 路径不通。**方案在执行受阻时应及时调整，而不是反复撞墙。**
2. **AI 无法验证未执行的环节。** `.gitignore` 的规则是按经验写的，在本次会话中**尚未通过一次真实的 `make` 编译来验证**是否覆盖了全部编译产物。这需要在后续编译后回头确认。
3. **凭据不能交给 AI 处理。** API Key、Personal Access Token 一类凭据，本次会话中始终由本人在终端手动输入，未出现在对话内容里。
4. **AI 的摘要不等于理解。** 阶段 2 汇总的指导书内容只是索引，练习 1、练习 2 的分析仍需自己阅读 `entry.S` 和实际用 GDB 调试后才能写进实验报告。

---

## 六、本次产出物清单

| 产出 | 位置 |
| --- | --- |
| Git 仓库（含 `.gitignore`） | `/opt/riscv/oslab` |
| 实验代码（22 个文件） | `/opt/riscv/oslab/labcodes/lab1` |
| SSH 密钥对（ed25519，已配置但当前未用于推送） | `~/.ssh/id_ed25519` |
| HTTPS 凭据（Personal Access Token） | `~/.git-credentials` |
| 本报告 | `/opt/riscv/oslab/labcodes/lab1/ai报告/AI使用报告.md` |

---

## 七、遗留事项

- [ ] 执行 `make` 验证工具链配置正确，并确认 `.gitignore` 是否完整覆盖编译产物；
- [ ] 执行 `make qemu` 验证内核能启动并输出 `(THU.CST) os is loading ...`；
- [ ] 在 GitHub 上验证 `13666309500@163.com` 邮箱，使提交关联到账号；
- [ ] 完成练习 1（`entry.S` 分析）与练习 2（GDB 跟踪启动流程）；
- [ ] 撰写正式实验报告，覆盖指导书要求的四个方面：
  - 本章节整体逻辑线；
  - 各功能的核心函数/模块理解；
  - 本实验知识点与 OS 原理知识点的对照；
  - OS 原理中重要但本实验未覆盖的知识点。
