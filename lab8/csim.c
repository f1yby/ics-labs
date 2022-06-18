#include "cachelab.h"
#include "stdint.h"
#include "stdio.h"
#include <getopt.h>
#include "string.h"
#include "stdlib.h"

typedef struct {
    uint64_t tag;
    uint8_t valid;
    uint64_t lastVisitTime;
} line_t;
typedef struct {
    line_t *lines;
    uint64_t e;
} set_slot_t;
typedef struct {
    set_slot_t *sets;
    uint8_t setIndexBit;
    uint8_t blockBit;
    uint32_t hitTimes;
    uint32_t missTimes;
    uint32_t evictionTimes;
} cache_t;

//cache init
void cacheInit(cache_t *cache, uint8_t blockBit, uint8_t setIndexBit, uint64_t e);

//cache delete
void cacheDelete(cache_t *cache);

//set slot init
void setSlotInit(set_slot_t *setSlot, uint64_t e);

//set slot delete
void setSlotDelete(set_slot_t *setSlot);

//simulate cache
void cacheVisitAddress(cache_t *cache, uint64_t address);

//eviction = miss + eviction
enum set_visit_result {
    hit, miss, eviction
};

//simulate set
enum set_visit_result setVisitAddress(set_slot_t *setSlot, uint64_t tag);

//run simulation
void simulateCache(cache_t *cache, char *traceFileName);


int main(int argc, char *const argv[]) {
  char *fileName = NULL;
  int option = getopt(argc, argv, "sEbt");
  uint64_t setIndexBit = 0;
  uint64_t blockBit = 0;
  uint64_t e = 0;
  while (option != -1) {
    switch (option) {
      case 's':
        setIndexBit = strtoul(argv[optind], NULL, 10);
        break;
      case 'E':
        e = strtoul(argv[optind], NULL, 10);
        break;
      case 'b':
        blockBit = strtoul(argv[optind], NULL, 10);
        break;
      case 't':
        fileName = malloc(strlen(argv[optind]) + 1);
        strcpy(fileName, argv[optind]);
        break;
      default:
        break;
    }

    option = getopt(argc, argv, "sEbt");
  }
  //check param validation
  if (fileName == NULL || setIndexBit + blockBit > 64) {
    exit(0);
  }
  cache_t *cache = malloc(sizeof(cache_t));
  cacheInit(cache, blockBit, setIndexBit, e);
  simulateCache(cache, fileName);
  printSummary((int) cache->hitTimes, (int) cache->missTimes, (int) cache->evictionTimes);
  cacheDelete(cache);
  free(fileName);
  return 0;
}

void cacheInit(cache_t *cache, uint8_t blockBit, uint8_t setIndexBit, uint64_t e) {
  cache->setIndexBit = setIndexBit;
  cache->blockBit = blockBit;


  cache->evictionTimes = 0;
  cache->missTimes = 0;
  cache->hitTimes = 0;

  cache->sets = malloc(sizeof(set_slot_t) * (1 << setIndexBit));
  for (int i = 0; i < 1 << setIndexBit; ++i) {
    setSlotInit(&cache->sets[i], e);
  }

}

void setSlotInit(set_slot_t *setSlot, uint64_t e) {
  setSlot->e = e;
  setSlot->lines = malloc(sizeof(line_t) * e);
  for (int i = 0; i < e; ++i) {
    setSlot->lines[i].lastVisitTime = 0;
    setSlot->lines[i].tag = 0;
    setSlot->lines[i].valid = 0;
  }

}

void cacheVisitAddress(cache_t *cache, uint64_t address) {
  uint64_t setIndex = (address >> cache->blockBit) & ((1 << cache->setIndexBit) - 1);
  uint64_t tag = address >> (cache->blockBit + cache->setIndexBit);

  switch (setVisitAddress(&cache->sets[setIndex], tag)) {
    case hit:
      ++cache->hitTimes;
      break;
    case miss:
      ++cache->missTimes;
      break;
    case eviction:
      ++cache->missTimes;
      ++cache->evictionTimes;
      break;
  }

}

enum set_visit_result setVisitAddress(set_slot_t *setSlot, uint64_t tag) {
  line_t *line;

  uint64_t latestVisitTime = 0;
  for (int i = 0; i < setSlot->e; ++i) {
    line = &setSlot->lines[i];
    if (line->valid && line->lastVisitTime > latestVisitTime) {
      latestVisitTime = line->lastVisitTime;
    }
  }
  latestVisitTime += 1;

  for (int i = 0; i < setSlot->e; ++i) {
    line = &setSlot->lines[i];
    if (line->valid && line->tag == tag) {
      line->lastVisitTime = latestVisitTime;
      return hit;
    }
  }

  for (int i = 0; i < setSlot->e; ++i) {
    line = &setSlot->lines[i];
    if (!line->valid) {
      line->valid = 1;
      line->tag = tag;
      line->lastVisitTime = latestVisitTime;
      return miss;
    }
  }

  uint64_t oldestVisitTime = latestVisitTime;
  line_t *evictLine = NULL;
  for (int i = 0; i < setSlot->e; ++i) {
    line = &setSlot->lines[i];
    if (line->valid && line->lastVisitTime < oldestVisitTime) {
      oldestVisitTime = line->lastVisitTime;
      evictLine = line;
    }
  }
  evictLine->tag = tag;
  evictLine->lastVisitTime = latestVisitTime;
  return eviction;
}

void simulateCache(cache_t *cache, char *traceFileName) {
  FILE *traceFile = fopen(traceFileName, "r");
  static char line[1024];
  while (fgets(line, 1024, traceFile)) {
    if (line[0] == 'I') {
      continue;
    }
    uint64_t address = strtoull(line + 2, NULL, 16);
    switch (line[1]) {
      case 'L':
        cacheVisitAddress(cache, address);
        break;
      case 'S':
        cacheVisitAddress(cache, address);
        break;
      case 'M':
        cacheVisitAddress(cache, address);
        cacheVisitAddress(cache, address);
        break;
      default:
        break;
    }


  }
  fclose(traceFile);
}

void cacheDelete(cache_t *cache) {
  for (int i = 0; i < (1 << cache->setIndexBit); ++i) {
    setSlotDelete(&cache->sets[i]);
  }
  free(cache->sets);
  free(cache);
}

void setSlotDelete(set_slot_t *setSlot) {
  free(setSlot->lines);
}
