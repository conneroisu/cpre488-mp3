#include "launcher-commands.h"
#include <unistd.h>
#include <stdio.h>
#include <sys/mman.h>

int main()
{
    printf("Page Size: %ld\n", sysconf(_SC_PAGE_SIZE));
    return 0;
}