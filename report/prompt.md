# 提示词汇总 —— lab2

> 本文件汇总本次实验使用 AI 完成各练习时的全部提示词，按练习组织。Buddy System 与 SLUB 为「设计提示词借助 AI 从零生成」任务，各含「生成模块」与「生成测试」两份提示词。

---

## 功能模块一 任务一：page_init（物理内存探测与页描述符初始化）

> 在 `kern/mm/pmm.c` 的 `page_init` 中，根据 `get_memory_base()`/`get_memory_size()` 探测到的物理内存范围，建立 `struct Page` 描述符数组 `pages`：把数组放在链接符号 `end` 之后按页对齐；设置 `va_pa_offset`、`npage`；先把 `[nbase, npage)` 全部置为 `SetPageReserved`；计算描述符数组之后按页对齐的 `freemem`，把 `[PPN(PADDR(freemem)), npage)` 的完整页面通过 `init_memmap(pa2page(freemem_pa), ...)` 交给管理器。要求：内核镜像与描述符数组不进入空闲区，设备树无效时 panic，无完整页面时不调用 init_memmap。

---

## 功能模块二 任务二：Best-Fit 分配算法

> 参考 `kern/mm/default_pmm.c` 的 First-Fit 完整实现，在 `kern/mm/best_fit_pmm.c` 中标有 `//lab2 function2-2` 的位置实现完整的 Best-Fit 物理内存管理模块。该文件已提供 `basic_check` 和 `best_fit_check` 两个测试函数（不可修改），请根据 `struct pmm_manager` 各成员职责自行设计内部状态（用 `free_area_t free_area` 和 `free_list`/`nr_free` 宏）与管理函数，实现 `best_fit_init` / `best_fit_init_memmap` / `best_fit_alloc_pages` / `best_fit_free_pages` / `best_fit_nr_free_pages`，并定义 `best_fit_pmm_manager` 完成注册。分配算法要求：分配 n 页时选择 property >= n 且 property 最小的空闲块。

---

## 功能模块二 任务三：Buddy System（两份提示词）

### 第一份：生成 Buddy System 分配模块并接入内核

> 请参考 ucore 的物理内存管理框架（`struct pmm_manager` 接口见 `kern/mm/pmm.h`，`struct Page`/`free_area_t` 见 `kern/mm/memlayout.h`，双向链表见 `libs/list.h`，First-Fit 参考实现见 `kern/mm/default_pmm.c`），从零实现一个 **Buddy System（伙伴系统）** 物理内存分配器，输出 `kern/mm/buddy_pmm.c` 与 `kern/mm/buddy_pmm.h` 并接入内核。要求：① 内存块大小必须是 2 的幂，分配时把请求页数向上取整到 2 的幂，从足够大的空闲块逐级二分分裂，分裂出的另一半作为伙伴块挂回对应阶链表；释放时若伙伴块同阶空闲则循环向上合并；② 用「阶 k → 空闲块链表」组织空闲块（`buddy_free_list[MAX_ORDER+1]`），块头 `Page` 用 `property` 存块大小、`PageProperty` 标记空闲块头；③ 伙伴关系由块在空闲区内的相对页号 `off` 与阶 `k` 确定（伙伴偏移 `off ^ (1<<k)`），`init_memmap(base,n)` 中 n 非 2 的幂时按贪心最大 2 的幂分解并保证各块相对空闲区起点对齐；④ 实现完整的 `pmm_manager`（`init`/`init_memmap`/`alloc_pages`/`free_pages`/`nr_free_pages`/`check`）并给出 `buddy_pmm_manager` 注册结构，能正常编译运行。

### 第二份：生成测试函数

> 请为上一份提示词生成的 Buddy System 分配器设计自测函数 `buddy_check()`（放在 `kern/mm/buddy_pmm.c` 中，作为 `buddy_pmm_manager.check`）。测试用 `assert` 表达断言、末尾打印成功信息，覆盖以下核心行为与边界情况：① 单页分配/回收往返后空闲页总量不变；② 分配按 2 的幂向上取整（如 `alloc_pages(5)` 消耗 8 页）且返回块按该大小对齐；③ 伙伴分裂与合并——先 `alloc_pages(4)` 再分两次各 `free_pages(…, 2)`：释放低半块时其伙伴仍被占用故不得提前合并（应仍是 2 页块），释放高半块后两半必须合并（该块不再是 2 页块）；④ 超额分配（`alloc_pages(nr_free+1)`）返回 `NULL`；⑤ 结构不变量：第 k 阶链表中每块大小均为 `2^k`，且所有块大小之和等于 `nr_free_pages()`。测试结束后空闲页总量必须恢复初始值（无泄漏、无状态破坏）。

---

## Challenge：SLUB 分配算法（两份提示词）

### 第一份：生成 SLUB 模块并接入内核

> 请参考现有 ucore 物理内存管理框架（页分配接口 `alloc_pages`/`free_pages` 见 `kern/mm/pmm.c`，`page2pa`/`pa2page`/`PADDR` 见 `kern/mm/pmm.h`，双向链表见 `libs/list.h`），从零实现一个**简化版 SLUB 内存对象分配器**（两层架构），输出 `kern/mm/slub.c` 与 `kern/mm/slub.h`。要求：① 第一层复用 `alloc_pages/free_pages` 获取/归还整页；第二层把整页（slab）切成固定大小对象，空闲对象用**内嵌于对象首字节的指针**串成 freelist；② 按 2 的幂 size class（如 16~2048）聚合同大小对象为 cache，每 cache 维护 partial/full 两个 slab 链表；分配从 freelist 弹对象、无空闲 slab 时新建整页切分，释放压回 freelist、slab 空时整页 `free_pages` 归还；③ 大于最大 size class 的请求走 large 路径：直接整页分配并在页首存头信息（页数），释放时据此整页归还；④ 提供 `slub_init`/`slub_malloc(size)`/`slub_free(ptr)` 接口，能正常编译运行。

### 第二份：生成测试函数

> 请为上一份提示词生成的 SLUB 分配器设计自测函数 `slub_check()`（放在 `kern/mm/slub.c` 中），用 `assert` 表达断言、末尾打印成功信息，覆盖不同大小内存对象的分配/释放是否正确：① 各 size class（8/16/24/64/100/512/1024/2048）对象的分配、8 字节对齐、可读写、释放后无泄漏（`nr_active` 归零）；② 同 cache 分配多个对象两两不重叠，释放其中一个后再次分配能复用该对象（freelist LIFO 语义）；③ 大对象（4096/8192 字节）的整页分配、读写与释放；④ 不同大小混合分配、分两批释放后无泄漏。测试结束所有对象均释放完毕，`slub_nr_active()` 回到 0。
