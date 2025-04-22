#include "kernel/types.h"
#include "user/user.h"

void test_basic_allocation() {
    printf("\n=== Testing Basic Allocation ===\n");
    
    // Get initial break
    char* initial_brk = sbrk_buddy(0);
    printf("Initial program break: 0x%lx\n", (uint64)initial_brk);

    // Test small allocation
    printf("Allocating 64 bytes...\n");
    char* small_alloc = sbrk_buddy(64);
    if ((uint64)small_alloc == (uint64)-1) {
        printf("Failed to allocate 64 bytes\n");
        exit(1);
    }
    printf("Allocated at: 0x%lx, new break: 0x%lx\n", 
           (uint64)small_alloc, (uint64)sbrk_buddy(0));

    // Test medium allocation
    printf("Allocating 256 bytes...\n");
    char* medium_alloc = sbrk_buddy(256);
    if ((uint64)medium_alloc == (uint64)-1) {
        printf("Failed to allocate 256 bytes\n");
        exit(1);
    }
    printf("Allocated at: 0x%lx, new break: 0x%lx\n", 
           (uint64)medium_alloc, (uint64)sbrk_buddy(0));

    // Test large allocation
    printf("Allocating 2048 bytes...\n");
    char* large_alloc = sbrk_buddy(2048);
    if ((uint64)large_alloc == (uint64)-1) {
        printf("Failed to allocate 2048 bytes\n");
        exit(1);
    }
    printf("Allocated at: 0x%lx, new break: 0x%lx\n", 
           (uint64)large_alloc, (uint64)sbrk_buddy(0));

    printf("small_alloc: 0x%lx, medium_alloc: 0x%lx, large_alloc: 0x%lx\n", 
           (uint64)small_alloc, (uint64)medium_alloc, (uint64)large_alloc);     

    // Verify allocations don't overlap
    if (small_alloc + 64 > medium_alloc || medium_alloc + 256 > large_alloc) {
        printf("Allocation overlap detected!\n");
        exit(1);
    }
}

void test_data_integrity() {
    printf("\n=== Testing Data Integrity ===\n");
    
    // Allocate and write data
    printf("Allocating 128 bytes...\n");
    char* alloc = sbrk_buddy(128);
    if ((uint64)alloc == (uint64)-1) {
        printf("Failed to allocate 128 bytes\n");
        exit(1);
    }

    printf("Writing pattern to memory...\n");
    for (int i = 0; i < 128; i++) {
        alloc[i] = (char)(i % 256);
    }

    printf("Verifying pattern...\n");
    for (int i = 0; i < 128; i++) {
        if (alloc[i] != (char)(i % 256)) {
            printf("Data corruption at offset %d: expected %d, got %d\n", 
                   i, (char)(i % 256), alloc[i]);
            exit(1);
        }
    }
    printf("Data integrity verified!\n");
}

void test_boundary_cases() {
    printf("\n=== Testing Boundary Cases ===\n");
    
    // Test allocation of 0 bytes
    printf("Testing sbrk_buddy(0)...\n");
    char* brk1 = sbrk_buddy(0);
    char* brk2 = sbrk_buddy(0);
    if (brk1 != brk2) {
        printf("sbrk_buddy(0) returned different values: 0x%lx vs 0x%lx\n", 
               (uint64)brk1, (uint64)brk2);
        exit(1);
    }

    // Test negative allocation (deallocation)
    printf("Allocating 512 bytes to test deallocation...\n");
    char* alloc = sbrk_buddy(512);
    if ((uint64)alloc == (uint64)-1) {
        printf("Failed to allocate 512 bytes\n");
        exit(1);
    }
    
    printf("Current break: 0x%lx\n", (uint64)sbrk_buddy(0));
    
    printf("Freeing 256 bytes...\n");
    if ((uint64)sbrk_buddy(-256) == (uint64)-1) {
        printf("Failed to free 256 bytes\n");
        exit(1);
    }
    printf("New break: 0x%lx\n", (uint64)sbrk_buddy(0));
    
    printf("Freeing remaining 256 bytes...\n");
    if ((uint64)sbrk_buddy(-256) == (uint64)-1) {
        printf("Failed to free 256 bytes\n");
        exit(1);
    }
    printf("Final break: 0x%lx\n", (uint64)sbrk_buddy(0));
}

void test_multiple_allocations() {
    printf("\n=== Testing Multiple Allocations ===\n");
    
    char* initial_brk = sbrk_buddy(0);
    printf("Initial break: 0x%lx\n", (uint64)initial_brk);
    
    // Allocate multiple blocks
    char* allocs[10];
    for (int i = 0; i < 10; i++) {
        int size = 64 * (i + 1);
        printf("Allocating %d bytes...\n", size);
        allocs[i] = sbrk_buddy(size);
        if ((uint64)allocs[i] == (uint64)-1) {
            printf("Failed to allocate %d bytes\n", size);
            exit(1);
        }
        
        // Write data
        for (int j = 0; j < size; j++) {
            allocs[i][j] = (char)((i + j) % 256);
        }
    }
    
    // Verify data
    printf("Verifying allocations...\n");
    for (int i = 0; i < 10; i++) {
        int size = 64 * (i + 1);
        for (int j = 0; j < size; j++) {
            if (allocs[i][j] != (char)((i + j) % 256)) {
                printf("Data corruption in allocation %d at offset %d\n", i, j);
                exit(1);
            }
        }
    }
    
    // Free all allocations
    printf("Freeing all allocations...\n");
    for (int i = 9; i >= 0; i--) {
        int size = 64 * (i + 1);
        printf("Freeing %d bytes...\n", size);
        if ((uint64)sbrk_buddy(-size) == (uint64)-1) {
            printf("Failed to free %d bytes\n", size);
            exit(1);
        }
    }
    
    printf("Final break: 0x%lx\n", (uint64)sbrk_buddy(0));
    if (sbrk_buddy(0) != initial_brk) {
        printf("Memory leak detected! Break didn't return to initial position\n");
        exit(1);
    }
}

int main(void) {
    printf("Starting sbrk_buddy tests...\n");
    
    test_basic_allocation();
    test_data_integrity();
    test_boundary_cases();
    test_multiple_allocations();
    
    printf("\nAll tests passed successfully!\n");
    exit(0);
}