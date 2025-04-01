#include "kernel/types.h"
#include "user/user.h"

void test_single_process() {
    printf("Test 1: Single process\n");
    int procnum = getprocnum();
    printf("Number of processes: %d \n", procnum);
}

void test_multiple_processes() {
    printf("Test 2: Multiple processes\n");
    int pid1 = fork();
    if (pid1 == 0) {
        int procnum = getprocnum();
        printf("Child process: Number of processes: %d\n", procnum);
        exit(0);
    } else {
        wait(0);
        int procnum = getprocnum();
        printf("Parent process: Number of processes: %d\n", procnum);
    }
}


int main(int argc, char *argv[]) {
    test_single_process();
    test_multiple_processes();
    exit(0);
}