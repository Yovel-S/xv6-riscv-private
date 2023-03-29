#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
    char hello_world[17] = "Hello World xv6\n";
    write(1, hello_world, strlen(hello_world));
    exit(0,"");
}
