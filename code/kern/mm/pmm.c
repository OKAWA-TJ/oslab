#include <default_pmm.h>
#include <best_fit_pmm.h>
#include <defs.h>
#include <error.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <sbi.h>
#include <stdio.h>
#include <string.h>
#include <riscv.h>
#include <dtb.h>

// virtual address of physical page array
struct Page *pages;
// amount of physical memory (in pages)
size_t npage = 0;
// the kernel image is mapped at VA=KERNBASE and PA=info.base
uint64_t va_pa_offset;
// memory starts at 0x80000000 in RISC-V
// DRAM_BASE defined in riscv.h as 0x80000000
const size_t nbase = DRAM_BASE / PGSIZE;

// virtual address of boot-time page directory
uintptr_t *satp_virtual = NULL;
// physical address of boot-time page directory
uintptr_t satp_physical;

// physical memory management
const struct pmm_manager *pmm_manager;


static void check_alloc_page(void);

// init_pmm_manager - initialize a pmm_manager instance
static void init_pmm_manager(void) {
    pmm_manager = &best_fit_pmm_manager;
    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}

// init_memmap - call pmm->init_memmap to build Page struct for free memory
static void init_memmap(struct Page *base, size_t n) {
    pmm_manager->init_memmap(base, n);
}

// alloc_pages - call pmm->alloc_pages to allocate a continuous n*PAGESIZE
// memory
struct Page *alloc_pages(size_t n) {
    return pmm_manager->alloc_pages(n);
}

// free_pages - call pmm->free_pages to free a continuous n*PAGESIZE memory
void free_pages(struct Page *base, size_t n) {
    pmm_manager->free_pages(base, n);
}

// nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE)
// of current free memory
size_t nr_free_pages(void) {
    return pmm_manager->nr_free_pages();
}

//lab2 function1 2411839
/* page_init - 根据设备树探测到的物理内存信息建立 Page 描述符数组，
 * 并把内核镜像与描述符数组之后的空闲物理页交给当前物理内存管理器。
 * 要求：
 *   1. 区分虚拟地址、物理地址、物理页号与 Page 描述符地址；
 *   2. 正确处理可管理上界与页面对齐；
 *   3. 内核及描述符数组不会进入空闲区；
 *   4. 设备树信息无效时终止初始化，没有可用完整页面时不调用 init_memmap。 */
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

    // 内核可管理的物理页号上界（物理内存末端对应的物理页号）
    npage = PPN(mem_end);
    if (npage <= nbase) {
        panic("page_init: no usable physical memory");
    }

    // Page 描述符数组紧跟在链接符号 end 之后，并按页对齐
    extern char end[];
    pages = (struct Page *)ROUNDUP((uintptr_t)end, PGSIZE);

    // 在空闲区建立之前，先把 [nbase, npage) 范围内的所有页都置为保留状态，
    // 防止尚未完成布局的页面被提前分配
    for (size_t i = 0; i < npage - nbase; i++) {
        SetPageReserved(pages + i);
    }

    // 描述符数组之后的地址向上按页对齐，得到第一个可分配物理页的虚拟地址
    uintptr_t freemem = ROUNDUP((uintptr_t)pages +
                                (npage - nbase) * sizeof(struct Page), PGSIZE);
    uintptr_t freemem_pa = PADDR(freemem);

    // 输出探测到的物理内存范围
    cprintf("physical memory map:\n");
    cprintf("memory: 0x%016lx, [0x%016lx, 0x%016lx].\n",
            mem_size, mem_begin, mem_end - 1);

    // 只有描述符数组之后、内存末端之前完整的页面才交给分配器，
    // 末尾不足一页的部分不会被加入空闲区
    size_t free_start = PPN(freemem_pa);
    if (free_start < npage) {
        init_memmap(pa2page(freemem_pa), npage - free_start);
    }
}

/* pmm_init - initialize the physical memory management */
void pmm_init(void) {
    // We need to alloc/free the physical memory (granularity is 4KB or other size).
    // So a framework of physical memory manager (struct pmm_manager)is defined in pmm.h
    // First we should init a physical memory manager(pmm) based on the framework.
    // Then pmm can alloc/free the physical memory.
    // Now the first_fit/best_fit/worst_fit/buddy_system pmm are available.
    init_pmm_manager();

    // detect physical memory space, reserve already used memory,
    // then use pmm->init_memmap to create free page list
    page_init();

    // use pmm->check to verify the correctness of the alloc/free function in a pmm
    check_alloc_page();

    extern char boot_page_table_sv39[];
    satp_virtual = (pte_t*)boot_page_table_sv39;
    satp_physical = PADDR(satp_virtual);
    cprintf("satp virtual address: 0x%016lx\nsatp physical address: 0x%016lx\n", satp_virtual, satp_physical);
}

static void check_alloc_page(void) {
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}
