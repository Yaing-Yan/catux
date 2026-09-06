/*
 * heap.c —— 内核堆：kmalloc / kfree（阶段 3c）
 *
 * 物理分配器最小只发 4KB 的帧——要个 64 字节的链表节点也发一帧，
 * 太浪费。堆在虚拟地址空间划出一块地盘（0xC0000000 起），把页帧
 * "铺"进去，再做细粒度切分。三层结构连起来看：
 *
 *   物理层（3a） alloc_frame      —— 一次给 4KB
 *   分页层（3b） paging_map_page  —— 把帧铺到任意虚拟地址
 *   堆层 （3c） kmalloc/kfree     —— 想要多少给多少
 *
 * 分页的价值在这里兑现：堆的虚拟地盘是"连续"的，但铺进去的
 * 物理页帧可以东一块西一块——虚拟连续 ≠ 物理连续。
 *
 * 算法：first-fit 空闲链表。每块数据前面挂 16 字节的头
 * （大小/占用标志/前后邻居），所有块按地址顺序串成双向链表。
 *   kmalloc：从链表头找第一个够大的空闲块，太大就劈开两半
 *   kfree  ：标记空闲，然后和相邻的空闲块合并（防止碎成渣）
 */

#include <stdint.h>

#include "console.h"
#include "mem.h"
#include "paging.h"
#include "heap.h"

#define HEAP_START 0xC0000000u
#define HEAP_LIMIT (HEAP_START + 16u * 1024 * 1024)   /* 堆上限 16MB 虚拟 */

struct block {
    uint32_t size;          /* 数据区大小（不含头） */
    uint8_t  used;
    struct block *next;     /* 双向链表，按地址序 */
    struct block *prev;
};
#define HDR ((uint32_t)sizeof(struct block))   /* 16 字节 → 数据区 16 对齐 */

static struct block *head, *tail;
static uint32_t heap_end;                     /* 已铺页的虚拟终点 */
static uint32_t used_bytes, free_bytes;

/* 把堆向前扩 bytes 字节（向上取整到页），新地盘整体做成一个空闲块挂链尾 */
static int heap_grow(uint32_t bytes)
{
    bytes = (bytes + 4095) & ~4095u;
    if (heap_end + bytes > HEAP_LIMIT || heap_end + bytes < HEAP_START)
        return 0;

    for (uint32_t v = heap_end; v < heap_end + bytes; v += 4096) {
        uint32_t phys = alloc_frame();
        if (!phys)
            return 0;
        paging_map_page(v, phys, PAGE_RW);
    }

    struct block *b = (struct block *)heap_end;
    b->size = bytes - HDR;
    b->used = 0;
    b->prev = tail;
    b->next = 0;
    if (tail)
        tail->next = b;
    else
        head = b;
    tail = b;
    free_bytes += b->size;
    heap_end += bytes;
    return 1;
}

/* 块太大时劈成两半：前半正好 size，后半成为新的空闲块 */
static void block_split(struct block *b, uint32_t size)
{
    if (b->size < size + HDR + 16)     /* 剩余不够独立成块：不劈 */
        return;

    struct block *r = (struct block *)((uint8_t *)b + HDR + size);
    r->size = b->size - size - HDR;
    r->used = 0;
    r->prev = b;
    r->next = b->next;
    if (r->next)
        r->next->prev = r;
    else
        tail = r;
    b->next = r;
    b->size = size;
    free_bytes -= HDR;                 /* 多了一个头，可分配空间缩水 */
}

void *kmalloc(uint32_t size)
{
    if (size == 0)
        size = 1;
    size = (size + 15) & ~15u;         /* 16 对齐 */

    for (struct block *b = head; b; b = b->next) {
        if (!b->used && b->size >= size) {
            block_split(b, size);
            b->used = 1;
            used_bytes += b->size;
            free_bytes -= b->size;
            return (uint8_t *)b + HDR;
        }
    }

    /* 现有块都不够 → 铺新地盘。新块必在链尾且够大 */
    if (!heap_grow(size + HDR))
        return 0;

    struct block *b = tail;
    block_split(b, size);
    b->used = 1;
    used_bytes += b->size;
    free_bytes -= b->size;
    return (uint8_t *)b + HDR;
}

void kfree(void *ptr)
{
    if (!ptr)
        return;

    struct block *b = (struct block *)((uint8_t *)ptr - HDR);
    b->used = 0;
    used_bytes -= b->size;
    free_bytes += b->size;

    /* 合并后一个空闲块：吞掉它的头和数据 */
    if (b->next && !b->next->used) {
        struct block *n = b->next;
        b->size += HDR + n->size;
        b->next = n->next;
        if (n->next)
            n->next->prev = b;
        else
            tail = b;
        free_bytes += HDR;
    }
    /* 合并前一个空闲块 */
    if (b->prev && !b->prev->used) {
        struct block *p = b->prev;
        p->size += HDR + b->size;
        p->next = b->next;
        if (b->next)
            b->next->prev = p;
        else
            tail = p;
        free_bytes += HDR;
    }
}

uint32_t kheap_used(void) { return used_bytes; }
uint32_t kheap_free(void) { return free_bytes; }

void heap_init(void)
{
    heap_end = HEAP_START;   /* 地盘为零，第一次 kmalloc 时现场铺页 */
    head = tail = 0;
    used_bytes = free_bytes = 0;
}
