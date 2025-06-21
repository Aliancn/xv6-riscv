// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);
void buddy_system_init(void *start);
void print_buddy_system_state();
extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run
{
    struct run *next;
};

struct
{
    struct spinlock lock;
    struct run *freelist;
} kmem;

void kinit()
{
    initlock(&kmem.lock, "kmem");
    freerange(end, (void *)MYPHYSTART);
    buddy_system_init((void *)MYPHYSTART);
}

void freerange(void *pa_start, void *pa_end)
{
    char *p;
    p = (char *)PGROUNDUP((uint64)pa_start);
    for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
        kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa)
{
    struct run *r;

    if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
        panic("kfree");

    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    r = (struct run *)pa;

    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
    struct run *r;

    acquire(&kmem.lock);
    r = kmem.freelist;
    if (r)
        kmem.freelist = r->next;
    release(&kmem.lock);

    if (r)
        memset((char *)r, 5, PGSIZE); // fill with junk
    return (void *)r;
}
#define PAGE_SIZE (1 << 12)             // 系统页大小4k = 2^12
#define MIN_BLOCK_SIZE 16               // 最小块16字节 (2^4)
#define TOTAL_MEMORY (16 * 1024 * 1024) // 总内存16MB (2^24)
#define MAX_ORDER (24 - 4 + 1)          // 最大阶数21 (2^24 = 16MB)
#define NULL ((void *)0)
#define size_t uint64
#define MAX_ALLOCATIONS 1024 * 1024 * 4 // 最大分配块数
struct allocation_info
{
    void *addr;  // 分配块的地址
    uint64 size; // 分配块的大小
};
struct allocation_info allocation_table[MAX_ALLOCATIONS];
struct spinlock allocation_lock; // 保护分配表的锁


void init_allocation_table()
{
    for (int i = 0; i < MAX_ALLOCATIONS; i++)
    {
        allocation_table[i].addr = NULL;
        allocation_table[i].size = 0;
    }
}
void save_allocation_info(void *addr, uint64 size)
{
    acquire(&allocation_lock); // 获取锁，防止并发修改
    for (int i = 0; i < MAX_ALLOCATIONS; i++)
    {
        if (allocation_table[i].addr == NULL)
        {
            allocation_table[i].addr = addr;
            allocation_table[i].size = size;
            release(&allocation_lock); // 释放锁
            return;
        }
    }
    release(&allocation_lock); // 释放锁
    panic("save_allocation_info: allocation table full");
}
void remove_allocation_info(void *addr)
{
    acquire(&allocation_lock); // 获取锁，防止并发修改
    for (int i = 0; i < MAX_ALLOCATIONS; i++)
    {
        if (allocation_table[i].addr == addr)
        {
            allocation_table[i].addr = NULL;
            allocation_table[i].size = 0;
            release(&allocation_lock); // 释放锁
            return;
        }
    }
    release(&allocation_lock); // 释放锁
    panic("remove_allocation_info: address not found");
}
uint64 get_allocation_size(void *addr)
{
    acquire(&allocation_lock); // 获取锁，防止并发修改
    for (int i = 0; i < MAX_ALLOCATIONS; i++)
    {
        if (allocation_table[i].addr == addr)
        {
            release(&allocation_lock); 
            return allocation_table[i].size;
        }
    }
    release(&allocation_lock); // 释放锁
    return 0; // 未找到
}
// 空闲块链表
struct free_block
{
    struct free_block *next; // 下一个空闲块
};

// 全局伙伴系统
struct buddy_system
{
    struct free_block *free_list[MAX_ORDER]; // 空闲块链表
    struct spinlock lock;                    // 保护空闲链表的锁
    uint64 start;                            // 伙伴系统起始地址
};

// 全局伙伴系统变量
struct buddy_system bsystem;

// 初始化伙伴系统
void buddy_system_init(void *start)
{
    // 初始化锁
    initlock(&bsystem.lock, "buddy_system");

    bsystem.start = PGROUNDUP((uint64)start); // 设置起始地址

    // 初始化所有空闲链表为空
    for (int i = 0; i < MAX_ORDER; i++)
    {
        bsystem.free_list[i] = NULL;
    }

    init_allocation_table(); // 初始化分配表
    int order = MAX_ORDER - 1;

    // 将整个内存块划分为最大块并加入空闲链表
    struct free_block *block = (struct free_block *)(start);
    block->next = bsystem.free_list[order];
    bsystem.free_list[order] = block;

}

// 计算请求大小对应的最小阶数
static int size_to_order(size_t size)
{
    if (size < MIN_BLOCK_SIZE)
    {
        size = MIN_BLOCK_SIZE;
    }

    // 向上取整到最近的2的幂次
    size_t rounded_size = MIN_BLOCK_SIZE;
    int order = 0;

    while (rounded_size < size && order < MAX_ORDER)
    {
        rounded_size <<= 1;
        order++;
    }

    return order;
}

// 从伙伴系统中分配内存
void *buddy_alloc(uint64 size)
{
    // 获取空闲块链表的锁
    acquire(&bsystem.lock);

    // 计算需要的阶数
    int order = size_to_order(size);
    if (order > MAX_ORDER)
    {
        release(&bsystem.lock);
        return NULL; // 请求过大
    }

    // 从当前阶开始向上查找可用块
    int current_order = order;
    struct free_block *block = NULL;
    while (current_order < MAX_ORDER)
    {
        if (bsystem.free_list[current_order] != NULL)
        {
            // 找到可用块
            block = bsystem.free_list[current_order];
            bsystem.free_list[current_order] = block->next;
            break;
        }
        current_order++;
    }
    if (current_order == MAX_ORDER)
    {
        // 没有找到可用块
        printf("No free block available for order %d\n", order);
    }
    // 如果没有找到，返回NULL
    if (block == NULL)
    {
        release(&bsystem.lock);
        return NULL;
    }

    // 如果找到的块比需要的大，进行分割
    while (current_order > order)
    {
        current_order--;

        // 分割块为两个伙伴
        struct free_block *buddy = (struct free_block *)((uint64)block + (MIN_BLOCK_SIZE * (1 << current_order)));

        // 将伙伴加入低一阶的空闲链表
        buddy->next = bsystem.free_list[current_order];
        bsystem.free_list[current_order] = buddy;
    }
    // 释放锁
    release(&bsystem.lock);
    // 返回分配的内存块
    return block;
}
// 计算给定地址的伙伴块地址
static void *get_buddy_address(void *addr, int order)
{
    uint64 offset = (uint64)addr - bsystem.start;
    uint64 buddy_offset = offset ^ (MIN_BLOCK_SIZE * (1 << order));
    return (void *)(bsystem.start + buddy_offset);
}
// 检查块是否在空闲链表中
static int is_block_free(void *addr, int order)
{
    struct free_block *curr = bsystem.free_list[order];
    while (curr != NULL)
    {
        if (curr == addr)
        {
            return 1;
        }
        curr = curr->next;
    }
    return 0;
}
// 从空闲链表中移除指定块
static void remove_from_free_list(void *addr, int order)
{
    struct free_block **curr = &bsystem.free_list[order];
    while (*curr != NULL)
    {
        if (*curr == addr)
        {
            *curr = (*curr)->next;
            return;
        }
        curr = &(*curr)->next;
    }
}
// 释放内存
void buddy_free(void *ptr, uint64 size)
{
    if (ptr == NULL || size == 0)
    {
        return;
    }
    acquire(&bsystem.lock);

    // 计算块的阶数
    int order = size_to_order(size);
    if (order > MAX_ORDER)
    {
        release(&bsystem.lock);
        return; // 非法大小
    }
    // 检查指针是否对齐
    uint64 offset = (uint64)ptr - bsystem.start;
    if (offset % (MIN_BLOCK_SIZE * (1 << order)))
    {
        release(&bsystem.lock);
        panic("buddy_free: unaligned pointer");
    }
    // 3. 尝试合并块
    int current_order = order;
    void *current_block = ptr;

    while (current_order < MAX_ORDER - 1)
    {
        // 计算伙伴块地址
        void *buddy = get_buddy_address(current_block, current_order);

        // 检查伙伴是否空闲
        if (!is_block_free(buddy, current_order))
        {
            break;
        }

        // 从空闲链表移除伙伴
        remove_from_free_list(buddy, current_order);

        // 合并两个块(取地址较小的作为合并后的块)
        if (current_block > buddy)
        {
            current_block = buddy;
        }

        current_order++;
    }

    // 4. 将合并后的块加入空闲链表
    struct free_block *block = (struct free_block *)current_block;
    block->next = bsystem.free_list[current_order];
    bsystem.free_list[current_order] = block;

    // 释放锁
    release(&bsystem.lock);
}

void *
kalloc_buddy(uint64 size)
{
    // 检查请求的大小是否有效
    if (size == 0 || size > TOTAL_MEMORY)
    {
        return NULL; // 请求大小无效
    }

    // 调用伙伴系统的分配函数
    void *block = buddy_alloc(size);
    if (block == NULL)
    {
        return NULL; // 分配失败
    }

    save_allocation_info(block, size);
    print_buddy_system_state();

    // 返回分配的内存块
    return block;
}

void kfree_buddy(void *ptr)
{
    // 检查指针和大小是否有效
    if (ptr == NULL)
    {
        return; // 无效的释放请求
    }
    uint64 bsize = get_allocation_size(ptr); // 检查分配信息
    // 调用伙伴系统的释放函数
    buddy_free(ptr, bsize);
    remove_allocation_info(ptr); // 删除分配信息
}

void print_buddy_system_state()
{
    acquire(&bsystem.lock); // 获取锁，防止并发修改

    printf("Buddy System State:\n");
    printf("Start Address: %p\n", (void *)bsystem.start);
    printf("-------------------------------------------------\n");
    printf("Order | Block Size | Free Blocks (Addresses)\n");
    printf("-------------------------------------------------\n");

    for (int order = 0; order < MAX_ORDER; order++)
    {
        struct free_block *curr = bsystem.free_list[order];
        int count = 0;

        // 计算当前阶的块大小
        uint64 block_size = MIN_BLOCK_SIZE * (1 << order);

        // 遍历当前阶的空闲链表
        printf("%d | %lu | ", order, block_size);
        while (curr != NULL)
        {
            printf("%p -> ", curr);
            curr = curr->next;
            count++;
        }

        // 打印空闲块数量
        if (count == 0)
        {
            printf("None");
        }
        printf("\n");
    }

    printf("-------------------------------------------------\n");

    release(&bsystem.lock); // 释放锁
}

void test_buddy_system()
{
    printf("\n=== Buddy System Test ===\n");

    // 1. 初始化测试变量
    void *block1, *block2, *block3, *block4;

    // 2. 测试分配最小块
    printf("\n[TEST] 分配最小块大小...\n");
    block1 = kalloc_buddy(MIN_BLOCK_SIZE);
    if (block1 == NULL)
    {
        printf("FAILED: Unable to allocate minimum block size.\n");
        return;
    }
    printf("Allocated block1 at %p, size = %lu\n", block1, get_allocation_size(block1));

    // 3. 测试分配页大小块
    printf("\n[TEST] 分配页大小块...\n");
    block2 = kalloc_buddy(PGSIZE);
    if (block2 == NULL)
    {
        printf("FAILED: Unable to allocate page size block.\n");
        return;
    }
    printf("Allocated block2 at %p, size = %lu\n", block2, get_allocation_size(block2));

    // 4. 测试分配大块
    printf("\n[TEST] 分配大于页大小块 (2 * PGSIZE)...\n");
    block3 = kalloc_buddy(2 * PGSIZE);
    if (block3 == NULL)
    {
        printf("FAILED: Unable to allocate large block.\n");
        return;
    }
    printf("Allocated block3 at %p, size = %lu\n", block3, get_allocation_size(block3));

    // 5. 测试释放块并检查合并
    printf("\n[TEST] 释放分配的块并检查合并...\n");
    kfree_buddy(block1);
    printf("Freed block1 at %p\n", block1);
    kfree_buddy(block2);
    printf("Freed block2 at %p\n", block2);
    kfree_buddy(block3);
    printf("Freed block3 at %p\n", block3);

    // 打印伙伴系统状态，检查是否合并
    print_buddy_system_state();

    // 6. 测试分配超大块
    printf("\n[TEST] 测试分配超出最大块限制的块...\n");
    block4 = kalloc_buddy(TOTAL_MEMORY + 1);
    if (block4 == NULL)
    {
        printf("PASSED: Oversized block allocation correctly failed.\n");
    }
    else
    {
        printf("FAILED: Oversized block allocation should not succeed.\n");
        return;
    }

    // 打印最终状态
    print_buddy_system_state();

    printf("\n=== Buddy System Test Completed ===\n");
}