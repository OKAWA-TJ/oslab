# 操作系统实验报告

## 实验基本信息

| 项目 | 内容 |
|------|------|
| **实验名称** | Lab 2: 物理内存和页表 |
| **小组成员** | 2411590-李幸之、2411839-侯大川、2411207-孙曌宣 |
| **完成日期** | 2026-10-20 |

### 小组分工

| 成员 | 负责的练习/模块 |
|------|----------------|
| 2411590-李幸之 | 功能模块一（page_init）、功能模块二（First-Fit 理解 + Best-Fit） |
| 2411839-侯大川 | 功能模块一（page_init）、功能模块二（First-Fit 理解 + Best-Fit） |
| 2411207-孙曌宣 | 功能模块一（page_init）、功能模块二（First-Fit 理解 + Best-Fit） |

> 本实验每位成员独立完成核心编程任务（物理内存探测与页描述符初始化 `page_init`、Best-Fit 分配器实现与接入），随后汇总形成本报告。

---

## 一、实验目的

1. 理解 **页表** 的建立与使用方法：掌握 Sv39 多级页表结构、页表项（PTE）格式、`satp` 寄存器与地址翻译过程。
2. 理解 **物理内存的管理方法**：掌握操作系统如何发现系统物理内存（设备树 / DTB），如何用 Page 描述符数组组织物理页，以及空闲页链表（`free_area_t`）的建立。
3. 理解 **页面分配算法**：掌握 First-Fit 连续分配算法，并在此基础上实现 Best-Fit 分配算法，理解不同分配策略对内存碎片的影响。
4. 理解 **段页式内存管理** 的整体机制：建立「虚拟地址 → 物理地址」的映射关系，完成从「只能直接访问物理地址」到「通过页表访问虚拟地址」的切换。

---

## 二、实验环境

| 成员 | AI 编程工具 | 底层模型 | 备注 |
|------|------------|---------|------|
| 2411590-李幸之 | DeepSeek Harness（DSH，终端 Agent） | DeepSeek Flash（`deepseek-V41-flash`） | WSL2 / Ubuntu（Linux 6.18.33.2），工具链 `riscv64-unknown-elf-*`，QEMU `qemu-system-riscv64` |
| 2411839-侯大川 | Claude Code（VS Code 扩展，终端 Agent） | DeepSeek V4 Pro（`deepseek-v4-pro`） | WSL2 / Ubuntu（Linux 6.18.33.2），工具链 `riscv64-unknown-elf-*`，QEMU `qemu-system-riscv64` |
| 2411207-孙曌宣 | Codex（桌面版） | GPT-6 | Ubuntu 25.10（Linux 6.17.0-41-generic，x86_64）；RISC-V GCC 10.2.0、GDB 10.1、QEMU 7.0.0 |

---

## 三、实验整体逻辑分析

### 3.1 本章节的逻辑主线

本章围绕「**操作系统如何发现、组织并分配物理内存，并在此基础上建立页表**」这一核心主题展开。整章可以概括为一条「自底向上」的实现链：

```
设备树(DTB)探测物理内存范围 → 建立 Page 描述符数组(page_init)
    → 建立空闲页链表(init_memmap) → 页面分配算法(First-Fit/Best-Fit)
    → 页表机制(Sv39, satp) → 虚拟地址与物理地址的映射
```

其逻辑顺序是：

1. **先解决「内存有多少、在哪里」**（物理内存探测）—— 通过 OpenSBI 传给内核的设备树（DTB，地址保存在 `a1` 寄存器）读出 DRAM 的基址与大小。
2. **再解决「如何描述每一页」**（Page 描述符）—— 为物理内存的每一页建立一个 `struct Page` 描述符，连续排列成数组，记录该页的引用计数、状态标志、空闲块大小等信息。
3. **然后解决「空闲页如何组织」**（空闲链表）—— 把描述符数组之后、内存末端之前的完整空闲页通过 `init_memmap` 交给物理内存管理器，组织成按地址排序的双向链表。
4. **接着解决「如何分配与回收」**（分配算法）—— 在 `pmm_manager` 框架下实现具体的分配策略（First-Fit、Best-Fit）。
5. **最后解决「如何建立虚实映射」**（页表）—— 在 `entry.S` 中构造一个只含一个 1GiB 大页的启动页表，设置 `satp` 使能 Sv39 分页，让内核从物理地址空间切换到虚拟地址空间运行。

### 3.2 功能的逐步实现

1. **`kern/init/entry.S`** —— 构造启动页表 `boot_page_table_sv39`：将 `[0xffffffffc0000000, ...)` 的虚拟地址映射到 `[0x80000000, 0xc0000000)` 的物理地址（一个 1GiB 大页），设置 `satp` 使能 Sv39，刷新 TLB，再跳入 C 代码。
2. **`kern/driver/dtb.c`** —— 解析设备树，读出 DRAM 的 `memory_base` / `memory_size`，对外提供 `get_memory_base()` / `get_memory_size()`。
3. **`kern/mm/pmm.c`** —— 实现 `page_init()`：根据设备树信息建立 Page 描述符数组，计算空闲页范围并交给管理器；实现 `init_pmm_manager()` 选择管理器，`pmm_init()` 串起整个初始化流程。
4. **`kern/mm/default_pmm.c`** —— 提供 First-Fit 完整参考实现（本实验的「读懂」对象）。
5. **`kern/mm/best_fit_pmm.c`** —— 实现 Best-Fit 分配器（本实验的「写」对象），并在 `init_pmm_manager()` 中切换到它。

---

## 四、实验内容与实现

### 功能模块一：物理内存初始化

#### 任务一：实现物理内存探测与页描述符初始化（`page_init`）

**需求分析**

`page_init()` 要完成「从设备树信息到可用空闲页」的转换：内核从设备树得知 DRAM 物理范围后，需要为其中可管理的每一页建立 `struct Page` 描述符，并把「内核镜像 + 描述符数组」之外的完整页面交给物理内存管理器。

实现时必须区分四类地址/编号：

| 概念 | 含义 | 示例 |
|------|------|------|
| 虚拟地址 VA | 内核链接、运行使用的地址 | `end = 0xffffffffc0206078` |
| 物理地址 PA | 内存总线上的真实地址 | `PADDR(end) = 0x80206078` |
| 物理页号 PPN | `地址 >> 12`，一页 4KiB | `PPN(0x80200000) = 0x80200` |
| Page 描述符地址 | `struct Page` 数组的某个元素 | `pages[i]` |

关键内存布局（初始化后）：

```
[OpenSBI] [内核镜像] [对齐空隙] [Page 描述符数组] [对齐空隙] [可分配连续物理页]
0x80000000  0x80200000              pages=RUP(end)   freemem            mem_end(0x88000000)
```

**核心代码**（`kern/mm/pmm.c`，对应注释 `//lab2 function1`）：

```c
//lab2 function1 2411839
static void page_init(void) {
    // 从设备树获取物理内存的起点与大小
    uint64_t mem_begin = get_memory_base();
    uint64_t mem_size = get_memory_size();

    // 设备树信息无效时，终止初始化
    if (mem_begin == 0 || mem_size == 0) {
        panic("page_init: invalid physical memory info from DTB");
    }
    uint64_t mem_end = mem_begin + mem_size;

    // 建立虚拟地址与物理地址之间的映射偏移量（供 PADDR 使用）
    va_pa_offset = PHYSICAL_MEMORY_OFFSET;

    // 内核可管理的物理页号上界
    npage = PPN(mem_end);
    if (npage <= nbase) {
        panic("page_init: no usable physical memory");
    }

    // Page 描述符数组紧跟在链接符号 end 之后，并按页对齐
    extern char end[];
    pages = (struct Page *)ROUNDUP((uintptr_t)end, PGSIZE);

    // 先把 [nbase, npage) 范围内的所有页都置为保留状态
    for (size_t i = 0; i < npage - nbase; i++) {
        SetPageReserved(pages + i);
    }

    // 描述符数组之后的地址向上按页对齐，得到第一个可分配物理页
    uintptr_t freemem = ROUNDUP((uintptr_t)pages +
                                (npage - nbase) * sizeof(struct Page), PGSIZE);
    uintptr_t freemem_pa = PADDR(freemem);

    // 输出探测到的物理内存范围
    cprintf("physical memory map:\n");
    cprintf("memory: 0x%016lx, [0x%016lx, 0x%016lx].\n",
            mem_size, mem_begin, mem_end - 1);

    // 只有描述符数组之后、内存末端之前完整的页面才交给分配器
    size_t free_start = PPN(freemem_pa);
    if (free_start < npage) {
        init_memmap(pa2page(freemem_pa), npage - free_start);
    }
}
```

**实现要点说明**

1. **`va_pa_offset = PHYSICAL_MEMORY_OFFSET`**：`PADDR(kva) = kva - va_pa_offset`，是内核虚拟地址到物理地址的换算依据，必须在任何 `PADDR` 调用之前设置好。
2. **`npage = PPN(mem_end)`**：可管理物理页号上界；`nbase = DRAM_BASE / PGSIZE` 是 DRAM 起始页号。因此描述符数组需要覆盖 `[nbase, npage)` 共 `npage - nbase` 个物理页。
3. **`pages` 放在 `end` 之后并按页对齐**：描述符数组本身也占用物理内存，所以它所在的页面必须保持「保留」状态，不能进入空闲区。
4. **先把所有页标记保留**，再只对描述符数组之后的部分调用 `init_memmap`。`init_memmap` 内部会通过 `p->flags = 0` 清除保留位并建立空闲链表，因此「保留页」与「空闲页」被正确区分。
5. **页面对齐**：描述符数组之后向上对齐（`ROUNDUP`）得到 `freemem`，内存末端 `npage = PPN(mem_end)` 天然向下取整，末尾不足一页的部分不会进入空闲区。

**任务 Prompt（AI 协作）**

> 在 `kern/mm/pmm.c` 的 `page_init` 中，根据 `get_memory_base()`/`get_memory_size()` 探测到的物理内存范围，建立 `struct Page` 描述符数组 `pages`：把数组放在链接符号 `end` 之后按页对齐；设置 `va_pa_offset`、`npage`；先把 `[nbase, npage)` 全部置为 `SetPageReserved`；计算描述符数组之后按页对齐的 `freemem`，把 `[PPN(PADDR(freemem)), npage)` 的完整页面通过 `init_memmap(pa2page(freemem_pa), ...)` 交给管理器。要求：内核镜像与描述符数组不进入空闲区，设备树无效时 panic，无完整页面时不调用 init_memmap。

---

### 功能模块二：物理内存分配

#### 任务一：理解 First-Fit 连续物理内存分配算法（思考题）

阅读 `kern/mm/default_pmm.c`，First-Fit 分配器由以下核心函数构成：

| 函数 | 作用 |
|------|------|
| `default_init()` | 初始化空闲链表 `free_list`，`nr_free = 0` |
| `default_init_memmap(base, n)` | 把 `[base, base+n)` 登记为一个空闲块：清空每页 `flags/property/ref`，令 `base->property = n`、`SetPageProperty(base)`，并把 `base` 按地址升序插入 `free_list` |
| `default_alloc_pages(n)` | 从链表头开始找第一个 `property >= n` 的空闲块；若块比 `n` 大则分裂，把剩余部分作为新块插回，`nr_free -= n` |
| `default_free_pages(base, n)` | 释放 `[base, base+n)`：清标志、`base->property = n`、按地址插入链表，再分别与前后相邻空闲块合并（`p + p->property == base` 或 `base + base->property == p`） |

**First-Fit 的完整流程（用自己的话概括）：**

- **初始化空闲页**：`page_init` 算出唯一的连续空闲区间后，`init_memmap` 把它做成一个 `property = 总页数` 的空闲块头节点，插入（初始为空，直接加入）空闲链表，`nr_free` 记录总空闲页数。
- **分配连续物理页**：请求 `n` 页时，从链表头开始线性扫描，命中第一个「块大小 ≥ n」的块，把该块从链表摘下；若块比 `n` 大，就把 `base + n` 处作为新的块头（`property = 原块 - n`）插回原位置，实现「分裂」；`nr_free` 减 `n`，并清除分配出去页头的 `PG_property`。
- **回收页面并合并**：释放时先按地址把释放块插入链表（保持有序），然后检查其前驱：若 `前驱 + 前驱.property == 本块`，则把本块并入前驱；再检查后继：若 `本块 + 本块.property == 后继`，则把后继并入本块。这样相邻空闲块始终合并成一块，减少外部碎片。

**问题：原有的 First-Fit 算法是否有进一步的改进空间？**

有。主要在于：

1. **查找是 O(n) 线性扫描**：空闲块越多，分配越慢。可以用红黑树 / 按大小排序的堆 / 空闲块大小分桶（segregated free list）把「找到合适块」降到 O(log n) 甚至 O(1)。
2. **低地址块被频繁分裂**：First-Fit 倾向从低地址开始分配，长期运行后低地址区碎成大量小块，高地址区反而空闲，产生「碎片向头部聚集」现象。可改用 Next-Fit（从上次分配位置继续）缓解。
3. **分裂阈值缺失**：当剩余部分很小（如只剩 1 页）时仍要维护一个新节点，开销大于收益。可设定「最小分裂阈值」，剩余过小则整块分配不分裂。

#### 任务二：实现 Best-Fit 连续物理内存分配算法

**需求分析**

Best-Fit 与 First-Fit 的区别**仅在分配策略**：分配 `n` 页时，不选第一个「够大」的块，而是遍历整个空闲链表，选「大小 ≥ n 且最接近 n」的那一块（即 `property` 最小的满足项），从而把残留碎片降到最小。其余（初始化、回收、合并、`nr_free_pages`）与 First-Fit 完全一致。

文件 `best_fit_pmm.c` 只提供了 `basic_check` / `best_fit_check` 两个测试函数，管理函数的声明和注册结构需自行设计，且**不得修改两个测试函数**——因此内部状态必须遵守测试的约定（用 `free_area_t free_area` + `free_list` / `nr_free` 宏）。

**核心代码**（`kern/mm/best_fit_pmm.c`，对应注释 `//lab2 function2-2`）：

```c
//lab2 function2-2 2411839
static free_area_t free_area;

#define free_list (free_area.free_list)
#define nr_free (free_area.nr_free)

// best_fit_init / best_fit_init_memmap / best_fit_free_pages /
// best_fit_nr_free_pages 与 default_pmm.c 对应实现相同（略）

static struct Page *
best_fit_alloc_pages(size_t n) {
    assert(n > 0);
    if (n > nr_free) {
        return NULL;
    }
    struct Page *page = NULL;
    list_entry_t *le = &free_list;
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        // 在 property >= n 的空闲块中，记录 property 最小的那一个
        if (p->property >= n &&
            (page == NULL || p->property < page->property)) {
            page = p;
        }
    }
    if (page != NULL) {
        list_entry_t *prev = list_prev(&(page->page_link));
        list_del(&(page->page_link));
        // 若该空闲块大于 n，把剩余部分作为新的空闲块插回原位置
        if (page->property > n) {
            struct Page *p = page + n;
            p->property = page->property - n;
            SetPageProperty(p);
            list_add(prev, &(p->page_link));
        }
        nr_free -= n;
        ClearPageProperty(page);
    }
    return page;
}

const struct pmm_manager best_fit_pmm_manager = {
    .name = "best_fit_pmm_manager",
    .init = best_fit_init,
    .init_memmap = best_fit_init_memmap,
    .alloc_pages = best_fit_alloc_pages,
    .free_pages = best_fit_free_pages,
    .nr_free_pages = best_fit_nr_free_pages,
    .check = best_fit_check,
};
```

**接入内核**：在 `kern/mm/pmm.c` 的 `init_pmm_manager()` 中把管理器从 `default_pmm_manager` 切换为 `best_fit_pmm_manager`：

```c
static void init_pmm_manager(void) {
    pmm_manager = &best_fit_pmm_manager;
    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}
```

**Best-Fit 与 First-Fit 的分配差异**：以 `best_fit_check` 为例，空闲链表里有两个块 `[p0+1, 2 页]` 和 `[p0+4, 1 页]`，请求 1 页时——First-Fit 会选 `p0+1`（第一个够大的块），而 Best-Fit 会选 `p0+4`（`property=1` 更贴合）。测试正是靠 `assert(p0 + 4 == p1)` 来验证这一点。

**任务 Prompt（AI 协作）**

> 参考 `kern/mm/default_pmm.c` 的 First-Fit 完整实现，在 `kern/mm/best_fit_pmm.c` 中标有 `//lab2 function2-2` 的位置实现完整的 Best-Fit 物理内存管理模块。该文件已提供 `basic_check` 和 `best_fit_check` 两个测试函数（不可修改），请根据 `struct pmm_manager` 各成员职责自行设计内部状态（用 `free_area_t free_area` 和 `free_list`/`nr_free` 宏）与管理函数，实现 `best_fit_init` / `best_fit_init_memmap` / `best_fit_alloc_pages` / `best_fit_free_pages` / `best_fit_nr_free_pages`，并定义 `best_fit_pmm_manager` 完成注册。分配算法要求：分配 n 页时选择 property >= n 且 property 最小的空闲块。

**问题：你的 Best-Fit 算法是否有进一步的改进空间？**

有。

1. **查找仍是 O(n)**：Best-Fit 需要遍历整个空闲链表找「最小满足项」，比 First-Fit 平均更慢。可用按 `property` 排序的红黑树/最小堆优化到 O(log n)。
2. **Best-Fit 的固有缺陷——产生更细碎的外部碎片**：它把大块切成「刚好够用 + 极小的剩余」，剩余部分往往小到无法被后续请求利用，反而可能比 First-Fit 更碎。Linux 内核后来就用 `slab/slub` 等对象缓存 + buddy system 组合来规避这种纯「最贴合」策略的碎片问题。
3. **合并时机**：当前只在 `free_pages` 里做「前后合并」，可以补充「释放时也尝试跨过已分配空洞」的惰性合并，或在空闲页紧张时触发一次显式碎片整理（compaction）。

#### 任务三：Buddy System（伙伴系统）分配算法（设计提示词借助 AI 完成）

**任务描述**

Buddy System 把可用存储空间划分为大小必须是 2 的 n 次幂（1, 2, 4, 8, 16, … 页）的存储块进行管理。本任务不提供代码框架和测试函数，需要自行**设计两份提示词**：第一份让 AI 参考现有 ucore 物理内存管理框架、从零生成 Buddy System 分配模块并接入内核；第二份让 AI 针对生成的算法设计测试函数。最终模块与测试需能正常编译运行。

**设计思路**

伙伴系统的核心是一条「分裂 / 合并」的对称规则，与 First-Fit / Best-Fit 的「自由分裂、相邻合并」不同：

- **块大小是 2 的幂**：分配时把请求页数 `n` 向上取整到 `2^ceil(log2(n))`（阶记为 `order`），从「阶 ≥ order」的最小非空链表中取出一个块，**逐级二分**：把一块 `2^k` 页的块切成两块 `2^(k-1)` 页的伙伴块，被切出的那一半挂回 `k-1` 阶链表，直到得到 `2^order` 页的目标块。
- **伙伴关系由地址决定**：一块在空闲区内的相对页号 `off`、阶 `k` 的伙伴块，其相对页号为 `off ^ (1 << k)`。回收时把块放回 `k` 阶链表，若其伙伴块（同阶）也空闲，则二者合并成 `k+1` 阶块，循环向上直到伙伴不空闲或超出空闲区。

**数据结构与不变量**：

| 状态 | 含义 |
|------|------|
| `buddy_free_list[k]` | 阶 k（块大小 `2^k` 页）的空闲块双向链表 |
| `buddy_arena_base` / `buddy_arena_n` | 空闲区起点 / 总页数（`init_memmap` 传入） |
| `buddy_nr_pages` | 当前空闲页总数 |
| 空闲块头 `Page` | `property` 存块大小 `2^k`，`PageProperty` 标记为空闲块头 |

由于 `init_memmap(base, n)` 的 `n` 不一定是 2 的幂，初始化时按「贪心取最大 2 的幂」把空闲区分解成若干块（如 `13 = 8 + 4 + 1`），这种分解保证每块都相对空闲区起点按自身大小对齐，伙伴关系 `off ^ (1<<k)` 得以成立；末端的「尾巴块」因伙伴越界而永远无法向上合并，这是非 2 的幂空闲区固有的外部碎片。

**核心代码**（`kern/mm/buddy_pmm.c`，对应注释 `//lab2 function3`）：

```c
// 初始化：把 [base, base+n) 按贪心最大 2 的幂分解挂到对应阶链表
static void buddy_init_memmap(struct Page *base, size_t n) {
    // ... 清空 [base, base+n) 的 flags/property/ref ...
    buddy_arena_base = base;  buddy_arena_n = n;  buddy_nr_pages = n;
    struct Page *cur = base;  size_t remaining = n;
    while (remaining > 0) {
        size_t k = buddy_floor_order(remaining);
        cur->property = 1UL << k;  SetPageProperty(cur);
        list_add(&buddy_free_list[k], &(cur->page_link));
        cur += (1UL << k);  remaining -= (1UL << k);
    }
}

// 分配：向上取整到 2^order，找到够大的块后逐级二分分裂
static struct Page *buddy_alloc_pages(size_t n) {
    size_t order = buddy_ceil_order(n);
    size_t k = order;
    while (k <= BUDDY_MAX_ORDER && list_empty(&buddy_free_list[k])) k++;
    if (k > BUDDY_MAX_ORDER) return NULL;
    struct Page *block = le2page(list_next(&buddy_free_list[k]), page_link);
    list_del(&(block->page_link));
    while (k > order) {
        k--;
        struct Page *buddy = block + (1UL << k);
        buddy->property = 1UL << k;  SetPageProperty(buddy);
        list_add(&buddy_free_list[k], &(buddy->page_link));
    }
    block->property = 0;  ClearPageProperty(block);
    buddy_nr_pages -= (1UL << order);
    return block;
}

// 回收：放回 k 阶链表后，若伙伴同阶空闲则循环向上合并
static void buddy_free_pages(struct Page *base, size_t n) {
    size_t order = buddy_ceil_order(n);
    // ... 清空 [base, base+2^order) 的 flags/ref ...
    size_t off = buddy_rel(base), k = order;
    while (k < BUDDY_MAX_ORDER) {
        size_t boff = off ^ (1UL << k);
        if (boff >= buddy_arena_n) break;            // 伙伴越界
        struct Page *buddy = buddy_arena_base + boff;
        if (PageProperty(buddy) && buddy->property == (1UL << k)) {
            list_del(&(buddy->page_link));  ClearPageProperty(buddy);
            off = off & ~(1UL << k);  base = buddy_arena_base + off;  k++;
        } else break;
    }
    base->property = 1UL << k;  SetPageProperty(base);
    list_add(&buddy_free_list[k], &(base->page_link));
    buddy_nr_pages += (1UL << order);
}
```

完整实现见 `kern/mm/buddy_pmm.c` 与 `kern/mm/buddy_pmm.h`，并注册为 `buddy_pmm_manager`（`.check = buddy_check`）。

**第一份提示词（生成 Buddy System 模块并接入内核）**

> 请参考 ucore 的物理内存管理框架（`struct pmm_manager` 接口见 `kern/mm/pmm.h`，`struct Page`/`free_area_t` 见 `kern/mm/memlayout.h`，双向链表见 `libs/list.h`，First-Fit 参考实现见 `kern/mm/default_pmm.c`），从零实现一个 **Buddy System（伙伴系统）** 物理内存分配器，输出 `kern/mm/buddy_pmm.c` 与 `kern/mm/buddy_pmm.h` 并接入内核。要求：① 内存块大小必须是 2 的幂，分配时把请求页数向上取整到 2 的幂，从足够大的空闲块逐级二分分裂，分裂出的另一半作为伙伴块挂回对应阶链表；释放时若伙伴块同阶空闲则循环向上合并；② 用「阶 k → 空闲块链表」组织空闲块（`buddy_free_list[MAX_ORDER+1]`），块头 `Page` 用 `property` 存块大小、`PageProperty` 标记空闲块头；③ 伙伴关系由块在空闲区内的相对页号 `off` 与阶 `k` 确定（伙伴偏移 `off ^ (1<<k)`），`init_memmap(base,n)` 中 n 非 2 的幂时按贪心最大 2 的幂分解并保证各块相对空闲区起点对齐；④ 实现完整的 `pmm_manager`（`init`/`init_memmap`/`alloc_pages`/`free_pages`/`nr_free_pages`/`check`）并给出 `buddy_pmm_manager` 注册结构，能正常编译运行。

**第二份提示词（生成测试函数）**

> 请为上一份提示词生成的 Buddy System 分配器设计自测函数 `buddy_check()`（放在 `kern/mm/buddy_pmm.c` 中，作为 `buddy_pmm_manager.check`）。测试用 `assert` 表达断言、末尾打印成功信息，覆盖以下核心行为与边界情况：① 单页分配/回收往返后空闲页总量不变；② 分配按 2 的幂向上取整（如 `alloc_pages(5)` 消耗 8 页）且返回块按该大小对齐；③ 伙伴分裂与合并——先 `alloc_pages(4)` 再分两次各 `free_pages(…, 2)`：释放低半块时其伙伴仍被占用故不得提前合并（应仍是 2 页块），释放高半块后两半必须合并（该块不再是 2 页块）；④ 超额分配（`alloc_pages(nr_free+1)`）返回 `NULL`；⑤ 结构不变量：第 k 阶链表中每块大小均为 `2^k`，且所有块大小之和等于 `nr_free_pages()`。测试结束后空闲页总量必须恢复初始值（无泄漏、无状态破坏）。

**测试为何能有效检验 Buddy System 实现**

| 测试项 | 检验的行为 | 若实现有误会怎样 |
| --- | --- | --- |
| 单页往返 + 总量守恒 | `alloc/free` 基本正确性与 `nr_free` 记账 | 计数错位，往返后 `nr_free` 不等于初始值，`assert` 失败 |
| 向上取整 + 对齐 | 分配大小必须是 2 的幂、块按自身大小对齐 | 若没取整或没对齐，`nr_free` 减量或 `off & (size-1)` 断言失败 |
| 伙伴分裂/合并 | 核心的「二分分裂」与「同阶伙伴合并」语义 | 若低半块被错误提前合并，`p1->property==2` 失败；若高半块未合并，`p1` 仍是 2 页块则 `!PageProperty(p1)\|\|property>=4` 失败 |
| 超额分配返回 NULL | 边界情况：资源不足时正确失败 | 若返回野指针/越界块，后续 `assert` 崩溃 |
| 结构不变量 | 每阶链表块大小一致 + 空闲总量守恒 | 分裂/合并时挂错链表或漏记账会破坏该不变量 |

**验证结果**：临时把 `init_pmm_manager` 切换到 `buddy_pmm_manager` 后编译运行，输出 `buddy system check: arena […), 31930 pages` 与 `check_buddy() succeeded!`，全部断言通过（截图见第五节）；随后恢复 Best-Fit 以保证 `make grade` 仍为 20/20。

#### Challenge：SLUB 分配算法（选做，设计提示词借助 AI 完成）

**任务描述**

SLUB 用「两层架构」实现高效内存单元分配：**第一层**基于页大小的分配，**第二层**在第一层之上实现任意大小内存对象的分配。可简化实现，体现主体思想即可。需设计两份提示词：第一份从零生成 SLUB 对象分配模块并接入内核，第二份生成测试函数检验不同大小对象的分配/释放是否正确。

**设计思路（如何体现 SLUB 主体思想）**

| 层 | 机制 | 本实现的做法 |
| --- | --- | --- |
| 第一层（页） | 从页分配器获取/归还整页 | 直接复用 `pmm_manager` 的 `alloc_pages` / `free_pages` |
| 第二层（对象） | 把整页（slab）切成固定大小对象 | 每页页首放一个 `slub_slab` 描述符，其后对象区按 `obj_size` 对齐切分 |
| 空闲对象管理 | 对象内嵌 freelist | 空闲对象的首 8 字节存「下一个空闲对象」指针，串成单链表，分配/释放 O(1) 且零额外内存 |
| 同大小聚合 | size class + cache | 16~2048 的 2 的幂为 size class，每个 cache 维护 `partial`（部分空闲）/`full`（已满）两个 slab 链表 |
| 大对象 | large 路径 | 超过 2048 字节直接整页分配，页首放一个 `cache == NULL` 的 `slub_slab` 头（`npages` 记页数），释放时据此整页归还 |

**分配 / 释放流程**：

- **分配**：从 `partial` 头 slab 的 freelist 弹出对象，`inuse++`；弹空后把 slab 移入 `full`。若 `partial` 为空，则 `alloc_pages(1)` 新建一个 slab、切分成对象再弹。
- **释放**：把对象压回 freelist，`inuse--`；slab 由满变部分空闲时 `full → partial`；slab 完全空闲时 `free_pages` 整页还给页分配器。

**核心代码**（`kern/mm/slub.c`，对应注释 `//lab2 challenge`）：

```c
// 新建 slab：对象区按 obj_size 对齐切分，全部串入 freelist
static void slub_slab_init(struct slub_slab *s, struct slub_cache *c) {
    s->cache = c;  s->npages = 1;  s->inuse = 0;
    char *start = (char *)ROUNDUP((uintptr_t)s + sizeof(struct slub_slab), c->obj_size);
    s->objs_per_slab = (PGSIZE - (size_t)(start - (char *)s)) / c->obj_size;
    s->freelist = NULL;
    for (unsigned int i = 0; i < s->objs_per_slab; i++) {
        void *obj = start + i * c->obj_size;
        *(void **)obj = s->freelist;   // 空闲对象首字节存下一指针
        s->freelist = obj;
    }
}

// 分配：从 partial 头 slab 弹对象；无则 alloc_pages(1) 新建
static void *slub_cache_alloc(struct slub_cache *c) {
    struct slub_slab *s;
    if (!list_empty(&c->partial)) s = le2slab(list_next(&c->partial));
    else {
        struct Page *page = alloc_pages(1);
        if (!page) return NULL;
        s = (struct slub_slab *)page2kva(page);
        slub_slab_init(s, c);
        list_add(&c->partial, &s->link);  c->nr_slabs++;
    }
    void *obj = s->freelist;
    s->freelist = *(void **)obj;           // 弹出
    if (++s->inuse == s->objs_per_slab) { list_del(&s->link); list_add(&c->full, &s->link); }
    c->nr_active++;
    return obj;
}

// 释放：压回 freelist；空 slab 整页归还
static void slub_cache_free(struct slub_cache *c, struct slub_slab *s, void *obj) {
    *(void **)obj = s->freelist;  s->freelist = obj;  s->inuse--;  c->nr_active--;
    if (s->inuse == 0) { list_del(&s->link); c->nr_slabs--; free_pages(pa2page(PADDR((uintptr_t)s)), 1); }
    else if (s->inuse == s->objs_per_slab - 1) { list_del(&s->link); list_add(&c->partial, &s->link); }
}

// 对外接口：小对象走 cache，大对象走整页
void *slub_malloc(size_t size) {
    if (size == 0) return NULL;
    size = ROUNDUP(size, sizeof(void *));
    if (size > SLUB_MAX_OBJ) {  /* large：整页 + 页首头 */ }
    return slub_cache_alloc(slub_find_cache(size));
}
void slub_free(void *ptr) {
    struct slub_slab *s = (struct slub_slab *)ROUNDDOWN((uintptr_t)ptr, PGSIZE);
    if (s->cache == NULL) free_pages(pa2page(PADDR((uintptr_t)s)), s->npages);
    else slub_cache_free(s->cache, s, ptr);
}
```

**第一份提示词（生成 SLUB 模块并接入内核）**

> 请参考现有 ucore 物理内存管理框架（页分配接口 `alloc_pages`/`free_pages` 见 `kern/mm/pmm.c`，`page2pa`/`pa2page`/`PADDR` 见 `kern/mm/pmm.h`，双向链表见 `libs/list.h`），从零实现一个**简化版 SLUB 内存对象分配器**（两层架构），输出 `kern/mm/slub.c` 与 `kern/mm/slub.h`。要求：① 第一层复用 `alloc_pages/free_pages` 获取/归还整页；第二层把整页（slab）切成固定大小对象，空闲对象用**内嵌于对象首字节的指针**串成 freelist；② 按 2 的幂 size class（如 16~2048）聚合同大小对象为 cache，每 cache 维护 partial/full 两个 slab 链表；分配从 freelist 弹对象、无空闲 slab 时新建整页切分，释放压回 freelist、slab 空时整页 `free_pages` 归还；③ 大于最大 size class 的请求走 large 路径：直接整页分配并在页首存头信息（页数），释放时据此整页归还；④ 提供 `slub_init`/`slub_malloc(size)`/`slub_free(ptr)` 接口，能正常编译运行。

**第二份提示词（生成测试函数）**

> 请为上一份提示词生成的 SLUB 分配器设计自测函数 `slub_check()`（放在 `kern/mm/slub.c` 中），用 `assert` 表达断言、末尾打印成功信息，覆盖不同大小内存对象的分配/释放是否正确：① 各 size class（8/16/24/64/100/512/1024/2048）对象的分配、8 字节对齐、可读写、释放后无泄漏（`nr_active` 归零）；② 同 cache 分配多个对象两两不重叠，释放其中一个后再次分配能复用该对象（freelist LIFO 语义）；③ 大对象（4096/8192 字节）的整页分配、读写与释放；④ 不同大小混合分配、分两批释放后无泄漏。测试结束所有对象均释放完毕，`slub_nr_active()` 回到 0。

**测试为何能有效检验 SLUB 实现**

| 测试项 | 检验的行为 | 若实现有误会怎样 |
| --- | --- | --- |
| 各 size class 分配/对齐/读写 | size class 选择正确、对象对齐、freelist 内存可写 | 选错 cache 或切分偏移错，读写/对齐断言失败 |
| 对象两两不重叠 + 复用 | 同一 slab 内切分不重叠、freelist 压弹（LIFO）正确 | 切分重叠会写坏相邻对象；freelist 指针未正确读写则复用断言 `b==reuse` 失败 |
| 大对象整页路径 | large 对象 `npages` 记录、整页归还正确 | 页数记错或 `cache==NULL` 判定错，`free` 时归还页数错误 |
| 混合分配/分批释放后 `nr_active==0` | 计数守恒、无泄漏、无越界归还 | 若 `nr_active` 计数错位或空 slab 未正确归还，最终 `assert(slub_nr_active()==0)` 失败 |

**验证结果**：临时在 `kern_init` 中 `pmm_init()` 后调用 `slub_init()` + `slub_check()` 编译运行，输出 `check_slub() succeeded!`，全部断言通过（截图见第五节）；随后恢复 `kern_init` 以保证 `make grade` 仍为 20/20。

---

## 五、测试与验证

**测试截图：**

1. 编译链接成功（`make` 输出，生成 `bin/kernel` 与 `bin/ucore.img`）：

![make 编译结果](./images/make.png)

2. 内核启动成功（`make qemu` 输出）：管理器已切换为 `best_fit_pmm_manager`，输出物理内存映射 `[0x80000000, 0x87ffffff]`，`check_alloc_page() succeeded!`，并打印 `satp` 的虚拟/物理地址：

![make qemu 运行结果](./images/make_qemu.png)

3. 自动评分通过（`make grade`，注入 `test_page_init` / `test_best_fit` 测试）：

![make grade 评分结果](./images/grade.png)

评分结果为 **20/20**：

```
  -check page init:                          OK
  -check best fit:                           OK
Total Score: 20/20
```

其中注入的两个测试分别验证了：

- **`[Test 1] Page Init: PASS`**：`va_pa_offset` 正确、设备树内存范围有效、`npage` 等于 `PPN(mem_end)` 且大于 `nbase`、`pages` 非空且页对齐并位于 `end` 之后、空闲页数在合法范围内、内核镜像尾页与描述符数组所在页均保持 `PageReserved`、`page2pa/pa2page` 可逆。
- **`[Test 2] Best-Fit: PASS`**：`pmm_manager` 指向 `best_fit_pmm_manager`（即已切换到 Best-Fit）。

4. Buddy System 自测通过（临时切换 `buddy_pmm_manager` 后 `make qemu`）：输出 `memory management: buddy_pmm_manager`、空闲区大小 `31930 pages` 与 `check_buddy() succeeded!`，即 `buddy_check` 覆盖的单页往返、2 的幂取整与对齐、伙伴分裂/合并、超额分配、结构不变量等断言全部通过：

![buddy 自测结果](./images/buddy.png)

5. SLUB 自测通过（临时在 `kern_init` 中调用 `slub_init()` + `slub_check()` 后 `make qemu`）：输出 `slub check start` 与 `check_slub() succeeded!`，即各 size class 分配/释放、对象不重叠与 freelist 复用、大对象整页路径、混合分配无泄漏等断言全部通过：

![slub 自测结果](./images/slub.png)

---

## 六、实验总结与收获

### 6.1 本实验重要知识点与 OS 原理对照

| 本实验知识点 | OS 原理对应 | 含义、关系与差异 |
| --- | --- | --- |
| Page 描述符数组、`page2pa/pa2page` | 页帧（Page Frame）管理 | 用数组下标 + `nbase` 建立「物理页号 ↔ 描述符」的双向映射，是 OS 管理物理内存的基础数据结构 |
| `free_area_t` 空闲链表 | 空闲区管理 / 空闲链表 | 用双向链表组织连续空闲块，`property` 记块大小；是连续分配算法的通用载体 |
| First-Fit / Best-Fit | 连续内存分配算法 | 原理课讲「首次适应/最佳适应」，实验用 Page 链表落地；差异在于选择「第一个够大」还是「最小够大」的块 |
| Buddy System（伙伴系统） | 连续内存分配算法 | 把内存按 2 的幂分块，「二分分裂 + 同阶伙伴合并」，用位运算 `off ^ (1<<k)` 定位伙伴；是 Linux 等 OS 底层页分配器的原型 |
| SLUB / slab 分配器 | 内存对象分配 / 对象缓存 | 两层架构：页分配器之上再切分固定大小对象，同大小对象聚合为 cache，空闲对象内嵌 freelist；对应原理课的「slab/对象缓存」 |
| 内碎片 / 外碎片 | 内存碎片 | 分配时「分裂大块」产生外部碎片；Best-Fit 旨在减少残留，但可能产生更细碎的小块 |
| 页表 / PTE / Sv39 | 页表与地址翻译 | 实验只建一个 1GiB 大页的启动页表，是原理课「多级页表、页表项格式、虚实映射」的最小化落地 |
| `satp` 寄存器 | 页表基址寄存器 | `satp` 的 MODE 字段使能 Sv39，PPN 字段指向根页表；是硬件地址翻译的入口 |
| TLB / `sfence.vma` | 快表（TLB） | 切换页表后必须刷新 TLB，否则旧映射会残留导致错误 |
| 设备树（DTB）探测内存 | 硬件探测 / 系统启动 | bootloader 通过 DTB 告知内核内存与外设布局，是「系统如何知道硬件」的机制 |
| `PADDR`/`KADDR`、`va_pa_offset` | 地址重定位 | 内核运行在高虚拟地址，但物理内存只有 128MB，需要偏移量换算；与原理课的「逻辑地址→物理地址」同构 |

### 6.2 OS 原理中重要、但本实验未覆盖的知识点

1. **虚拟内存管理 / 缺页中断与请求调页**（对应 lab3/lab4）：本实验只建立静态的 1GiB 大页映射，没有按需分配、缺页异常处理和页面置换。
2. **页面置换算法**（LRU、CLOCK 等）：物理页只「分配/回收」，没有「换入/换出」。
3. **段式存储管理 / 段页式结合**：只涉及页式（Sv39），未实现段表与段的权限/共享语义。
4. **进程地址空间与内核/用户态隔离**：`satp` 只有一个启动页表，还没有为每个进程建立独立页表、也没有 U 态与 S 态的权限切换。
5. **反置页表 / 多级页表动态伸缩**：启动页表是手工静态构造的，未实现通用页表分配器。
6. **内存保护与共享内存**：PTE 的 R/W/X/U 位只是「理解」，尚未用于实际保护策略。

### 6.3 AI 协作开发的经验与遇到的 bug

**AI 生成代码过程中遇到并解决的问题：**

1. **`va_pa_offset` 未先设置就调用 `PADDR`**：最初的 `page_init` 实现把 `PADDR(freemem)` 放在 `va_pa_offset` 赋值之前，导致 `PADDR` 用偏移量 0 换算，得到错误的物理地址。解决：把 `va_pa_offset = PHYSICAL_MEMORY_OFFSET` 提到所有 `PADDR` 调用之前。这是「初始化顺序」类 bug，说明底层代码对全局状态的赋值时机很敏感。
2. **描述符数组大小估算**：一开始想当然地把数组大小当作 `npage * sizeof(struct Page)`，忽略了「描述符只覆盖 `[nbase, npage)`」这一关键点，导致 `freemem` 计算偏大、浪费一截内存。解决：改为 `(npage - nbase) * sizeof(struct Page)`。这是「页号上界」与「描述符个数」概念的混淆。
3. **Best-Fit 测试对「最小满足块」的强约束**：早期实现沿用了 First-Fit「找到就 break」的逻辑，结果 `best_fit_check` 里 `assert(p0 + 4 == p1)` 失败（因为选错了块）。解决：改为完整遍历、记录 `property` 最小的块。这印证了「读懂测试用例能反过来约束实现」。
4. **Buddy System 的「非 2 的幂空闲区」与「合并级联」问题**：空闲区页数（31930）不是 2 的幂，且自测时周围内存仍是空闲的，释放两个相邻半块后合并出的块会继续与空闲伙伴向上「级联」，使「释放后某块一定是 4 页」这类断言不稳定。解决：① `init_memmap` 按贪心最大 2 的幂分解并保证块对齐，伙伴偏移用 `off ^ (1<<k)`、伙伴越界即停止合并；② 测试改用「释放低半块时伙伴仍被占用、不得提前合并（`property==2`）」这一确定性中间态，配合「`!PageProperty(p1) || p1->property >= 4`」判定合并确实发生，而非断言具体块大小。
5. **SLUB 的对象对齐与「内嵌 freelist」的取舍**：整页按 `obj_size` 切分时，若对象区紧贴页首 `slub_slab` 头（48 字节），32 字节及更大的 size class 对象只会对齐到 16 字节，`uint64_t` 写入可能触发未对齐访问。解决：对象区起点 `ROUNDUP(头尾, obj_size)`，牺牲少量页内空间换取每个对象按自身大小对齐。同时用 `cache == NULL` 标记大对象页、`npages` 记页数，使 `slub_free` 无需额外全局元数据即可区分「小对象 slab」与「大对象整页」。

**AI 协作的体会：**

- **让 AI 先读懂框架再写实现**：本实验的 `pmm_manager`、`free_area_t`、`Page` 描述符、`list.h` 是「约定」，AI 只有先复述清楚这些约定，才能写出与测试一致的代码。
- **测试是比文档更硬的规格**：`best_fit_check` 对分配结果有精确断言，AI 的初版被断言「打回」后，调整的收敛速度远快于靠人肉读代码推演。
- **分层验证仍然有效**：先 `make` 确认编译，再 `make qemu` 确认运行输出，最后 `make grade` 确认注入测试通过——三层逐步缩小问题范围。
