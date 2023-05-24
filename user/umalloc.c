#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

// Memory allocator by Kernighan and Ritchie,
// The C programming Language, 2nd ed.  Section 8.7.

typedef long Align;

union header {
  struct {
    union header *ptr; /* next block if on free list */
    uint size; /* size of this block */
  } s;
  Align x; 
};

typedef union header Header;

static Header base; /* empty list to get started */ 
static Header *freep; /* start of free list */

/* free: put block ap in free list */
void
free(void *ap)
{
  Header *bp, *p;
  
  bp = (Header*)ap - 1; // Find address of block header for the data to be inserted

  /* Step through the free list looking for the position in the list to place
   * the insertion block.  In the typical circumstances this would be the block
   * immediately to the left of the insertion block; this is checked for by
   * finding a block that is to the left of the insertion block and such that
   * the following block in the list is to the right of the insertion block.
   * However this check doesn't check for one such case, and misses another.  We
   * still have to check for the cases where either the insertion block is
   * either to the left of every other block owned by malloc (the case that is
   * missed), or to the right of every block owned by malloc (the case not
   * checked for).  These last two cases are what is checked for by the
   * condition inside of the body of the loop.
   */
  for(p = freep; !(bp > p && bp < p->s.ptr); p = p->s.ptr)
   /* p >= p->s.ptr implies that the current block is the rightmost
     * block in the free list.  Then if the insertion block is to the right of
     * that block, then it is the new rightmost block; conversely if it is to
     * the left of the block that p points to (which is the current leftmost
     * block), then the insertion block is the new leftmost block.  Note that
     * this conditional handles the case where we only have 1 block in the free
     * list (this case is the reason that we need >= in the first test rather
     * than just >).
     */
    if(p >= p->s.ptr && (bp > p || bp < p->s.ptr))
      break; /* freed block at start or end of blocks owned by malloc*/


  /* Having found the correct location in the free list to place the insertion
   * block, now we have to (i) link it to the next block, and (ii) link the
   * previous block to it.  These are the tasks of the next two if/else pairs.
   */

  /* case: the end of the insertion block is adjacent to the beginning of
   * another block of data owned by malloc.  Absorb the block on the right into
   * the block on the left (i.e. the previously existing block is absorbed into
   * the insertion block).
   */
  if(bp + bp->s.size == p->s.ptr){
    bp->s.size += p->s.ptr->s.size;
    bp->s.ptr = p->s.ptr->s.ptr;
    /* case: the insertion block is not left-adjacent to the beginning of another
   * block of data owned by malloc.  Set the insertion block member to point to
   * the next block in the list.
   */
  } else
    bp->s.ptr = p->s.ptr;
    /* case: the end of another block of data owned by malloc is adjacent to the
   * beginning of the insertion block.  Absorb the block on the right into the
   * block on the left (i.e. the insertion block is absorbed into the preceeding
   * block).
   */
  if(p + p->s.size == bp){
    p->s.size += bp->s.size;
    p->s.ptr = bp->s.ptr;
    /* case: the insertion block is not right-adjacent to the end of another block
   * of data owned by malloc.  Set the previous block in the list to point to
   * the insertion block.
   */
  } else
    p->s.ptr = bp;
    /* Set the free pointer list to start the block previous to the insertion
   * block.  This makes sense because calls to malloc start their search for
   * memory at the next block after freep, and the insertion block has as good a
   * chance as any of containing a reasonable amount of memory since we've just
   * added some to it.  It also coincides with calls to morecore from
   * malloc because the next search in the iteration looks at exactly the
   * right memory block.
   */
  freep = p;
}

/* morecore: ask system for more memory */
static Header*
morecore(uint nu)
{
  char *p;      // The address of the newly created memory
  Header *hp;   // Header ptr for integer arithmatic and constructing header

 /* Obtaining memory from OS is a comparatively expensive operation, so obtain
   * at least 4096 blocks of memory and partition as needed
   */
  if(nu < 4096)
    nu = 4096;
     /* Request that the OS increment the program's data space.  sbrk changes the
   * location of the program break, which defines the end of the process's data
   * segment (i.e., the program break is the first location after the end of the
   * uninitialized data segment).  Increasing the program break has the effect
   * of allocating memory to the process.  On success, brk returns the previous
   * break - so if the break was increased, then this value is a pointer to the
   * start of the newly allocated memory.
   */
  p = sbrk(nu * sizeof(Header));
  if(p == (char*)-1)
    return 0;
  hp = (Header*)p;
  hp->s.size = nu;
  free((void*)(hp + 1));
  return freep;
}

void*
malloc(uint nbytes)
{
  Header *p, *prevp;
  uint nunits;
/* Calculate the number of memory units needed to provide at least nbytes of
   * memory.
   *
   * Suppose that we need n >= 0 bytes and that the memory unit sizes are b > 0
   * bytes.  Then n / b (using integer division) yields one less than the number
   * of units needed to provide n bytes of memory, except in the case that n is
   * a multiple of b; then it provides exactly the number of units needed.  It
   * can be verified that (n - 1) / b provides one less than the number of units
   * needed to provide n bytes of memory for all values of n > 0.  Thus ((n - 1)
   * / b) + 1 provides exactly the number of units needed for n > 0.
   *
   * The extra sizeof(Header) in the numerator is to include the unit of memory
   * needed for the header itself.
   */
  nunits = (nbytes + sizeof(Header) - 1)/sizeof(Header) + 1;
  if((prevp = freep) == 0){ /* no free list yet, we have to initialize. */
  // Create degenerate free list; base points to itself and has size 0
  // Set free list starting point to base address
    base.s.ptr = freep = prevp = &base;
    base.s.size = 0;
  }
  // p = current block
  for(p = prevp->s.ptr; ; prevp = p, p = p->s.ptr){
    if(p->s.size >= nunits){      // big enough
      if(p->s.size == nunits)     // exactly
        prevp->s.ptr = p->s.ptr;
      else {                      // allocate tail end
        p->s.size -= nunits;      // Changes the memory stored at currp to reflect the reduced block size
        p += p->s.size;           // Find location at which to create the block header for the new block
        p->s.size = nunits;       // Store the block size in the new header
      }
      /* Set global starting position to the previous pointer.  Next call to
       * malloc will start either at the remaining part of the partitioned block
       * if a partition occurred, or at the block after the selected block if
       * not.
       */
      freep = prevp;
      /* Return the location of the start of the memory, i.e. after adding one so as to move past the header */
      return (void*)(p + 1);
    }
    if(p == freep)
      if((p = morecore(nunits)) == 0)
        return 0; /* none left */
  }
}
