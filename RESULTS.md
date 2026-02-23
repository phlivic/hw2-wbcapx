# Benchmark Results

## Setup

| Parameter | Value |
|------|-----|
| Stream length (N) | 150,000,000 |
| Trials (T) | 5 |
| Window sizes (W) | 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000 |
| k values | 10, 100, 1000 |
| Compiler flag | `-O0` |

---

## Throughput (items/sec, average of 5 trials)

| W | exact | apx[k=10] | apx[k=100] | apx[k=1000] |
|--:|------:|----------:|-----------:|------------:|
| 10 | 358,939,089 | 69,515,598 | 68,219,963 | 67,992,111 |
| 100 | 360,784,387 | 46,776,479 | 67,507,520 | 68,207,911 |
| 1,000 | 373,805,646 | 44,083,389 | 47,240,256 | 68,473,779 |
| 10,000 | 373,560,541 | 44,338,082 | 43,880,097 | 46,838,331 |
| 100,000 | 372,975,310 | 43,756,413 | 44,021,541 | 43,453,478 |
| 1,000,000 | 360,526,072 | 44,124,852 | 44,003,610 | 43,759,470 |
| 10,000,000 | 375,103,973 | 43,902,657 | 43,917,362 | 43,623,822 |
| 100,000,000 | 372,452,073 | 41,410,617 | 42,321,794 | 41,682,197 |

---

## Memory Footprint (bytes)

| W | exact | apx[k=10] | apx[k=100] | apx[k=1000] |
|--:|------:|----------:|-----------:|------------:|
| 10 | 10 | 104 | 824 | 8,024 |
| 100 | 100 | 416 | 824 | 8,024 |
| 1,000 | 1,000 | 728 | 3,296 | 8,024 |
| 10,000 | 10,000 | 1,040 | 5,768 | 32,096 |
| 100,000 | 100,000 | 1,456 | 8,240 | 56,168 |
| 1,000,000 | 1,000,000 | 1,768 | 11,536 | 80,240 |
| 10,000,000 | 10,000,000 | 2,080 | 14,008 | 112,336 |
| 100,000,000 | 100,000,000 | 2,496 | 16,480 | 136,408 |

---

## Observations

### Throughput
- **exact** maintains a stable ~370M items/sec across all W, because it uses a circular buffer and each operation is O(1).
- **apx** has higher throughput when W is very small (W=10), where merges almost never occur; as W grows, it stabilizes around ~43-44M items/sec.
- **apx[k=1000]** performs better at small W because larger k allows more buckets, resulting in fewer merges.
- exact throughput is about **8-9x** higher than apx, at the cost of memory growing linearly with W.

### Memory
- **exact** memory = W bytes, growing linearly with W (at W=10^8, it requires 100 MB).
- **apx** memory = O(k · log W), with very slow growth.
  - apx[k=10]: as W increases from 10 to 10^8, memory only rises from 104 bytes to 2,496 bytes.
  - apx[k=1000]: at W=10^8, it needs only 136,408 bytes (~133 KB), about **700x** less than exact.
- Larger k reduces error but increases memory usage (k=1000 uses about 55x memory of k=10).
