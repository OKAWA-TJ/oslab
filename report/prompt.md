# 提示词汇总 —— lab2

> 本文件汇总本次实验使用 AI 完成各练习时的全部提示词，按练习组织。Buddy System 与 SLUB 为「设计提示词借助 AI 从零生成」任务，各含「生成模块」与「生成测试」两份提示词。

---

## 功能模块一 任务一：page_init（物理内存探测与页描述符初始化）

```text
[PROMPT]
任务：在 kern/mm/pmm.c 的 lab2 function1 标记处完成 page_init，
实现物理内存探测、页描述符初始化和空闲页注册。
操作要求：修改实际文件，保留已有代码、lab2 注释和测试，不改变管理器选择。
输出要求：给出修改说明和实际验证结果，未运行的检查明确注明。

[RELY]
读取 pmm.c/h、memlayout.h、mmu.h、dtb.h 和 entry.S，以真实定义为准。
可用 get_memory_base、get_memory_size、init_memmap、pa2page、PADDR、
页对齐及 Page 状态宏、panic、cprintf、链接符号 end。
全局量为 pages、npage、nbase、va_pa_offset；Page 含 ref、flags、
property、page_link。nbase=DRAM_BASE/PGSIZE，描述符数量为 npage-nbase。
当前平台使用从 DRAM_BASE 开始的单段 RAM 和既定直接映射。

[GUARANTEE]
static void page_init(void);
允许增加文件内 static 辅助函数，不修改公共接口。

[SPECIFICATION]
## page_init
Pre-Condition：DTB 和启动映射已就绪，当前管理器已初始化但未注册空闲页。
Post-Condition：
- va_pa_offset 与启动映射一致，等于 PHYSICAL_MEMORY_OFFSET。
- 有效物理上界取 RAM 末端与 PHYSICAL_MEMORY_END 的较小值并向下页对齐；
  npage 表示该上界的物理页号，不是描述符数量。
- pages 位于 end 之后的第一个页边界，容纳 npage-nbase 个描述符；
  pages[i] 对应物理页号 nbase+i，数组及对齐占用不得越界。
- OpenSBI、内核和描述符数组占用页保持保留；其后的完整空闲页全部交给
  管理器，并输出探测到的内存范围。
Case 1：DTB 无效、基址不符合当前平台、计算溢出或布局越界时，panic 终止，
不得先越界访问或注册错误页面。
Case 2：没有剩余完整页时，不调用 init_memmap，报告内存不足并终止启动。
Requirements：区分虚拟地址、物理地址、物理页号和描述符指针；
各描述符初始状态确定，地址与长度检查先于内存写入。
```

---

## 功能模块二 任务二：Best-Fit 分配算法

```text
[PROMPT]
任务：在 kern/mm/best_fit_pmm.c 中完成 Best-Fit 模块，并在 pmm.c
的 init_pmm_manager 中切换为 best_fit_pmm_manager。
操作要求：修改实际文件，保留 lab2 标记、basic_check、best_fit_check
及评分测试，不修改公共结构或已完成的 page_init。
输出要求：说明修改及实际验证结果，确认内核确实使用 Best-Fit。

[RELY]
参考 default_pmm.c，读取 pmm.h、memlayout.h、best_fit_pmm.h 和 libs/list.h。
PageProperty 标识空闲块头，property 表示块页数，page_link 用于链表。
free_area_t 包含 free_list 和 nr_free；原测试直接保存和恢复这两个状态，
并会构造“链表为空但计数尚未清零”的情况。
原测试允许释放已分配块的子区间，也允许合并释放相邻的已分配区间。

[GUARANTEE]
static void best_fit_init(void);
static void best_fit_init_memmap(struct Page *base, size_t n);
static struct Page *best_fit_alloc_pages(size_t n);
static void best_fit_free_pages(struct Page *base, size_t n);
static size_t best_fit_nr_free_pages(void);
const struct pmm_manager best_fit_pmm_manager;
修改 pmm.c 中 static void init_pmm_manager(void)。
提供测试需要的 free_list、nr_free；注册对象的 check 绑定已有 best_fit_check。
允许 static 辅助函数。

[SPECIFICATION]
## best_fit_init
Pre-Condition：首次初始化，无活动分配。
Post-Condition：空闲链表有效且为空，nr_free=0。

## best_fit_init_memmap
Pre-Condition：n>0，给定页面连续、有效、仍保留且未注册。
Post-Condition：全部页面转为空闲，ref=0，计数增加 n；链表按地址有序，
相邻空闲区合并，只有块头保留 PageProperty 和有效长度。

## best_fit_alloc_pages
Pre-Condition：n>0，管理器已初始化。
Post-Condition：成功返回独占的连续 n 页，计数减少 n，返回头不带
PageProperty，ref=0；失败返回 NULL 且状态不变。
Case 1：选择长度不小于 n 的最小空闲块，同长时选低地址块；
从低地址端分配，剩余部分继续作为合法空闲块。
Case 2：链表为空、总量不足或缺少足够连续空间时，分配失败。

## best_fit_free_pages
Pre-Condition：n>0，区间全部已分配、非保留且未释放；允许上述子区间释放。
Post-Condition：ref 清零，计数增加 n，按地址归还并合并两侧相邻空闲块；
合并不重复计数，失效块头标记清除。

## best_fit_nr_free_pages
Pre-Condition：管理器已初始化。
Post-Condition：只读返回空闲页总数，正常状态下等于各空闲块长度之和。

## init_pmm_manager
Pre-Condition：尚未注册空闲区，无活动分配。
Post-Condition：选择 Best-Fit，打印名称并调用一次 init，后续使用同一管理器。

Requirements：n=0 按框架约定断言拒绝；验证最小合适块、分裂、双侧合并、
连续空间不足和全部回收后的计数恢复，不修改原测试以迎合实现。
```

---

## 功能模块二 任务三：Buddy System（两份提示词）

### 第一份：生成 Buddy System 分配模块并接入内核

```text
[PROMPT]
任务：在独立实验副本中新增或完善 kern/mm/buddy_pmm.c/h，
并通过 pmm.c 的 init_pmm_manager 接入 Buddy System。
操作要求：修改实际文件，保留原分配器、公共结构和测试，完成必要构建接入。
输出要求：说明分配/释放约定、接入位置及真实验证结果。

[RELY]
读取 pmm.c/h、memlayout.h、default_pmm.c 和 libs/list.h；page_init 已完成。
pmm_manager 包含 init、init_memmap、alloc_pages、free_pages、nr_free_pages、
check 及 name；可用 Page 状态宏、ref、property 和 page_link。
本方案只管理启动时注册的一段区域 [A,A+N)：
第 k 阶块含 2^k 页，按相对于 A 的页偏移对齐，伙伴也按相对页号判定。
不能把相对对齐误写为物理地址按整个块大小对齐。

[GUARANTEE]
static void buddy_init(void);
static void buddy_init_memmap(struct Page *base, size_t n);
static struct Page *buddy_alloc_pages(size_t n);
static void buddy_free_pages(struct Page *base, size_t n);
static size_t buddy_nr_free_pages(void);
static void buddy_check(void);
const struct pmm_manager buddy_pmm_manager;  // 在头文件添加 extern 声明
修改 pmm.c 中 static void init_pmm_manager(void)。
允许模块私有结构和 static 辅助函数，不依赖尚不存在的堆。

[SPECIFICATION]
## buddy_init
Pre-Condition：无活动分配，尚未注册区域。
Post-Condition：各阶空闲结构为空且有效，空闲计数为零。

## buddy_init_memmap
Pre-Condition：仅注册一次；n>0，页面连续、有效、保留且未注册。
Post-Condition：全部 n 页转为空闲，ref=0；由合法、相对对齐的二次幂块
完整覆盖，无重叠、遗漏或越界，计数为 n；非二次幂长度的尾部也须管理。

## buddy_alloc_pages
Pre-Condition：管理器和区域已初始化，n>0。
Post-Condition：令 a 为不小于 n 的最小二次幂。成功返回独占的 a 页，
相对偏移按 a 对齐，计数减少 a，返回头不带空闲块头标记。
Case 1：有目标阶或更高阶块时，从最小可用阶满足请求，剩余页继续可用。
Case 2：资源不足、没有足够大块、超出支持范围或取整溢出时，
返回 NULL，已有状态不变。

## buddy_free_pages
Pre-Condition：可按原头和原请求 n 归还整个 a 页块；也可按确切二次幂
页数归还对齐、互不重叠且未释放的子块。部分归还后不能再释放原整块。
Post-Condition：计数增加实际归还页数，清理页状态；只合并同阶、空闲且
确为伙伴的块，直到不能继续合并，始终不越过区域边界。
Requirements：相邻且同长不一定是伙伴；合并不重复增加计数。

## buddy_nr_free_pages
Pre-Condition：管理器已初始化。
Post-Condition：只读返回实际空闲页数，等于各阶空闲块页数之和。

## buddy_check
Pre-Condition：Buddy 已注册并启用，具备最小测试所需资源。
Post-Condition：验证单页和非二次幂请求的分配/释放及计数恢复；
断言通过后打印结果，完整测试由下一份提示词补充。

## init_pmm_manager
Pre-Condition：尚未注册空闲区，无活动分配。
Post-Condition：选择并初始化 Buddy，后续注册和自检使用其接口。
```

### 第二份：生成测试函数

```text
[PROMPT]
任务：完善 kern/mm/buddy_pmm.c 中的 buddy_check，检验上一份提示词的实现。
操作要求：修改实际测试，调用真实分配接口，保留生产策略和公共接口。
输出要求：给出用例、对应规格和实际结果，未运行项注明原因。

[RELY]
使用 buddy_pmm.c/h、pmm.h、assert、alloc_pages、free_pages、nr_free_pages。
沿用前一份提示词的取整、相对对齐和子块释放约定；check 须实际绑定并执行。

[GUARANTEE]
static void buddy_check(void);
允许 static 测试辅助函数；独立初始化测试须说明运行入口。

[SPECIFICATION]
## buddy_check
Pre-Condition：Buddy 已启用，测试资源足够；初始化边界测试独立运行。
Post-Condition：全部执行用例有断言，测试页面全部归还，计数和可分配能力恢复。
Case 1：申请 1、3、5 页，检查实际消耗分别为 1、4、8 页、相对对齐、
活动区间不重叠，以及按原参数释放后的计数恢复。
Case 2：验证大块分裂；申请 4 页后分别释放两个 2 页子块，
检查未释放的一半不会被合并为空闲，全部释放后可形成更大块；
同时检查相邻但非伙伴的块不会被错误合并。
Case 3：独立测试 1 页、13 页区域的初始化，确认尾页不丢失。
Case 4：超额申请、耗尽或缺少足够大块时返回 NULL，前后状态不变。
Requirements：核对各阶块大小、相对对齐、唯一性和总量；对同时存活的真实
页块写入不同数据并回读。不得通过重置管理器掩盖泄漏；测试夹具不冒充真实 RAM。
```

---

## Challenge：SLUB 分配算法（两份提示词）

### 第一份：生成 SLUB 模块并接入内核

```text
[PROMPT]
任务：在独立实验副本中新增或完善 kern/mm/slub.c/h，实现两层内存分配：
下层复用 Best-Fit 分配整页，上层提供任意字节数的对象分配。
操作要求：修改实际文件，在 kern/init/init.c 的 pmm_init 后接入初始化
和自检，保留公共结构、原页分配器及测试。
输出要求：说明接口、资源归属和真实验证结果。

[RELY]
读取 pmm.h、memlayout.h、libs/defs.h、libs/list.h 和 kern/init/init.c。
可用 alloc_pages、free_pages、nr_free_pages、page2pa、pa2page、PADDR、
va_pa_offset 和页对齐宏；KADDR 当前被注释，不能直接使用。
小对象类别为 16、32、64、128、256、512、1024、2048 字节；
每个 slab 占一页，大对象使用连续页。只要求当前单核环境的简化实现。

[GUARANTEE]
void slub_init(void);
void *slub_malloc(size_t size);
void slub_free(void *ptr);
size_t slub_nr_active(void);
void slub_check(void);
在 slub.h 声明上述接口，并修改现有 int kern_init(void) 完成接入，
保持其 noreturn 属性。允许模块内 static 辅助函数。

[SPECIFICATION]
## slub_init
Pre-Condition：页管理已就绪，首次初始化且无存活对象。
Post-Condition：各缓存有效且为空，活动计数为零，不预占对象页。

## slub_malloc
Pre-Condition：模块已初始化，无并发调用。
Post-Condition：成功返回至少 size 字节连续可写、至少 8 字节对齐的
内核虚拟地址，与其他存活对象及元数据不重叠。
Case 1：size=0 时返回 NULL，状态不变。
Case 2：小对象使用最小合适类别，优先使用已有空槽；容量扣除管理头与
对齐占用，活动计数和 slab 状态准确。
Case 3：大对象申请足以容纳管理头、填充和请求的连续页，记录释放信息。
Case 4：大小计算溢出、请求超出支持范围或下层分配失败时返回 NULL，
不泄漏页面、不破坏已有对象和状态。

## slub_free
Pre-Condition：ptr 为 NULL 或本模块返回且尚未释放的对象起始地址。
Post-Condition：NULL 无操作；小对象归还槽位并更新计数，最后一个对象
释放时归还整个 slab；大对象按原页头和实际页数完整归还。
其他存活对象不受影响，缓存不残留指向已归还页面的引用。

## slub_nr_active
Pre-Condition：模块已初始化。
Post-Condition：只读返回小对象缓存的存活对象总数，不包含大对象。

## slub_check
Pre-Condition：初始化完成，无其他调用者的存活对象。
Post-Condition：完成小对象和跨页大对象的申请、读写与释放；
活动数和物理空闲页数恢复，详细用例由下一份提示词补充。

## kern_init
Pre-Condition：保留原启动流程。
Post-Condition：pmm_init 后依次调用 slub_init、slub_check，再继续原流程。
```

### 第二份：生成测试函数

```text
[PROMPT]
任务：完善 kern/mm/slub.c 中的 slub_check，验证上一份提示词的对象分配器。
操作要求：修改实际测试，保留分配策略，确认启动流程真正调用自检。
输出要求：说明覆盖的行为与实际运行结果，不把预期结果写成实测。

[RELY]
使用 slub.c/h、pmm.h、assert、slub_malloc、slub_free、slub_nr_active、
nr_free_pages。slub_nr_active 只统计小对象；大对象还需检查物理页回收。

[GUARANTEE]
void slub_check(void);
允许增加 static 测试辅助函数。

[SPECIFICATION]
## slub_check
Pre-Condition：页管理和 SLUB 已就绪，无其他存活对象，记录初始资源计数。
Post-Condition：测试对象全部释放，活动数和物理空闲页数回到初值。
Case 1：覆盖零请求、各类别边界前后、4096 和 8192 字节请求；
检查对齐、请求范围完整可写、不同对象区间不重叠。
Case 2：同一 slab 分配多个对象，验证满、部分空闲、全部空闲的变化；
保留其他对象时不得归还页，最后一个对象释放后应归还页。
Case 3：混合大小对象写入不同数据并完整回读，交错释放后其余数据不变；
大对象释放后物理页数恢复，不能只凭活动对象数为零判断无泄漏。
Case 4：slub_free(NULL) 无副作用；溢出请求和受控内存不足时返回 NULL，
已有对象、计数及资源不受影响。
Requirements：不要求重新申请得到固定地址；不通过重置计数掩盖泄漏；
所有断言通过后才打印成功，未运行项单独说明。
```
