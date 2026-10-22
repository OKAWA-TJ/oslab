#include <defs.h>
#include <pmm.h>
#include <list.h>
#include <string.h>
#include <stdio.h>
#include <slub.h>

//lab2 challenge
// SLUB 分配器（简化实现）：体现「两层架构」的主体思想。
//   第一层（页分配）：复用 pmm_manager 的 alloc_pages / free_pages 获取/归还整页；
//   第二层（对象分配）：把整页（slab）切割成固定大小的对象，空闲对象通过
//                     内嵌在对象首部的指针串成 freelist，实现 O(1) 的分配/释放。
//
// 主要机制：
//   - 按 size class（16~2048 的 2 的幂）聚合同大小对象为 cache；
//   - 每个 cache 维护 partial（部分空闲）与 full（已满）两个 slab 链表；
//   - 分配：从 partial 头 slab 的 freelist 弹一个对象；无 partial 则再 alloc_pages(1)
//           新建一个 slab 并切分；弹空后把 slab 移入 full；
//   - 释放：把对象压回 freelist；slab 由满变部分空闲时 full->partial；
//           slab 空后整页 free_pages 还给页分配器；
//   - 大于 2048 的请求走 large 路径：直接整页分配，页首放一个 cache==NULL 的
//           slub_slab 头（npages 记页数），释放时据此整页归还。

#define SLUB_SIZES_NUM 8
#define SLUB_MAX_OBJ   2048

struct slub_cache;

// 每个 slab（一页）的头部描述，位于页首；对象区紧随其后。
struct slub_slab {
    list_entry_t link;          // 挂在 cache 的 partial/full 链表上
    struct slub_cache *cache;   // 所属 cache；NULL 表示 large 对象页
    void *freelist;             // 空闲对象链表头
    unsigned int inuse;         // 已用对象数
    unsigned int objs_per_slab; // 本页可容纳对象数
    size_t npages;              // large 对象占用页数（小对象 slab 恒为 1）
};

struct slub_cache {
    size_t obj_size;            // 对象大小（字节）
    list_entry_t partial;       // 部分空闲的 slab 链表
    list_entry_t full;          // 已满的 slab 链表
    unsigned int nr_slabs;      // 当前持有的 slab 数
    unsigned int nr_active;     // 当前在用对象数
    const char *name;
};

#define le2slab(le) to_struct((le), struct slub_slab, link)

static const size_t slub_sizes[SLUB_SIZES_NUM] =
    {16, 32, 64, 128, 256, 512, 1024, 2048};
static struct slub_cache slub_caches[SLUB_SIZES_NUM];

// page2kva - 由 Page 描述符得到对应内核虚拟地址（页首）
static void *
page2kva(struct Page *page) {
    return (void *)(page2pa(page) + va_pa_offset);
}

void
slub_init(void) {
    for (int i = 0; i < SLUB_SIZES_NUM; i++) {
        struct slub_cache *c = &slub_caches[i];
        c->obj_size = slub_sizes[i];
        list_init(&c->partial);
        list_init(&c->full);
        c->nr_slabs = 0;
        c->nr_active = 0;
        c->name = "slub";
    }
}

// slub_find_cache - 返回「obj_size >= size」的最小 size class 对应 cache
static struct slub_cache *
slub_find_cache(size_t size) {
    for (int i = 0; i < SLUB_SIZES_NUM; i++) {
        if (slub_sizes[i] >= size) {
            return &slub_caches[i];
        }
    }
    return NULL;
}

// slub_slab_init - 把一个新页初始化为 slab，并把所有对象串进 freelist
static void
slub_slab_init(struct slub_slab *s, struct slub_cache *c) {
    s->cache = c;
    s->npages = 1;
    s->inuse = 0;
    // 对象区起点按对象大小对齐，保证每个对象满足其 size class 的对齐要求
    char *start = (char *)ROUNDUP((uintptr_t)s + sizeof(struct slub_slab),
                                  c->obj_size);
    s->objs_per_slab = (PGSIZE - (size_t)(start - (char *)s)) / c->obj_size;
    s->freelist = NULL;
    for (unsigned int i = 0; i < s->objs_per_slab; i++) {
        void *obj = start + i * c->obj_size;
        *(void **)obj = s->freelist;
        s->freelist = obj;
    }
}

// slub_cache_alloc - 从指定 cache 分配一个对象
static void *
slub_cache_alloc(struct slub_cache *c) {
    struct slub_slab *s;
    if (!list_empty(&c->partial)) {
        s = le2slab(list_next(&c->partial));
    } else {
        struct Page *page = alloc_pages(1);
        if (page == NULL) {
            return NULL;
        }
        s = (struct slub_slab *)page2kva(page);
        slub_slab_init(s, c);
        list_add(&c->partial, &s->link);
        c->nr_slabs++;
    }

    assert(s->freelist != NULL);
    void *obj = s->freelist;
    s->freelist = *(void **)obj;
    s->inuse++;
    c->nr_active++;
    if (s->inuse == s->objs_per_slab) {
        // 弹空：从 partial 移到 full
        list_del(&s->link);
        list_add(&c->full, &s->link);
    }
    return obj;
}

// slub_cache_free - 把对象归还给所属 slab，必要时把空 slab 还给页分配器
static void
slub_cache_free(struct slub_cache *c, struct slub_slab *s, void *obj) {
    *(void **)obj = s->freelist;
    s->freelist = obj;
    s->inuse--;
    c->nr_active--;
    if (s->inuse == 0) {
        // slab 已空：整页还给页分配器
        list_del(&s->link);
        c->nr_slabs--;
        free_pages(pa2page(PADDR((uintptr_t)s)), 1);
    } else if (s->inuse == s->objs_per_slab - 1) {
        // 由满变部分空闲：full -> partial
        list_del(&s->link);
        list_add(&c->partial, &s->link);
    }
}

void *
slub_malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }
    size = ROUNDUP(size, sizeof(void *));
    if (size > SLUB_MAX_OBJ) {
        // large 路径：直接整页分配，页首放头（cache==NULL 标记）
        size_t need = size + sizeof(struct slub_slab);
        size_t npages = ROUNDUP(need, PGSIZE) / PGSIZE;
        struct Page *page = alloc_pages(npages);
        if (page == NULL) {
            return NULL;
        }
        struct slub_slab *s = (struct slub_slab *)page2kva(page);
        s->cache = NULL;
        s->npages = npages;
        s->inuse = 1;
        return (void *)((char *)s + sizeof(struct slub_slab));
    }
    return slub_cache_alloc(slub_find_cache(size));
}

void
slub_free(void *ptr) {
    if (ptr == NULL) {
        return;
    }
    struct slub_slab *s = (struct slub_slab *)ROUNDDOWN((uintptr_t)ptr, PGSIZE);
    if (s->cache == NULL) {
        // large 对象页：整页归还
        free_pages(pa2page(PADDR((uintptr_t)s)), s->npages);
    } else {
        slub_cache_free(s->cache, s, ptr);
    }
}

size_t
slub_nr_active(void) {
    size_t total = 0;
    for (int i = 0; i < SLUB_SIZES_NUM; i++) {
        total += slub_caches[i].nr_active;
    }
    return total;
}

// slub_check - 检验 SLUB 实现正确性的自测函数。
// 覆盖：各 size class 的分配/释放、8 字节对齐、数据可写、对象不重叠、
//       释放后 freelist 复用、大对象整页路径、混合分配/释放后无泄漏。
void
slub_check(void) {
    cprintf("slub check start\n");

    // (1) 各 size class 的分配/释放/可写/对齐
    size_t sizes[8] = {8, 16, 24, 64, 100, 512, 1024, 2048};
    for (int i = 0; i < 8; i++) {
        void *p = slub_malloc(sizes[i]);
        assert(p != NULL);
        assert(((uintptr_t)p & 7) == 0);
        *(uint64_t *)p = 0xdeadbeefcafebabeULL;
        assert(*(uint64_t *)p == 0xdeadbeefcafebabeULL);
        slub_free(p);
    }
    assert(slub_nr_active() == 0);

    // (2) 同 cache 对象不重叠，释放后 freelist 复用同一对象
    void *a[16];
    for (int i = 0; i < 16; i++) {
        a[i] = slub_malloc(64);
        assert(a[i] != NULL);
        for (int j = 0; j < i; j++) {
            assert(a[i] != a[j]);
        }
    }
    void *reuse = a[0];
    slub_free(a[0]);
    void *b = slub_malloc(64);
    assert(b == reuse);   // LIFO 复用刚释放的对象
    for (int i = 1; i < 16; i++) {
        slub_free(a[i]);
    }
    slub_free(b);
    assert(slub_nr_active() == 0);

    // (3) 大对象（> 2048，走直接整页分配路径）
    void *big1 = slub_malloc(4096);
    void *big2 = slub_malloc(8192);
    assert(big1 != NULL && big2 != NULL && big1 != big2);
    *(uint64_t *)big1 = 0x1122334455667788ULL;
    *(uint64_t *)big2 = 0x8877665544332211ULL;
    assert(*(uint64_t *)big1 == 0x1122334455667788ULL);
    assert(*(uint64_t *)big2 == 0x8877665544332211ULL);
    slub_free(big1);
    slub_free(big2);
    assert(slub_nr_active() == 0);

    // (4) 混合压力：不同大小随机分配，分两批释放，结束后无泄漏
    void *mix[32];
    for (int i = 0; i < 32; i++) {
        size_t sz = ((size_t)(i * 37) % 512) + 1;
        mix[i] = slub_malloc(sz);
        assert(mix[i] != NULL);
    }
    for (int i = 0; i < 32; i += 2) {
        slub_free(mix[i]);
    }
    for (int i = 1; i < 32; i += 2) {
        slub_free(mix[i]);
    }
    assert(slub_nr_active() == 0);

    cprintf("check_slub() succeeded!\n");
}
