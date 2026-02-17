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

#include <string.h>

typedef struct{
    uint32_t timestamp;
    uint32_t size;
} package;

typedef struct {
    uint32_t wnd_size;
    uint32_t k;

    package* pack_buffer;
    uint32_t capacity;
    
    uint32_t ts;
    uint32_t* level_counts;
    uint32_t max_level;
} StateApx;

static inline uint32_t bucket_level(uint32_t size) {
    uint32_t lvl = 0;
    while ((1u << lvl) < size) {
        lvl++;
    }
    return lvl;
}

// k = 1/eps
// if eps = 0.01 (relative error 1%) then k = 100
// if eps = 0.001 (relative error 0.1%) the k = 1000
uint64_t wnd_bit_count_apx_new(StateApx* self, uint32_t wnd_size, uint32_t k) {
    assert(wnd_size >= 1);
    assert(k >= 1);

    self->ts = 0;
    self->wnd_size = wnd_size;
    self->k = k;
    self->max_level = 0;

    uint64_t T = ((uint64_t)wnd_size - 1) / (uint64_t)k + 1;
    while (((uint64_t)1 << (self->max_level + 1)) <= T) {
        self->max_level++;
    }
    self->capacity = (k + 2) * (self->max_level + 1);

    size_t bytes_for_buckets = (size_t)self->capacity * sizeof(package);
    size_t bytes_for_levels  = (size_t)(self->max_level + 1) * sizeof(uint32_t);

    self->pack_buffer = (package*)malloc(bytes_for_buckets);
    self->level_counts = (uint32_t*)malloc(bytes_for_levels);

    for (uint32_t lvl = 0; lvl <= self->max_level; lvl++ ) {
        self->level_counts[lvl] = 0;
    }

    uint64_t total_bytes = (uint64_t)bytes_for_buckets + (uint64_t)bytes_for_levels;

    // The function should return the total number of bytes allocated on the heap.
    return total_bytes;
}

void wnd_bit_count_apx_destruct(StateApx* self) {
    // Make sure you free the memory allocated on the heap.
    free(self->pack_buffer);
    free(self->level_counts);
}

void wnd_bit_count_apx_print(StateApx* self) {
    // This is useful for debugging.
}

uint32_t wnd_bit_count_apx_next(StateApx* self, bool item) {
    // TODO: Fill me.
    self->ts += 1;
    uint32_t used = 0;
    uint32_t lvl;
    for (lvl = 0; lvl <= self->max_level; lvl++) {
        used += self->level_counts[lvl];
    }

    if (used > 0 && self->ts - self->pack_buffer[0].timestamp >= self->wnd_size){
        uint32_t temp = bucket_level(self->pack_buffer[0].size);
        assert(self->level_counts[temp] > 0);
        self->level_counts[temp]--;

        memmove(self->pack_buffer, self->pack_buffer + 1, (used - 1) * sizeof(package));
        used--;
    }

    if (item){
        package *b = &self->pack_buffer[used];
        b->timestamp = self->ts;
        b->size = 1;
        used++;

        self->level_counts[bucket_level(1)]++;
    }

    uint32_t cur = 0;
    while (cur <= self->max_level){
        if (self->level_counts[cur] <= self->k + 1) {
            break;
        }

        int first  = -1, second = -1;
        for (uint32_t i = 0; i < used; i++){
            if (bucket_level(self->pack_buffer[i].size) == cur){
                if (first == -1) {
                    first = (int)i;
                } else {
                    second = (int)i;
                    break;
                }
            }
        }

        self->pack_buffer[second].size *= 2;
        memmove(&self->pack_buffer[first], &self->pack_buffer[first + 1], (used - first - 1) * sizeof(package));
        used--;

        self->level_counts[cur] -= 2;
        cur ++;
        self->level_counts[cur]++;

        N_MERGES++;
    }

    if (used == 0){
        return 0;
    }

    uint32_t sum = 0;
    for (uint32_t i = 1; i < used; ++i) {
        sum += self->pack_buffer[i].size;
    }
    // Oldest bucket is counted as 1.
    sum ++;

    return sum;
}

#endif // _WINDOW_BIT_COUNT_APX_
