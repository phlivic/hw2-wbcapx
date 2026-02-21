#ifndef _WINDOW_BIT_COUNT_APX_
#define _WINDOW_BIT_COUNT_APX_

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>

uint64_t N_MERGES = 0; // keep track of how many bucket merges occur

typedef struct {
    uint32_t timestamp;
    uint32_t size;
} package;

/*
 * Each level L owns a circular buffer of (k+2) slots inside pack_buffer.
 * level_head[L]  = physical index of the oldest (front) bucket at level L.
 * level_count[L] = number of live buckets at level L.
 *
 * Higher levels always hold older (smaller-timestamp) buckets, so the
 * global oldest bucket is always the front of the highest non-empty level.
 * This lets expire run in O(1).  Insert and per-level merge are also O(1).
 *
 * total_sum is maintained incrementally:
 *   +1  on insert,  -expired.size  on expire,  0  on merge (−s−s+2s = 0).
 * Estimate = total_sum − oldest_size + 1  (oldest bucket counted as 1).
 *
 * Single malloc layout:
 *   [ pack_buffer : (max_level+1) × slots_per_level × sizeof(package) ]
 *   [ level_head  : (max_level+1) × sizeof(uint32_t)                  ]
 *   [ level_count : (max_level+1) × sizeof(uint32_t)                  ]
 */
typedef struct {
    uint32_t wnd_size;
    uint32_t k;
    uint32_t max_level;
    uint32_t slots_per_level; // = k + 2
    uint32_t ts;
    uint32_t total_sum;       // running sum of all live bucket sizes

    package*  pack_buffer; // [max_level+1][slots_per_level]
    uint32_t* level_head;  // circular-buffer head per level
    uint32_t* level_count; // live bucket count per level
} StateApx;

/* ---- circular-buffer helpers (per level) ---- */

static inline package* lbuf(StateApx* s, uint32_t L) {
    return s->pack_buffer + (size_t)L * s->slots_per_level;
}

static inline package lb_front(StateApx* s, uint32_t L) {
    return lbuf(s, L)[s->level_head[L]];
}

// Remove and return the oldest bucket; advance head by 1 (mod slots_per_level).
static inline package lb_pop_front(StateApx* s, uint32_t L) {
    package p = lbuf(s, L)[s->level_head[L]];
    s->level_head[L] = (s->level_head[L] + 1) % s->slots_per_level;
    s->level_count[L]--;
    return p;
}

// Append a bucket at the tail without moving any existing elements.
static inline void lb_push_back(StateApx* s, uint32_t L, package p) {
    uint32_t tail = (s->level_head[L] + s->level_count[L]) % s->slots_per_level;
    lbuf(s, L)[tail] = p;
    s->level_count[L]++;
}

/* ---- public API ---- */

// k = 1/eps
// if eps = 0.01 (relative error 1%) then k = 100
// if eps = 0.001 (relative error 0.1%) the k = 1000
uint64_t wnd_bit_count_apx_new(StateApx* self, uint32_t wnd_size, uint32_t k) {
    assert(wnd_size >= 1);
    assert(k >= 1);

    self->ts        = 0;
    self->wnd_size  = wnd_size;
    self->k         = k;
    self->total_sum = 0;
    self->max_level = 0;

    uint64_t T = ((uint64_t)wnd_size - 1) / (uint64_t)k + 1;
    while (((uint64_t)1 << (self->max_level + 1)) <= T)
        self->max_level++;

    self->slots_per_level = k + 2;
    uint32_t num_levels = self->max_level + 1;

    size_t bytes_buckets = (size_t)self->slots_per_level * num_levels * sizeof(package);
    size_t bytes_heads   = (size_t)num_levels * sizeof(uint32_t);
    size_t bytes_counts  = (size_t)num_levels * sizeof(uint32_t);

    // Single malloc: all three arrays are packed into one contiguous block.
    // pack_buffer sits at the start; level_head and level_count follow it.
    uint8_t* mem = (uint8_t*)malloc(bytes_buckets + bytes_heads + bytes_counts);
    self->pack_buffer = (package*)mem;
    self->level_head  = (uint32_t*)(mem + bytes_buckets);
    self->level_count = (uint32_t*)(mem + bytes_buckets + bytes_heads);

    for (uint32_t L = 0; L < num_levels; L++) {
        self->level_head[L]  = 0;
        self->level_count[L] = 0;
    }

    return (uint64_t)(bytes_buckets + bytes_heads + bytes_counts);
}

void wnd_bit_count_apx_destruct(StateApx* self) {
    // pack_buffer points to the start of the single malloc block,
    // so freeing it releases level_head and level_count as well.
    free(self->pack_buffer);
}

void wnd_bit_count_apx_print(StateApx* self) {
    // useful for debugging
}

uint32_t wnd_bit_count_apx_next(StateApx* self, bool item) {
    self->ts++;

    // 1. Expire: oldest bucket = front of highest non-empty level.
    //    Higher levels always hold older buckets, so no linear scan needed.
    //    At most one bucket expires per step (window advances by 1).
    for (int L = (int)self->max_level; L >= 0; L--) {
        if (self->level_count[L] > 0) {
            if (self->ts - lb_front(self, L).timestamp >= self->wnd_size) {
                package expired = lb_pop_front(self, (uint32_t)L);
                self->total_sum -= expired.size;
            }
            break;
        }
    }

    // 2. Insert: new size-1 bucket at level 0.
    if (item) {
        package p;
        p.timestamp = self->ts;
        p.size      = 1;
        lb_push_back(self, 0, p);
        self->total_sum += 1;

        // 3. Merge cascade: if level L has k+2 buckets, merge the two oldest
        //    into one size-doubled bucket at level L+1.
        //    Merged bucket takes the newer timestamp (most recent 1 inside it).
        //    total_sum is unchanged by merges: −s − s + 2s = 0.
        for (uint32_t L = 0; L <= self->max_level; L++) {
            if (self->level_count[L] <= self->k + 1) break;
            assert(L < self->max_level);

            package older = lb_pop_front(self, L); (void)older;
            package newer = lb_pop_front(self, L);
            package merged;
            merged.timestamp = newer.timestamp;
            merged.size      = newer.size * 2;
            lb_push_back(self, L + 1, merged);
            N_MERGES++;
        }
    }

    // 4. Estimate = total_sum − oldest_size + 1.
    //    The oldest bucket may straddle the window boundary, so we count it
    //    as 1 (lower bound) to guarantee apx <= exact within error 1/k.
    if (self->total_sum == 0) return 0;

    uint32_t oldest_size = 0;
    for (int L = (int)self->max_level; L >= 0; L--) {
        if (self->level_count[L] > 0) {
            oldest_size = lb_front(self, (uint32_t)L).size;
            break;
        }
    }

    return self->total_sum - oldest_size + 1;
}

#endif // _WINDOW_BIT_COUNT_APX_
