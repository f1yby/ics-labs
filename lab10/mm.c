#include "mm.h"
#include "memlib.h"
#include "stdint.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/**
 * Lab 10 Allocation Implementation
 * Implements mm_init, mm_alloc, mm_check, mm_free, mm_realloc, and
 * mm_check, get_header as tool function
 *
 * Aligment is 32 bit
 *
 * Allocate:
 * Allocation Requests are classified in to two types, small chunk / big chunk.
 * Request size smaller than ALIGMENT HEADERS will first look up the
 * FREE_HEADERS, which is a list store chunks with certain size. If failed,
 * mm_alloc will travel in to big chunk free list, to find a slot. Request
 * bigger than ALIGMENT * HEADERS will go through the big chunk to find a slot.
 * If no slot find, sbrk will be called. Big Chunk will just request its size.
 * Small Chunk will request N(size) * size, N is natural.
 *
 * Free:
 * Free will mark the chunk alloced as free, add it to Free List. Compaction
 * will execute after each free.
 *
 * Realloc:
 * Realloc in most time is simply Free, Allocate and Copy, EXCEPT Align(new
 * size) and Align(old size) are equal, or the chunk is at the border.In the
 * former case, Realloc will do nothing. In the latter one Ralloc will simply
 * call sbrk.
 */

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 32

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x1f)
#define USEDF (1)
#define PUSED (0x2)

#define IS_USED(pointer) (*(uint64_t *)(pointer)&USEDF)
#define IS_PUSED(pointer) ((*(uint64_t *)(pointer)&PUSED) >> 1)
#define GET_PSIZE(pointer) (*(uint64_t *)(pointer) & ~0x1f)
#define GET_DATA(pointer) ((void *)(pointer) + 8)
#define GET_PADD(pointer) (*(uint64_t *)(pointer))
#define SET_PADD(pointer, padd) (*(uint64_t *)(pointer) = (padd))
#define GET_FLAG(pointer) (*(uint64_t *)(pointer)&0x1f)
#define MM_CHECK
#define HEADERS 18 // 7-85 8-82 9-83 10-93
// 7 68 75
// 8 67 43
// 9 65 42
// 10 76 42
// 11 75 45
// 12 74 55
// 13 81 48
// 14 80 43
// 15 79 43
// 16 84 45
// 17 50 51 X
// 18 88 47
// 19 50 44

#define MIN_CHUNCK_SIZE ((HEADERS - 1) * ALIGNMENT)

/**
 * Free Chunk:
 * +--------+----+--------+
 * |Paddling|Data|Paddling|
 * +--------+----+--------+
 *
 * Allocated Chunk:
 * +--------+----+
 * |Paddling|Data|
 * +--------+----+
 */

typedef struct paddling {
  uint64_t paddling;
  struct paddling *prev;
  struct paddling *next;
} paddling_t;

void *MEM_HEAP_LO;
void *MEM_HEAP_HIGH;
paddling_t *FREE_HEADERS;

/*
 * @brief Initialize Memory manage Structure
 *
 * +-------+------+
 * |Headers|Chunks|
 * +-------+------+
 */
int mm_init(void) {
  // Allocate Headers
  MEM_HEAP_LO = mem_sbrk(sizeof(paddling_t) * HEADERS);
  FREE_HEADERS = MEM_HEAP_LO;
  MEM_HEAP_LO = mem_heap_lo() + sizeof(paddling_t) * HEADERS;
  memset(FREE_HEADERS, 0, sizeof(paddling_t) * HEADERS);
  for (int i = 0; i < HEADERS; ++i) {
    FREE_HEADERS[i].prev = &FREE_HEADERS[i];
    FREE_HEADERS[i].next = &FREE_HEADERS[i];
  }
  MEM_HEAP_HIGH = mem_heap_hi();
  return 0;
}

/**
 * @bried Use size to get corresponding Header
 * @param size size of Chunk
 *
 * +--------------------+--------------------+---+
 * |Header 1 (size = 32)|Header 2 (size = 64)|...|
 * +--------------------+--------------------+---+
 */

paddling_t *get_header(uint64_t size) {
  if (size > MIN_CHUNCK_SIZE) {
    return FREE_HEADERS + HEADERS - 1;
  }
  return size / 32 + FREE_HEADERS;
}

/**
 * @brief Wrapper of mem_sbrk, can receive negative size
 * @param size move MEM_HEAP_HIGH by size
 */

void *mm_sbrk(int size) {
  MEM_HEAP_HIGH += size;
  if (MEM_HEAP_HIGH > mem_heap_hi()) {
    mem_sbrk(MEM_HEAP_HIGH - mem_heap_hi());
  }
  return MEM_HEAP_HIGH - size + 1;
}
/**
 * @brief Memory Structure Checker
 * Check Header errors and Chunk errors
 */

void mm_check(void) {
  // Check header errors
  for (paddling_t *h = FREE_HEADERS; h != MEM_HEAP_LO; ++h) {
    for (paddling_t *p = h->next; p != h; p = p->next) {
    }
  }

  // travel through list to check errors
  void *p = MEM_HEAP_LO;
  for (; (void *)p <= MEM_HEAP_HIGH;) {
    if (!IS_USED(p)) {
      if ((GET_PADD(p) & ~0X2 & ~0x1) !=
          (GET_PADD(p + GET_PSIZE(p) - 8) & ~0X2 & ~0x1)) {
        abort();
      }
    }
    p += GET_PSIZE(p);
  }
  if (p != MEM_HEAP_HIGH + 1) {
    abort();
  }
}

/**
 * @brief Allocate data.
 * @param size size of user data
 * @return pointer to allocated space or NULL on failure.
 *  Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size) {
  uint64_t alloc_size = ALIGN(size + 8);
  //  alloc_size = 24 + (alloc_size + 8) / 32 * 32;
  paddling_t *FREE_HEADER = get_header(alloc_size);
  paddling_t *pointer;
  do {
    pointer = FREE_HEADER;
    do {
      pointer = pointer->prev;
    } while (pointer != FREE_HEADER && GET_PSIZE(pointer) < alloc_size);
    if (pointer != FREE_HEADER) {
      break;
    }
    ++FREE_HEADER;
  } while (FREE_HEADER != MEM_HEAP_LO);

  if (FREE_HEADER == MEM_HEAP_LO) {
    if (alloc_size <= MIN_CHUNCK_SIZE) {
      uint64_t chunk_size = MIN_CHUNCK_SIZE;
      switch (alloc_size) {
      case 32:
        chunk_size = 6 * 32;
        break;
      default:
        break;
      }

      pointer = mm_sbrk(chunk_size);
      if (pointer == (void *)-1) {
        return NULL;
      } else {
        void *p = pointer;
        SET_PADD(pointer, alloc_size | USEDF | PUSED);
        pointer = p + alloc_size;
        paddling_t *new_paddling = pointer;
        uint64_t free_size = chunk_size - alloc_size;
        SET_PADD(pointer, (free_size & ~USEDF) | PUSED);
        SET_PADD(p + chunk_size - 8, free_size | USEDF | PUSED);
        FREE_HEADER = get_header(free_size);
        new_paddling->prev = FREE_HEADER;
        new_paddling->next = FREE_HEADER->next;
        FREE_HEADER->next->prev = new_paddling;
        FREE_HEADER->next = new_paddling;
        MM_CHECK;
        return GET_DATA(p);
      }

    } else {
      pointer = mm_sbrk(alloc_size);
      if (pointer == (void *)-1) {
        return NULL;
      } else {
        SET_PADD(pointer, alloc_size | USEDF | PUSED);
        MM_CHECK;
        return GET_DATA(pointer);
      }
    }

  } else {
    uint64_t old_size = GET_PSIZE(pointer);
    if (old_size <= alloc_size + 32) {
      paddling_t *p = pointer;
      p->prev->next = p->next;
      p->next->prev = p->prev;

      SET_PADD(pointer, old_size | USEDF | PUSED);
      SET_PADD((void *)pointer + old_size,
               GET_PADD((void *)pointer + old_size) | PUSED);
      MM_CHECK;
      return GET_DATA(pointer);
    } else {
      void *p = pointer;
      paddling_t *old_paddling = pointer;
      SET_PADD(pointer, alloc_size | USEDF | PUSED);
      pointer = p + alloc_size;
      paddling_t *new_paddling = pointer;
      old_size = old_size - alloc_size;
      SET_PADD(pointer, (old_size & ~USEDF) | PUSED);
      SET_PADD(p + alloc_size + old_size - 8, (old_size & ~USEDF) | PUSED);
      old_paddling->prev->next = old_paddling->next;
      old_paddling->next->prev = old_paddling->prev;

      FREE_HEADER = get_header(old_size);
      new_paddling->prev = FREE_HEADER;
      new_paddling->next = FREE_HEADER->next;
      FREE_HEADER->next->prev = new_paddling;
      FREE_HEADER->next = new_paddling;
      MM_CHECK;
      return GET_DATA(p);
    }
  }
}

/**
 * @bried Free allocated Data.
 * @param ptr pointer to data
 */
void mm_free(void *ptr) {

  ptr -= 8;
  uint64_t size = GET_PSIZE(ptr);
  void *free_begin = ptr;
  void *free_end = ptr + size - 8;
  uint64_t free_size = size;
  if (!IS_PUSED(ptr)) {
    void *prv_ptr = ptr - 8;

    free_begin = ptr - GET_PSIZE(prv_ptr);
    ((paddling_t *)free_begin)->prev->next = ((paddling_t *)free_begin)->next;
    ((paddling_t *)free_begin)->next->prev = ((paddling_t *)free_begin)->prev;
    free_size += GET_PSIZE(free_begin);
  }
  ptr += GET_PSIZE(ptr);
  if (ptr <= MEM_HEAP_HIGH) {
    if (!IS_USED(ptr)) {
      ((paddling_t *)ptr)->prev->next = ((paddling_t *)ptr)->next;
      ((paddling_t *)ptr)->next->prev = ((paddling_t *)ptr)->prev;
      free_end = ptr + GET_PSIZE(ptr) - 8;
      free_size += GET_PSIZE(ptr);
    } else {
      SET_PADD(ptr, GET_PADD(ptr) & ~PUSED);
    }
  } else {
    mm_sbrk(-((int)free_size));
    MM_CHECK;
    return;
  }
  SET_PADD(free_begin, (free_size + GET_FLAG(free_begin)) & ~USEDF);
  SET_PADD(free_end, (free_size + GET_FLAG(free_begin)) & ~USEDF);

  paddling_t *paddling = free_begin;
  paddling_t *FREE_HEADER = get_header(free_size);
  paddling->prev = FREE_HEADER;
  paddling->next = FREE_HEADER->next;
  FREE_HEADER->next->prev = paddling;
  FREE_HEADER->next = paddling;

  MM_CHECK;
}

/**
 * @bried resize user data or find a new slot for it.
 * @param ptr pointer to user data
 * @param size new size
 * @return pointer to new data on success, NULL on failure.
 */
void *mm_realloc(void *ptr, size_t size) {
  void *old_ptr = ptr - 8;
  uint64_t chunk_size = ALIGN(size + 8);
  if (GET_PSIZE(old_ptr) >= chunk_size) {
    return ptr;
  }
  if (old_ptr + GET_PSIZE(old_ptr) == MEM_HEAP_HIGH + 1) {
    mm_sbrk(chunk_size - GET_PSIZE(old_ptr));
    SET_PADD(old_ptr, chunk_size | GET_FLAG(old_ptr));
    return ptr;
  }

  void *new_ptr;
  size_t copySize;

  new_ptr = mm_malloc(size);
  if (new_ptr == NULL)
    return NULL;
  copySize = chunk_size - 8;
  if (size < copySize)
    copySize = size;
  memcpy(new_ptr, ptr, copySize);
  mm_free(ptr);
  return new_ptr;
}
