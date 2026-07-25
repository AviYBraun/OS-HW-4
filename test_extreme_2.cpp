// Extreme tests for malloc_2.cpp: reference-model fuzzer.
// An independent model of the spec (first-fit by address, whole-block reuse,
// blocks never split/merge/shrink) predicts the exact pointer and all six
// statistics after EVERY operation. 3000 randomized ops + failure injections.
// Build: g++ test_extreme_2.cpp malloc_2.cpp -o tx2 && ./tx2
#include <cstdio>
#include <cstring>

void* smalloc(size_t size);
void* scalloc(size_t num, size_t size);
void sfree(void* p);
void* srealloc(void* oldp, size_t size);
size_t _num_free_blocks();
size_t _num_free_bytes();
size_t _num_allocated_blocks();
size_t _num_allocated_bytes();
size_t _num_meta_data_bytes();
size_t _size_meta_data();

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FAIL: %s (op %d, line %d)\n", name, g_op, __LINE__); } \
} while(0)

static int g_op = -1;
static size_t META;

// ---------- reference model (plain arrays: the model itself must not
// ---------- disturb the program break with libc allocations) ----------
enum { MAXB = 8000 };
struct MBlock { char* p; size_t size; bool free_; unsigned char seed; };
static MBlock blk[MAXB];
static int nblk = 0;

static int model_fit(size_t sz){                    // first-fit, address order
    for (int i = 0; i < nblk; i++)
        if (blk[i].free_ && blk[i].size >= sz) return i;
    return -1;
}
static size_t m_free_blocks(){ size_t c=0; for(int i=0;i<nblk;i++) if(blk[i].free_) c++; return c; }
static size_t m_free_bytes(){ size_t b=0; for(int i=0;i<nblk;i++) if(blk[i].free_) b+=blk[i].size; return b; }
static size_t m_alloc_bytes(){ size_t b=0; for(int i=0;i<nblk;i++) b+=blk[i].size; return b; }

static int stats_match(){
    return _num_free_blocks()   == m_free_blocks() &&
           _num_free_bytes()    == m_free_bytes() &&
           _num_allocated_blocks() == (size_t)nblk &&
           _num_allocated_bytes()  == m_alloc_bytes() &&
           _num_meta_data_bytes()  == (size_t)nblk * META;
}

static void fill(char* p, size_t n, unsigned char s){
    for (size_t i = 0; i < n; i++) p[i] = (char)(s + i * 13);
}
static int verify(const char* p, size_t n, unsigned char s){
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)p[i] != (unsigned char)(s + i * 13)) return 0;
    return 1;
}

// register a block the allocator just created via sbrk
static int model_append(char* p, size_t sz, unsigned char seed){
    int bad = 0;
    for (int i = 0; i < nblk; i++) if (blk[i].p == p) bad = 1;      // must be new memory
    if (nblk > 0 && p <= blk[nblk-1].p) bad = 1;                    // heap grows upward
    blk[nblk].p = p; blk[nblk].size = sz; blk[nblk].free_ = false; blk[nblk].seed = seed;
    nblk++;
    return !bad;
}

int main(){
    printf("=== malloc_2 extreme: reference-model fuzzer ===\n");
    META = _size_meta_data();
    CHECK(_num_allocated_blocks() == 0, "fresh state");

    unsigned long rng = 987654321;
    #define RND(n) (rng = rng*6364136223846793005UL + 1442695040888963407UL, (size_t)((rng >> 33) % (n)))

    int corrupt = 0;
    for (g_op = 0; g_op < 3000; g_op++) {
        if (!stats_match()) { g_fail++; printf("  FAIL: stats diverged from model (op %d)\n", g_op); break; }

        // periodic failure injections: must change nothing
        if (g_op % 211 == 0) {
            size_t fb = _num_free_blocks();
            if (smalloc(0) != NULL || smalloc(100000001) != NULL ||
                scalloc(0, 3) != NULL || scalloc(100000000, 2) != NULL ||
                scalloc(4611686018427387905ULL, 4) != NULL || _num_free_blocks() != fb) {
                g_fail++; printf("  FAIL: failure injection changed state (op %d)\n", g_op);
            } else g_pass++;
            sfree(NULL);
        }

        int r = (int)RND(100);
        if (r < 40) {                                            // --- smalloc
            size_t sz = 1 + RND(600);
            int idx = model_fit(sz);
            char* p = (char*)smalloc(sz);
            if (!p) { g_fail++; printf("  FAIL: smalloc(%zu) NULL (op %d)\n", sz, g_op); continue; }
            if (idx >= 0) {
                CHECK(p == blk[idx].p, "reuse picks first fitting free block (ascending)");
                blk[idx].free_ = false; blk[idx].seed = (unsigned char)RND(256);
                fill(blk[idx].p, blk[idx].size, blk[idx].seed);  // whole block is ours (note 12)
            } else {
                CHECK(model_append(p, sz, (unsigned char)RND(256)), "new block at fresh, higher address");
                fill(p, sz, blk[nblk-1].seed);
            }
        } else if (r < 55) {                                     // --- scalloc
            size_t num = 1 + RND(20), sz = 1 + RND(30);
            int idx = model_fit(num * sz);
            char* p = (char*)scalloc(num, sz);
            if (!p) { g_fail++; printf("  FAIL: scalloc NULL (op %d)\n", g_op); continue; }
            int zeroed = 1;
            for (size_t i = 0; i < num * sz; i++) if (p[i] != 0) zeroed = 0;
            CHECK(zeroed, "scalloc zeroes the requested bytes");
            if (idx >= 0) {
                CHECK(p == blk[idx].p, "scalloc reuse follows first-fit too");
                blk[idx].free_ = false; blk[idx].seed = (unsigned char)RND(256);
                fill(blk[idx].p, blk[idx].size, blk[idx].seed);
            } else {
                CHECK(model_append(p, num * sz, (unsigned char)RND(256)), "scalloc new block");
                fill(p, num * sz, blk[nblk-1].seed);
            }
        } else if (r < 80) {                                     // --- sfree
            int live = -1, tries = 0;
            while (tries++ < 50) { int i = (int)RND(nblk ? nblk : 1); if (nblk && !blk[i].free_) { live = i; break; } }
            if (live < 0) continue;
            if (!verify(blk[live].p, blk[live].size, blk[live].seed)) corrupt++;
            sfree(blk[live].p);
            blk[live].free_ = true;
            if (RND(4) == 0) sfree(blk[live].p);                 // double free: no-op
        } else {                                                 // --- srealloc
            int live = -1, tries = 0;
            while (tries++ < 50) { int i = (int)RND(nblk ? nblk : 1); if (nblk && !blk[i].free_) { live = i; break; } }
            if (live < 0) continue;
            size_t nsz = 1 + RND(800);
            size_t old_size = blk[live].size;
            unsigned char old_seed = blk[live].seed;
            if (nsz <= old_size) {
                char* p = (char*)srealloc(blk[live].p, nsz);
                CHECK(p == blk[live].p, "shrink/equal reuses same block");
                if (!verify(p, old_size, old_seed)) corrupt++;
            } else {
                int fidx = model_fit(nsz);
                char* p = (char*)srealloc(blk[live].p, nsz);
                if (!p) { g_fail++; printf("  FAIL: srealloc NULL (op %d)\n", g_op); continue; }
                if (!verify(p, old_size, old_seed)) corrupt++;   // content copied
                blk[live].free_ = true;                          // old block freed
                unsigned char ns = (unsigned char)RND(256);
                if (fidx >= 0) {
                    CHECK(p == blk[fidx].p, "grow reuses first fitting free block");
                    blk[fidx].free_ = false; blk[fidx].seed = ns;
                    fill(blk[fidx].p, blk[fidx].size, ns);
                } else {
                    CHECK(model_append(p, nsz, ns), "grow gets fresh block when none fit");
                    fill(p, nsz, ns);
                }
            }
        }

        if (g_op % 100 == 99) {                                  // full integrity sweep
            for (int i = 0; i < nblk; i++)
                if (!blk[i].free_ && !verify(blk[i].p, blk[i].size, blk[i].seed)) corrupt++;
        }
    }
    CHECK(corrupt == 0, "zero data corruption events");
    CHECK(stats_match(), "stats match model at end of fuzz");

    // free everything: totals must be exact
    for (int i = 0; i < nblk; i++) if (!blk[i].free_) { sfree(blk[i].p); blk[i].free_ = true; }
    CHECK(_num_free_blocks() == (size_t)nblk, "all blocks free at end");
    CHECK(_num_free_bytes() == m_alloc_bytes(), "free bytes == total usable bytes");
    CHECK(_num_meta_data_bytes() == (size_t)nblk * META, "metadata total exact");
    printf("model: %d blocks, %zu usable bytes\n", nblk, m_alloc_bytes());

    printf("=== malloc_2 extreme: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
