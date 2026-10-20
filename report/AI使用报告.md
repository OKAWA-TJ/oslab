# AI 使用报告 —— lab2

## 一、基本信息

| 项目 | 内容 |
| --- | --- |
| 实验 | lab2：物理内存和页表 |
| 日期 | 2026-10-20 |
| 组员 | 2411839-侯大川（HouDachuan） |
| AI 编程工具 | Claude Code（VS Code 扩展） |
| 使用模型 | DeepSeek V4 Pro（`deepseek-v4-pro`） |
| 操作系统 | WSL2 / Ubuntu（Linux 6.18.33.2-microsoft-standard-WSL2） |
| 项目仓库 | https://github.com/OKAWA-TJ/oslab |
| 本地路径 | `/opt/riscv/oslab/code` |

> 说明：本报告记录的是**本次会话中与 AI 协作完成的工作**，内容由 AI 依据会话过程整理，经本人核对后提交。

---

## 二、本次会话的目标

本次会话完成 lab2 的全部练习与报告：

1. **功能模块一 任务一**：实现物理内存探测与页描述符初始化 `page_init`；
2. **功能模块二 任务一**：理解 First-Fit 分配算法（思考题）；
3. **功能模块二 任务二**：实现 Best-Fit 分配算法并接入内核；
4. **功能模块二 任务三**：设计两份提示词，借助 AI 从零生成 Buddy System 分配模块与测试；
5. **Challenge（选做）**：设计两份提示词，借助 AI 从零生成 SLUB 对象分配模块与测试；
6. 通过 `make` / `make qemu` / `make grade` 三层验证，并撰写实验报告与本报告。

---

## 三、AI 协作过程

### 阶段 1：解读实验要求与框架

AI 先通读了 `pmm.h` / `memlayout.h` / `mmu.h` / `default_pmm.c` / `dtb.c` / `test_lab2.c` / `inject.py` / `grade.sh` 等关键文件，明确了三个「约定」：

- `struct pmm_manager` 是物理内存管理器的统一接口（`init/init_memmap/alloc_pages/free_pages/nr_free_pages/check` 六个成员）；
- `struct Page` 的 `property`（空闲块大小）与 `PG_property`（空闲块头标记）是 First-Fit/Best-Fit 的通用约定；
- 评分方式：`inject.py` 把 `test_page_init/test_best_fit` 注入 `pmm_init`，`grade.sh` 用 grep 匹配 `[Test 1]/[Test 2]` 的 PASS 行（共 20 分），**不包含** Buddy/SLUB。

**评价**：这一定位至关重要——它决定了「Buddy/SLUB 不能作为默认管理器接入」，否则 `test_best_fit` 里 `pmm_manager == best_fit_pmm_manager` 会失败。

### 阶段 2：实现 page_init

AI 按「设备树 → 页号上界 → Page 数组 → 保留页 → 空闲页交接」的顺序实现 `page_init`，并把 `//lab2 function1 YOUR CODE` 替换为 `//lab2 function1 2411839`。

### 阶段 3：实现 Best-Fit

AI 在 `best_fit_pmm.c` 中实现完整 Best-Fit：`init/init_memmap/free_pages/nr_free_pages` 与 First-Fit 相同，`alloc_pages` 改为「遍历找 `property >= n` 且 `property` 最小的块」，并追加 `best_fit_pmm_manager` 注册结构。随后在 `pmm.c` 的 `init_pmm_manager` 中把管理器从 `default_pmm_manager` 切换为 `best_fit_pmm_manager`。

### 阶段 4：Buddy System（两份提示词 + 生成）

AI 先设计第一份提示词（从零生成模块并接入内核），据此实现 `buddy_pmm.c/h`：按「阶 k → 空闲块链表」组织，伙伴偏移 `off ^ (1<<k)`，非 2 的幂空闲区按贪心最大 2 的幂分解；再设计第二份提示词（生成测试），实现 `buddy_check` 覆盖单页往返、2 的幂取整与对齐、伙伴分裂/合并、超额分配、结构不变量。

**验证方式**：临时把 `pmm_manager` 切到 `buddy_pmm_manager` 运行确认 `check_buddy() succeeded!`，再切回 Best-Fit 保证评分不受影响。

### 阶段 5：SLUB（两份提示词 + 生成）

AI 设计第一份提示词生成 `slub.c/h`（两层：复用 `alloc_pages/free_pages` 的页层 + 内嵌 freelist 的对象层，size class 聚合为 cache，`>2048` 走大对象整页路径），第二份提示词生成 `slub_check`（各 size class、对象不重叠与复用、大对象、混合无泄漏）。临时在 `kern_init` 中 `pmm_init()` 后调用 `slub_init()+slub_check()` 验证 `check_slub() succeeded!`，再恢复 `kern_init`。

### 阶段 6：三层验证与截图

依次 `make`（编译）、`make qemu`（运行输出）、`make grade`（注入测试 20/20），并生成 `make/make_qemu/grade/buddy/slub` 五张终端截图。

### 阶段 7：撰写实验报告与本报告

AI 按实验报告要求补齐各练习的「代码 + 提示词 + 思考题回答 + 测试说明 + 知识点对照」，并把两处 `未完成` 状态说明替换为 Buddy/SLUB 的完整实现章节。

---

## 四、遇到的问题汇总

| # | 问题 | 原因 | 解决方式 |
| --- | --- | --- | --- |
| 1 | `page_init` 里 `PADDR(freemem)` 得到错误物理地址 | `va_pa_offset` 在调用 `PADDR` 之后才赋值 | 把 `va_pa_offset = PHYSICAL_MEMORY_OFFSET` 提到所有 `PADDR` 之前 |
| 2 | `freemem` 计算偏大，浪费一截内存 | 把描述符数组大小误当作 `npage * sizeof(Page)`，忽略「描述符只覆盖 `[nbase, npage)`」 | 改为 `(npage - nbase) * sizeof(struct Page)` |
| 3 | `best_fit_check` 的 `assert(p0+4==p1)` 失败 | 沿用 First-Fit「找到就 break」，选错了空闲块 | 完整遍历、记录 `property` 最小的块 |
| 4 | Buddy 测试「释放后某块一定是 4 页」断言不稳定 | 空闲区非 2 的幂，且测试时周围内存仍空闲，合并会向上级联 | 分解保证对齐 + `off^(1<<k)` 越界停止；测试改用确定性中间态判定 |
| 5 | SLUB 大 size class 对象可能未对齐 | 对象区紧贴 48 字节页首头，32 字节对象只对齐到 16 | 对象区起点 `ROUNDUP(头尾, obj_size)` |

---

## 五、AI 使用经验与反思

**有效的做法：**

1. **先读懂框架约定与评分方式，再写实现。** 阶段 1 中 AI 先确认「评分只测 page_init + Best-Fit」，据此把 Buddy/SLUB 设计为「临时接入验证、验证后恢复」，避免为演示加分项而破坏 20/20 的评分基线。
2. **让 AI 先复述约定，再生成代码。** `pmm_manager`、`Page.property`、`free_area_t`、`list.h` 是「隐藏的契约」，AI 复述清楚后才能写出与 `test_lab2.c` 一致的实现。
3. **三层验证逐层收敛。** 先 `make` 过编译，再 `make qemu` 看运行输出，最后 `make grade` 看注入测试，问题范围逐步缩小。
4. **让 AI 承担「提示词 → 模块 → 测试」的完整闭环。** Buddy/SLUB 两份提示词的产出（模块 + 测试）能直接编译运行并通过断言，验证了「测试比文档更硬的规格」——断言失败时 AI 的收敛速度远快于人工推演。

**需要警惕的地方：**

1. **AI 的初版实现可能踩「初始化顺序」类坑。** 问题 1、2 都源于对全局状态（`va_pa_offset`、`nbase`）赋值的时序与语义理解不到位，这类 bug 编译期不报、运行期才暴露。
2. **对非理想输入要有警觉。** Buddy 的空闲区页数（31930）不是 2 的幂，直接套用理想化的 `idx ^ (1<<k)` 会出错；SLUB 的页内切分也存在对齐陷阱。AI 需要主动追问「如果不是 2 的幂/不是对齐的会怎样」。
3. **「临时接入再恢复」的改动要可回退。** Buddy/SLUB 的验证都是「改一行/几行 → 跑 → 恢复」，务必在验证后恢复并重新 `make grade` 确认，否则可能把演示代码误留在提交里。
4. **截图与报告要一一对应。** 五张截图分别对应 `make / make qemu / grade / buddy / slub`，报告中的引用路径与文件名要保持一致，避免提交时图片缺失。

---

## 六、本次产出物清单

| 产出 | 位置 |
| --- | --- |
| `page_init` 实现（学生号已替换） | `kern/mm/pmm.c` |
| Best-Fit 分配器 + 注册结构 | `kern/mm/best_fit_pmm.c` |
| Buddy System 模块 + `buddy_check` | `kern/mm/buddy_pmm.c`、`kern/mm/buddy_pmm.h` |
| SLUB 模块 + `slub_check` | `kern/mm/slub.c`、`kern/mm/slub.h` |
| 测试截图（5 张） | `report/images/` |
| 实验报告 | `report/report.md` |
| 提示词汇总 | `report/prompt.md` |
| 本报告 | `report/AI使用报告.md` |

---

## 七、遗留事项

- [ ] 将 lab2 分支（含 `code/` 与 `report/`）提交并推送至 GitHub；
- [ ] 回答 Challenge 助教提问（SLUB 相关）；
- [ ] 核对组内其他成员（李幸之、孙曌宣）的实现与报告，合并为本组最终提交。
