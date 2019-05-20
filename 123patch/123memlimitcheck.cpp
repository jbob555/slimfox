#include <unistd.h>

int 123memlimitcheck() {
    struct* rusage memory = malloc(sizeof(struct rusage));
    getrusage(RUSAGE_SELF, memory);
    long usedmem = memory.ru_maxrss;
    if (usedmem >= 2000000)
        return 1;

    return 0;
}
