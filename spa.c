#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#include <unistd.h>

typedef intptr_t word_t;

inline size_t align(size_t x) {
  return (x + sizeof(word_t) - 1) & ~(sizeof(word_t) - 1);
}

struct Block
{
  size_t size;
  bool used;

  struct Block *next;
  word_t data[1];
};

static struct Block *heapStart = NULL;
static struct Block *top = NULL;
static struct Block *searchStart = NULL;

struct Block *segregatedLists[] = {
  NULL,   //   8
  NULL,   //  16
  NULL,   //  32
  NULL,   //  64
  NULL,   // 128
};

struct Block *segregatedTops[] = {
  NULL,   //   8
  NULL,   //  16
  NULL,   //  32
  NULL,   //  64
  NULL,   // 128
};

#define NUM_LISTS (sizeof(segregatedLists) / sizeof(segregatedLists[0]))

inline size_t allocSize(size_t size) {
  return sizeof(struct Block) + size - sizeof(((struct Block *)0)->data);
}

static struct Block *requestFromOS(size_t size) {
    struct Block *block = (struct Block *)sbrk(0);

    if (sbrk(allocSize(size)) == (void *)-1) {
        return NULL;
    }

    return block;
}

struct Block *split(struct Block *block, size_t size) {
  struct Block *freePart = (struct Block *)((char *)block + allocSize(size));
  freePart->size = block->size - allocSize(size);
  freePart->used = false;
  freePart->next = block->next;

  block->size = size;
  block->next = freePart;

  return block;
}

inline bool canSplit(struct Block *block, size_t size) {
  return (int)(allocSize(block->size) - size) >= (int)sizeof(struct Block);
}

struct Block *listAllocate(struct Block *block, size_t size) {
  block->used = true;
  block->size = size;

  return block;
}

inline int getBucket(size_t size) {
  return size / sizeof(word_t) - 1;
}

struct Block *nextFit(size_t size) {
  // Prime the search start.
  if (searchStart == NULL) {
    searchStart = heapStart;
  }

  struct Block *start = searchStart;
  struct Block *block = start;

  while (block != NULL) {
    // O(n) search.
    if (block->used || block->size < size) {
      block = block->next;
      // Start from the beginning.
      if (block == NULL) {
        block = heapStart;
      }
      // Did the full circle, and didn't find.
      if (block == start) {
        break;
      }
      continue;
    }

    // Found the block:
    searchStart = block;
    return listAllocate(block, size);
  }

  return NULL;
}

struct Block *findBlock(size_t size) {
  int bucket = getBucket(size);
  struct Block *originalHeapStart = heapStart;

  heapStart = segregatedLists[bucket];

  struct Block *block = nextFit(size);

  heapStart = originalHeapStart;
  return block;
}

struct Block *getHeader(word_t *data) {
  return (struct Block *)((char *)data - offsetof(struct Block, data));
}

void resetHeap() {
  if (heapStart == NULL) {
    return;
  }

  brk(heapStart);

  heapStart = NULL;
  top = NULL;
  searchStart = NULL;
}

void init() {
  resetHeap();
}

word_t *allocMem(size_t size) {
  size = align(size);

  struct Block *b = findBlock(size);
  if (b != NULL) {
    return b->data;
  }

  struct Block *block = requestFromOS(size);

  block->size = size;
  block->used = true;

  int bucket = getBucket(size);
  if (segregatedLists[bucket] == NULL) {
    segregatedLists[bucket] = block;
  }
  if (segregatedTops[bucket] != NULL) {
    segregatedTops[bucket]->next = block;
  }
  segregatedTops[bucket] = block;

  return block->data;
}

void freeMem(word_t *data) {
  struct Block *block = getHeader(data);
  block->used = false;
}

void visit(void (*callback)(struct Block *)) {
  struct Block *block = heapStart;
  while (block != NULL) {
    callback(block);
    block = block->next;
  }
}

void traverse(void (*callback)(struct Block *)) {
  struct Block *originalHeapStart;
  struct Block *block;
  for (int i = 0; i < NUM_LISTS; i++) {
    block = segregatedLists[i];

    while (block != NULL) {
      originalHeapStart = heapStart;
      heapStart = block;
      visit(callback);
      heapStart = originalHeapStart;

      block = block->next;
    }
  }
}