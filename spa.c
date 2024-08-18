#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>

typedef intptr_t word_t;

size_t align(size_t x) {
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

size_t allocSize(size_t size) {
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

bool canSplit(struct Block *block, size_t size) {
  return (int)(allocSize(block->size) - size) >= (int)sizeof(struct Block);
}

struct Block *listAllocate(struct Block *block, size_t size) {
  block->used = true;
  block->size = size;

  return block;
}

int getBucket(size_t size) {
  int res = size / sizeof(word_t) - 1;
  if(res >= NUM_LISTS) return NUM_LISTS - 1;
  return res;
}

struct Block *firstFit(size_t size) {
  struct Block *block = heapStart;

  while (block != NULL) {
    // O(n) search.
    if (block->used || block->size < size) {
      block = block->next;
      continue;
    }

    // Found the block:
    return listAllocate(block, size);
  }

  return NULL;
}

struct Block *findBlock(size_t size) {
  int bucket = getBucket(size);
  struct Block *originalHeapStart = heapStart;

  heapStart = segregatedLists[bucket];

  struct Block *block = firstFit(size);

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
  for (int i = 0; i < NUM_LISTS; i++) {
      struct Block *block = segregatedLists[i];
      originalHeapStart = heapStart;
      heapStart = block;
      while (block != NULL) {
          callback(block);
          block = block->next;
      }
      heapStart = originalHeapStart;
    }
}

void printSegregatedLists() {
    for (int i = 0; i < NUM_LISTS; i++) {
        printf("List %d: ", i);
        struct Block *block = segregatedLists[i];
        while (block != NULL) {
            printf("[%zu, %d] ", block->size, block->used);
            block = block->next;
        }
        printf("\n");
    }
}

void printBlock(struct Block *block) {
    printf("[%zu, %d, %p]\n", block->size, block->used, (void*)block);
}

void printBlocks() {
    traverse(printBlock);
    printf("\n");
}

int main(int argc, char const *argv[]) {
  init();
  word_t *all = allocMem(sizeof(word_t) * 1);
  word_t *all2 = allocMem(sizeof(word_t) * 3);
  word_t *all3 = allocMem(sizeof(word_t) * 2);
  word_t *all4 = allocMem(sizeof(word_t) * 5);
  word_t *all5 = allocMem(sizeof(word_t) * 6);
  word_t *all7 = allocMem(sizeof(word_t) * 7);
  word_t *all8 = allocMem(sizeof(word_t) * 20);
  word_t *all9 = allocMem(sizeof(word_t) * 4);
  
  printBlocks();
  printSegregatedLists();
  freeMem(all);
  printSegregatedLists();
  freeMem(all7);
  printSegregatedLists();
  printBlocks();
  all = allocMem(sizeof(word_t) * 1);
  word_t *all11 = allocMem(sizeof(word_t) * 1);
  
  printBlocks();
  printSegregatedLists();
}