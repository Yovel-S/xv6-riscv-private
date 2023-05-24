#include "ustack.h"

#define MAX_ALLOCATION_SIZE 512
// PGSIZE, PGROUNDUP, PGROUNDDOWN

/* stack entry that preserves the malloc operations occured */
struct header
{
    uint len;               // length of the buffer
    uint free_space;        // how much space is available in the page
    char* address;          // address of the beginning of the buffer
    struct header *prev;    // pointer to the previous header
};

/*Initialize empty stack*/
static struct header stack_base;    
static struct header *stack_top;    // pointer to the header in the top of the stack

//static uint stack_bottom = 0;

/*Allocates a buffer of length len onto the stack and returns a pointer to the beginning of the buffer.
If len is larger than the maximum allowed size of an allocation operation, the function should return -1.
If a call to sbrk() fails, the function should return -1.
Note: the “heap” area does not yet exist at the first call to ustack_malloc().
Refer to the implementation of malloc() to see how to handle this case.*/
void *
ustack_malloc(uint len)
{
    printf("ustack_malloc()\n");
    if (len > MAX_ALLOCATION_SIZE || len <= 0){
        printf("len is larger than the maximum allowed size of an allocation operation\n");
        return (void *)-1;
    }
        
    // First call to ustack_malloc()
    if (stack_top == 0){
        printf("First call to ustack_malloc()\n");
        if((stack_base.address = sbrk(PGSIZE)) == (void *)-1){
            printf("sbrk() failed\n");
            return (void *)-1;
        }
        stack_base.free_space = PGSIZE;
        stack_base.len = 0;
        stack_base.prev = stack_top = &stack_base; // maybe assign 0
        stack_top->address = (char *)(stack_top + 1);
    }

    struct header *new_header;
    // Check if there is enough memory in the current page
    if (stack_top->free_space >= len + sizeof(struct header)){
        printf("Enough memory in the current page\n");
        new_header = (struct header *)(stack_top->address + stack_top->len);
        new_header->address = stack_top->address + sizeof(struct header) + stack_top->len;
        new_header->free_space = stack_top->free_space - len - sizeof(struct header);
        new_header->len = len;
        new_header->prev = stack_top;
        stack_top = new_header;
    }
    else{
        // Allocate a new page
        printf("Allocate a new page\n");
        if ((new_header = (struct header *)sbrk(PGSIZE)) == (void *)-1)
            return (char *)-1;
        new_header->address = (char *)(new_header + 1);
        new_header->free_space = PGSIZE - len - sizeof(struct header);
        new_header->len = len;
        new_header->prev = stack_top;
        stack_top = new_header;
    }
    return stack_top->address;
}

int ustack_free()
{
    printf("ustack_free()\n");
    // Check if there is a buffer to free
    if (stack_top == 0 || stack_top == &stack_base){
        printf("No buffer to free\n");
        return -1;
    }
    
    struct header *prev = stack_top->prev;

    int len_to_free = stack_top->len;
    // Check if the buffer is the only one in the page
    if (len_to_free + stack_top->free_space + sizeof(struct header) == PGSIZE)
        if (sbrk(-PGSIZE) == (void *)-1){
            printf("sbrk() failed\n");
            return -1;
        }
    stack_top = prev;
    return len_to_free;
}
