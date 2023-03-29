#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
    uint64 p_sz = memsize();
    printf("%d\n", p_sz);
    char * space = malloc(20000);
    p_sz = memsize();
    printf("%d\n", p_sz);
    free(space);
    p_sz = memsize();
    printf("%d\n", p_sz);
    exit(0,"");
}
