#ifndef _WINDOW_BIT_COUNT_APX_
#define _WINDOW_BIT_COUNT_APX_

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>

uint64_t N_MERGES = 0; // keep track of how many bucket merges occur

/*
    TODO: You can add code here.
*/

typedef struct{
    uint32_t timestamp;
    uint32_t size;
} package;

typedef struct {
    // Window size and k
    uint32_t wnd_size;
    uint32_t k;

    // The memory pointer for every package.
    package* pack_buffer;
    
    uint32_t ts; // Timestamp

    // This will remember how many packages are in each level
    uint32_t* level_counters;

    // Pointing to each level respectively.
    uint32_t* level_pointers;

    // The theoretical maximum package size of this implementation.
    uint32_t max_level;

    // To make this code faster, record the sum and the largest package.
    uint32_t sum;
    int32_t top;
} StateApx;

static inline package* find_level(StateApx* s, int level) {
    return s->pack_buffer + (size_t)level * (s->k + 2);
}

// k = 1/eps
// if eps = 0.01 (relative error 1%) then k = 100
// if eps = 0.001 (relative error 0.1%) the k = 1000
uint64_t wnd_bit_count_apx_new(StateApx* self, uint32_t wnd_size, uint32_t k) {
    assert(wnd_size >= 1);
    assert(k >= 1);

    // Initialization
    self->ts = 0;
    self->wnd_size = wnd_size;
    self->k = k;
    self->max_level = 0;

    self->sum = 0;
    self->top = 0;

    // The method for calculating the max level in class
    uint64_t T = ((uint64_t)wnd_size - 1) / (uint64_t)k + 1;
    while (((uint64_t)1 << (self->max_level + 1)) <= T) {
        self->max_level++;
    }

    // Allocate the memory once.
    size_t buckets = (size_t)(self->k + 2) * (size_t)(self->max_level + 1) * sizeof(package);
    size_t pointers  = (size_t)(self->max_level + 1) * sizeof(uint32_t);
    size_t counters = (size_t)(self->max_level + 1) * sizeof(uint32_t);

    // This ensures that the displacement is accurate.
    uint8_t* mem = (uint8_t*)malloc(buckets + pointers + counters);

    self->pack_buffer = (package*)mem;
    self->level_pointers = (uint32_t*)(mem + buckets);
    self->level_counters = (uint32_t*)(mem + buckets + pointers);

    // Another initialization
    for (uint32_t lvl = 0; lvl <= self->max_level; lvl++ ) {
        self->level_pointers[lvl] = 0;
        self->level_counters[lvl] = 0;
    }

    uint64_t total_bytes = (uint64_t)(buckets + pointers + counters);

    // The function should return the total number of bytes allocated on the heap.
    return total_bytes;
}

void wnd_bit_count_apx_destruct(StateApx* self) {
    // Make sure you free the memory allocated on the heap.
    free(self->pack_buffer);
}

void wnd_bit_count_apx_print(StateApx* self) {
    // This is useful for debugging.
    printf("Levels: %u\n", self->max_level + 1);
    for (uint32_t L = 0; L <= self->max_level; L++) {
        printf("  Level %u: %u buckets\n", L, self->level_counters[L]);
    }
}

uint32_t wnd_bit_count_apx_next(StateApx* self, bool item) {
    self->ts += 1;
    uint32_t size = self->k + 2;

    // 1. Expire. We keep self->level_pointers[lvl] always points to the oldest package in one level.
    for (int lvl = (int)self->max_level; lvl >= 0; lvl --){
        if(self->level_counters[lvl] == 0) continue;
        package* temp = find_level(self, lvl);
        uint32_t p = self->level_pointers[lvl];
        if (self->ts-temp[p].timestamp >= self->wnd_size){
            // 4) Update.
            self->sum -= temp[p].size;

            self->level_pointers[lvl] = (p + 1) % size;
            self->level_counters[lvl] -- ;

            while (self->top >= 0 && self->level_counters[self->top] == 0) {
                self->top--;
            }
        }
        break;
    }

    // 2. Insert. Based on heads we can find tails.
    if (item){
        package *b = find_level(self, 0);
        uint32_t t = (self->level_pointers[0] + self->level_counters[0]) % size;
        b[t].timestamp = self->ts;
        b[t].size = 1;
        self->level_counters[0]++;
        
        // 4) Update.
        self->sum ++;
        if (self->top < 0) self->top = 0;

        // 3. Merge. Only happens when "item = true" to be faster.
        for (uint32_t cur = 0; cur < self->max_level; cur++ ){
            if (self->level_counters[cur] <= self->k + 1) {
                break;
            }
            package* cur_lvl = find_level(self, cur);
            uint32_t p0 = self->level_pointers[cur];
            
            // Delete old packages
            package oldest = cur_lvl[p0];
            p0 = (p0 + 1) % size;
            package second = cur_lvl[p0];
            p0 = (p0 + 1) % size;
            self->level_pointers[cur] = p0;
            self->level_counters[cur] -= 2;
            
            // Create new packages
            package newP;
            newP.timestamp = second.timestamp;
            newP.size = second.size * 2;

            // Find new position for new package.
            package* pos = find_level(self, cur + 1);
            pos[(self->level_pointers[cur + 1] + 
                self->level_counters[cur + 1]) % size] = newP;
            self->level_counters[cur + 1] ++;

            // 4) Update
            if ((int)(cur + 1) > self->top) self->top = (int)(cur + 1);

            N_MERGES++;
        }
    }

    // 4. Count all the "1"s.
    if (self->sum == 0){
        return 0;
    }
    package* top = find_level(self, self->top);
    // Oldest bucket is counted as 1.
    return self->sum - top[self->level_pointers[self->top]].size + 1;
}

#endif // _WINDOW_BIT_COUNT_APX_