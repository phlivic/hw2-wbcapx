# Window Bit Count Approximation — 修改紀錄

## 原始 Implementation 的問題

### 問題 1：兩次 `malloc`（違反規定）

作業要求初始化只能呼叫一次 `malloc`，但原本的程式呼叫了兩次：

```c
self->pack_buffer  = malloc(bytes_for_buckets);  // 第一次
self->level_counts = malloc(bytes_for_levels);   // 第二次
```

### 問題 2：`memmove` 造成嚴重效能瓶頸

所有 bucket 存在一個平坦陣列中（最舊在 index 0，最新在尾端）。
每次 expire 或 merge 都需要搬動整個陣列：

```c
memmove(self->pack_buffer, self->pack_buffer + 1, ...); // expire
memmove(&self->pack_buffer[first], ...);                // merge
```

實測：N = 10^9 的 benchmark 跑超過幾分鐘仍未完成。

---

## 做出的改動

### 改動 1：合併成單次 `malloc`

將兩塊記憶體合併為一次分配，用指標偏移切割區域：

```c
uint8_t* mem = malloc(bytes_for_buckets + bytes_for_levels);
self->pack_buffer  = (package*)mem;
self->level_counts = (uint32_t*)(mem + bytes_for_buckets);
// destruct 只需 free(self->pack_buffer)
```

### 改動 2：平坦陣列 → Per-level Circular Buffer

**核心觀察**：高 level 的 bucket 永遠比低 level 的 bucket 舊，
因此全域最舊的 bucket 必定在最高非空 level 的 front。

將每個 level 改為獨立的環形緩衝區，全部共用同一塊 malloc：

```
malloc 記憶體配置：
[ pack_buffer : (max_level+1) × (k+2) × sizeof(package) ]
[ level_head  : (max_level+1) × sizeof(uint32_t)         ]  ← 每個 level 的環形頭
[ level_count : (max_level+1) × sizeof(uint32_t)         ]
```

操作複雜度對比：

| 操作 | 舊版（memmove） | 新版（circular buffer） |
|------|----------------|------------------------|
| expire oldest | O(k·logW) | **O(1)** pop_front |
| insert | O(1) | O(1) push_back |
| merge | O(k·logW) | **O(1)** pop×2 + push×1 |
| estimate | O(k·logW) scan | **O(1)**（`total_sum` 即時維護）|

新增 `total_sum` 欄位即時維護 bucket size 加總：
- insert：+1
- expire：減去 expired.size
- merge：不變（-s - s + 2s = 0）

### 改動 3：新增 `.gitignore`

忽略 make 產出的暫存檔案：

```
results.txt / *_results.txt   # benchmark 結果
*.pdf                         # R 產生的 plots
```

---

## 效能比較（N = 10^9, W = 10^8, k = 1000）

| 版本 | throughput | memory |
|------|-----------|--------|
| memmove（舊） | 未完成（> 幾分鐘） | — |
| circular buffer（新） | **42,440,222 items/sec** | 136,408 bytes |
