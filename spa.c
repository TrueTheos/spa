#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <unistd.h>
#include <assert.h>

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

enum SearchMode {
  FirstFit,
  NextFit,
  BestFit,
  SegregatedList,
};

static struct Block *heapStart = NULL;
static struct Block *top = NULL;
static struct Block *searchStart = NULL;
static enum SearchMode searchMode = FirstFit;

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
  auto freePart = (struct Block *)((char *)block + allocSize(size));
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
  if (searchMode != SegregatedList && canSplit(block, size)) {
    block = split(block, size);
  }

  block->used = true;
  block->size = size;

  return block;
}

struct Block *firstFit(size_t size) {
  auto block = heapStart;

  while (block != NULL) {
    if (block->used || block->size < size) {
      block = block->next;
      continue;
    }

    return listAllocate(block, size);
  }

  return NULL;
}

struct Block *nextFit(size_t size) {
  if (searchStart == NULL) {
    searchStart = heapStart;
  }

  auto start = searchStart;
  auto block = start;

  while (block != NULL) {
    if (block->used || block->size < size) {
      block = block->next;
      if (block == NULL) {
        block = heapStart;
      }
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

struct Block *bestFit(size_t size) {
  auto block = heapStart;
  struct Block *best = NULL;

  while (block != NULL) {
    if (block->used || block->size < size) {
      block = block->next;
      continue;
    }
    // Found a block of a smaller size, than previous best:
    if (best == NULL || block->size < best->size) {
      best = block;
      block = block->next;
    }
  }

  if (best == NULL) {
    return NULL;
  }

  return listAllocate(best, size);
}

inline int getBucket(size_t size) {
  return size / sizeof(word_t) - 1;
}

/**
 * Segregated fit algorithm.
 */
struct Block *segregatedFit(size_t size) {
  // Bucket number based on size.
  auto bucket = getBucket(size);
  auto originalHeapStart = heapStart;

  // Init the search.
  heapStart = segregatedLists[bucket];

  // Use first-fit here, but can be any:
  auto block = firstFit(size);

  heapStart = originalHeapStart;
  return block;
}

struct Block *findBlock(size_t size) {
  switch (searchMode) {
    case FirstFit:
      return firstFit(size);
    case NextFit:
      return nextFit(size);
    case BestFit:
      return bestFit(size);
    case SegregatedList:
      return segregatedFit(size);
  }
}

struct Block *coalesce(struct Block *block) {
  if (!block->next->used) {
    if (block->next == top) {
      top = block;
    }

    block->size += block->next->size;
    block->next = block->next->next;
  }
  return block;
}

bool canCoalesce(struct Block *block) { return block->next && !block->next->used; }

struct Block *getHeader(word_t *data) {
  return (struct Block *)((char *)data - offsetof(struct Block, data));
}

void resetHeap() {
  // Already reset.
  if (heapStart == NULL) {
    return;
  }

  brk(heapStart);

  heapStart = NULL;
  top = NULL;
  searchStart = NULL;
}

void init(enum SearchMode mode) {
  searchMode = mode;
  resetHeap();
}

word_t *alloc(size_t size) {
  size = align(size);

  struct Block *block = findBlock(size);
  if (block != NULL) {
    return block->data;
  }

  auto block = requestFromOS(size);

  block->size = size;
  block->used = true;

  if (searchMode == SegregatedList) {
    auto bucket = getBucket(size);
    if (segregatedLists[bucket] == NULL) {
      segregatedLists[bucket] = block;
    }
    if (segregatedTops[bucket] != NULL) {
      segregatedTops[bucket]->next = block;
    }
    segregatedTops[bucket] = block;
  } else {
    if (heapStart == NULL) {
      heapStart = block;
    }
    if (top != NULL) {
      top->next = block;
    }
    top = block;
  }

  return block->data;
}

void free(word_t *data) {
  auto block = getHeader(data);
  if (searchMode != SegregatedList && canCoalesce(block)) {
    block = coalesce(block);
  }
  block->used = false;
}

void visit(void (*callback)(struct Block *)) {
  auto block = heapStart;
  while (block != NULL) {
    callback(block);
    block = block->next;
  }
}

void segregatedTraverse(void (*callback)(struct Block *)) {
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

void traverse(void (*callback)(struct Block *)) {
  if (searchMode == SegregatedList) {
    return segregatedTraverse(callback);
  }
  visit(callback);
}