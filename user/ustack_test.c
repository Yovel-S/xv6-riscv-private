#include "ustack.h"

int main()
{
    int len;
    
    len = ustack_free();
    printf("len: %d\n", len);
    
    void *ptr1 = ustack_malloc(100);
    printf("ptr1: %p\n", ptr1);

    void *ptr2 = ustack_malloc(200);
    printf("ptr2: %p\n", ptr2);

    void *ptr3 = ustack_malloc(300);
    printf("ptr3: %p\n", ptr3);

    //ustack_free();

    len = ustack_free();
    printf("len: %d\n", len);

    len = ustack_free();
    printf("len: %d\n", len);

    len = ustack_free();
    printf("len: %d\n", len);
    
    len = ustack_free();
    printf("len: %d\n", len);

    return 0;
}
