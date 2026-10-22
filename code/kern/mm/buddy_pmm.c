#include <pmm.h>
#include <list.h>
#include <string.h>
#include <stdio.h>
#include <buddy_pmm.h>

//lab2 function3
// Buddy System（伙伴系统）物理内存分配器。
//
// 核心思想：把可用内存划分为大小必须为 2 的幂（1, 2, 4, 8, ... 页）的存储块，
// 每个 order = k 的空闲块正好是「一对 order = k-1 的伙伴块」合并而来，伙伴关系
// 由块在分配区内的相对页号决定：块偏移 off、order k 的伙伴偏移为 off ^ (1 << k)。
//
// 与 First-Fit / Best-Fit 的关键区别：
//   - 分配：把请求页数 n 向上取整到 2^ceil(log2(n))，从足够大的空闲块逐级
//     「二分分裂」得到，分裂出的另一半作为伙伴块挂回对应阶的空闲链表。
//   - 回收：把块放回对应阶链表后，若其伙伴块（同一阶）也空闲，则循环向上合并。
//
// 内部状态：
//   buddy_free_list[k]  —— 阶为 k（块大小 2^k 页）的空闲块双向链表；
//   buddy_arena_base    —— 空闲区起点（第一个可分配物理页的 Page）；
//   buddy_arena_n       —— 空闲区总页数（init_memmap 传入的 n）；
//   buddy_nr_pages      —— 当前空闲页总数；
//   每个空闲块由首地址对应 Page 作为链表节点，property 保存块大小（2^k 页），
//   PageProperty 标记该 Page 为空闲块头（与 First-Fit/Best-Fit 约定一致）。

#define BUDDY_MAX_ORDER 20

static list_entry_t buddy_free_list[BUDDY_MAX_ORDER + 1];
static struct Page *buddy_arena_base = NULL;
static size_t buddy_arena_n = 0;
static size_t buddy_nr_pages = 0;

// buddy_rel - 返回 page 在空闲区内的相对页号（相对空闲区起点）
static size_t
buddy_rel(struct Page *page) {
    return (size_t)(page - buddy_arena_base);
}

// buddy_ceil_order - 返回不小于 n 的 2 的幂对应的阶（向上取整）
static size_t
buddy_ceil_order(size_t n) {
    assert(n > 0);
    size_t order = 0;
    while ((1UL << order) < n) {
        order++;
    }
    return order;
}

// buddy_floor_order - 返回不大于 n 的 2 的幂对应的阶（向下取整）
static size_t
buddy_floor_order(size_t n) {
    assert(n > 0);
    size_t order = 0;
    while ((1UL << (order + 1)) <= n) {
        order++;
    }
    return order;
}

static void
buddy_init(void) {
    for (size_t k = 0; k <= BUDDY_MAX_ORDER; k++) {
        list_init(&buddy_free_list[k]);
    }
    buddy_arena_base = NULL;
    buddy_arena_n = 0;
    buddy_nr_pages = 0;
}

// buddy_init_memmap - 把 [base, base+n) 登记为初始空闲区。
// 由于 n 不一定是 2 的幂，按「贪心取最大 2 的幂」分解成若干块挂到对应阶链表；
// 这种分解保证每块都相对空闲区起点按自身大小对齐，伙伴关系得以成立。
static void
buddy_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);
    struct Page *p = base;
    for (; p != base + n; p++) {
        assert(PageReserved(p));
        p->flags = p->property = 0;
        set_page_ref(p, 0);
    }
    buddy_arena_base = base;
    buddy_arena_n = n;
    buddy_nr_pages = n;

    struct Page *cur = base;
    size_t remaining = n;
    while (remaining > 0) {
        size_t k = buddy_floor_order(remaining);
        size_t size = 1UL << k;
        cur->property = size;
        SetPageProperty(cur);
        list_add(&buddy_free_list[k], &(cur->page_link));
        cur += size;
        remaining -= size;
    }
}

static struct Page *
buddy_alloc_pages(size_t n) {
    assert(n > 0);
    if (n > buddy_nr_pages) {
        return NULL;
    }
    size_t order = buddy_ceil_order(n);

    // 找到阶 >= order 的最小非空链表
    size_t k = order;
    while (k <= BUDDY_MAX_ORDER && list_empty(&buddy_free_list[k])) {
        k++;
    }
    if (k > BUDDY_MAX_ORDER) {
        return NULL;
    }

    // 从链表摘下该块，逐级二分分裂到目标阶
    list_entry_t *le = list_next(&buddy_free_list[k]);
    struct Page *block = le2page(le, page_link);
    list_del(le);
    while (k > order) {
        k--;
        struct Page *buddy = block + (1UL << k);
        buddy->property = 1UL << k;
        SetPageProperty(buddy);
        list_add(&buddy_free_list[k], &(buddy->page_link));
    }

    block->property = 0;
    ClearPageProperty(block);
    buddy_nr_pages -= (1UL << order);
    return block;
}

static void
buddy_free_pages(struct Page *base, size_t n) {
    assert(n > 0);
    size_t order = buddy_ceil_order(n);
    size_t size = 1UL << order;

    struct Page *p = base;
    for (; p != base + size; p++) {
        assert(!PageReserved(p) && !PageProperty(p));
        p->flags = 0;
        set_page_ref(p, 0);
    }

    size_t off = buddy_rel(base);
    assert(off + size <= buddy_arena_n);
    assert((off & (size - 1)) == 0);

    // 循环与伙伴块合并
    size_t k = order;
    while (k < BUDDY_MAX_ORDER) {
        size_t boff = off ^ (1UL << k);
        if (boff >= buddy_arena_n) {
            break;  // 伙伴块超出空闲区，无法再向上合并
        }
        struct Page *buddy = buddy_arena_base + boff;
        if (PageProperty(buddy) && buddy->property == (1UL << k)) {
            list_del(&(buddy->page_link));
            ClearPageProperty(buddy);
            buddy->property = 0;
            off = off & ~(1UL << k);
            base = buddy_arena_base + off;
            k++;
        } else {
            break;
        }
    }

    base->property = 1UL << k;
    SetPageProperty(base);
    list_add(&buddy_free_list[k], &(base->page_link));
    buddy_nr_pages += size;
}

static size_t
buddy_nr_free_pages(void) {
    return buddy_nr_pages;
}

// buddy_check - 检验 Buddy System 实现正确性的自测函数。
// 覆盖：单页分配/回收、2 的幂向上取整与对齐、伙伴分裂与合并、
//       超额分配返回 NULL、以及「每阶链表块大小一致 + 总量守恒」的结构不变量。
static void
buddy_check(void) {
    size_t initial = buddy_nr_pages;
    cprintf("buddy system check: arena [0x%016lx, 0x%016lx), %lu pages\n",
            (uintptr_t)buddy_arena_base, (uintptr_t)(buddy_arena_base + buddy_arena_n),
            (unsigned long)buddy_arena_n);

    // (1) 单页分配 / 回收往返
    struct Page *p0 = alloc_page();
    assert(p0 != NULL);
    assert(!PageReserved(p0) && !PageProperty(p0));
    assert(buddy_nr_pages == initial - 1);
    free_page(p0);
    assert(buddy_nr_pages == initial);

    // (2) 分配按 2 的幂向上取整，且返回块按该大小对齐
    size_t before = buddy_nr_pages;
    struct Page *q = alloc_pages(5);        // 5 -> 向上取整到 8 页
    assert(q != NULL);
    assert(buddy_nr_pages == before - 8);
    assert((buddy_rel(q) & 7) == 0);        // 8 页对齐
    free_pages(q, 5);
    assert(buddy_nr_pages == before);

    // (3) 伙伴分裂与合并：分配 4 页，再分两次各释放 2 页
    struct Page *p1 = alloc_pages(4);
    assert(p1 != NULL);
    assert((buddy_rel(p1) & 3) == 0);       // 4 页对齐
    free_pages(p1, 2);                       // 释放低半块；其伙伴(p1+2)仍被占用，
                                             // 故不得提前合并，应仍是 2 页块
    assert(PageProperty(p1) && p1->property == 2);
    free_pages(p1 + 2, 2);                   // 释放高半块；两半是伙伴，必须合并
    assert(!PageProperty(p1) || p1->property >= 4);  // p1 不再是 2 页块
    assert(buddy_nr_pages == initial);       // 全部归还

    // (4) 超额分配应失败
    assert(alloc_pages(buddy_nr_pages + 1) == NULL);
    assert(alloc_pages(buddy_nr_pages + 1024) == NULL);

    // (5) 结构不变量：第 k 阶链表中的每块大小均为 2^k，且总量守恒
    size_t total = 0;
    for (size_t k = 0; k <= BUDDY_MAX_ORDER; k++) {
        list_entry_t *le = &buddy_free_list[k];
        while ((le = list_next(le)) != &buddy_free_list[k]) {
            struct Page *p = le2page(le, page_link);
            assert(PageProperty(p));
            assert(p->property == (1UL << k));
            total += p->property;
        }
    }
    assert(total == buddy_nr_pages);
    assert(buddy_nr_pages == initial);

    cprintf("check_buddy() succeeded!\n");
}

const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};
