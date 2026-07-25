// Extreme tests for malloc_3.cpp: buddy-allocator reference model + edge scenarios.
//
// A full independent simulation of the spec (tightest fit at minimal address,
// binary splitting, XOR-buddy merging, mmap threshold) predicts the EXACT
// address and all statistics after every one of 4000 randomized operations.
// Plus: heap exhaustion without sbrk, 1024-block checkerboard, order ladder,
// mmap-list surgery, and srealloc merge-priority scenarios.
//
// Note: this test uses STL for the model. That's safe here (and allowed -
// the STL ban applies to the submitted malloc files): malloc_3 calls sbrk
// only during init, so libc allocations in the test can't interfere.
// Build: g++ test_extreme_3.cpp malloc_3.cpp -o tx3 && ./tx3
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <unistd.h>
#include <set>
#include <map>

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
static int g_op = -1;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FAIL: %s (op %d, line %d)\n", name, g_op, __LINE__); } \
} while(0)
#define SECTION(s) printf("-- %s\n", s)

static size_t META;
static size_t ORD[11];
static const size_t BLOCK = 128 * 1024;
static char* g_base = 0;                 // start of the aligned 4MB region
static size_t BASE_BYTES;

static int at_baseline(){
    return _num_free_blocks() == 32 && _num_allocated_blocks() == 32 &&
           _num_free_bytes() == BASE_BYTES && _num_allocated_bytes() == BASE_BYTES;
}
static void fill(char* p, size_t n, unsigned char s){
    for (size_t i = 0; i < n; i++) p[i] = (char)(s + i * 11);
}
static int verify(const char* p, size_t n, unsigned char s){
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)p[i] != (unsigned char)(s + i * 11)) return 0;
    return 1;
}

// ---------------- reference buddy model ----------------
struct Model {
    std::set<size_t> fl[11];             // free block offsets per order
    std::map<size_t, int> used;          // used heap blocks: offset -> order
    size_t mmap_blocks, mmap_bytes;

    void init(){
        for (int i = 0; i <= 10; i++) fl[i].clear();
        used.clear(); mmap_blocks = 0; mmap_bytes = 0;
        for (size_t i = 0; i < 32; i++) fl[10].insert(i * BLOCK);
    }
    static int orderFor(size_t sz){
        for (int k = 0; k <= 10; k++) if (ORD[k] >= sz + META) return k;
        return -1;
    }
    bool feasible(size_t sz){
        int k = orderFor(sz);
        if (k < 0) return false;
        for (int j = k; j <= 10; j++) if (!fl[j].empty()) return true;
        return false;
    }
    size_t alloc(size_t sz){             // tightest order, minimal address, split down
        int k = orderFor(sz);
        for (int j = k; j <= 10; j++) {
            if (fl[j].empty()) continue;
            size_t off = *fl[j].begin();
            fl[j].erase(fl[j].begin());
            for (int i = j - 1; i >= k; i--) fl[i].insert(off + ORD[i]);
            used[off] = k;
            return off;
        }
        return (size_t)-1;
    }
    void freeHeap(size_t off){           // merge with free buddies upward
        int o = used[off]; used.erase(off);
        while (o < 10) {
            std::set<size_t>::iterator it = fl[o].find(off ^ ORD[o]);
            if (it == fl[o].end()) break;
            size_t bud = *it; fl[o].erase(it);
            if (bud < off) off = bud;
            o++;
        }
        fl[o].insert(off);
    }
    // srealloc heap-grow: in-place / merge-in-place (spec 1a+1b). Returns true
    // and sets *noff on success; false means "relocate or fail" (spec 1c).
    bool tryGrow(size_t off, size_t sz, size_t* noff){
        int o = used[off];
        if (ORD[o] - META >= sz) { *noff = off; return true; }
        size_t cur = off; int co = o; bool ok = false;
        while (co < 10) {                                  // simulate, don't merge yet
            size_t bud = cur ^ ORD[co];
            if (!fl[co].count(bud)) break;
            if (bud < cur) cur = bud;
            co++;
            if (ORD[co] - META >= sz) { ok = true; break; }
        }
        if (!ok) return false;
        used.erase(off);                                   // commit the merges
        size_t c = off; int oo = o;
        while (ORD[oo] - META < sz) {
            size_t bud = c ^ ORD[oo];
            fl[oo].erase(bud);
            if (bud < c) c = bud;
            oo++;
        }
        used[c] = oo; *noff = c; return true;
    }
    size_t nFreeBlocks(){ size_t c = 0; for (int i = 0; i <= 10; i++) c += fl[i].size(); return c; }
    size_t nFreeBytes(){ size_t b = 0; for (int i = 0; i <= 10; i++) b += fl[i].size() * (ORD[i] - META); return b; }
    size_t nAllocBlocks(){ return nFreeBlocks() + used.size() + mmap_blocks; }
    size_t nAllocBytes(){
        size_t b = nFreeBytes();
        for (std::map<size_t,int>::iterator it = used.begin(); it != used.end(); ++it)
            b += ORD[it->second] - META;
        return b + mmap_bytes;
    }
};
static Model M;

static int stats_match(){
    return _num_free_blocks()      == M.nFreeBlocks() &&
           _num_free_bytes()       == M.nFreeBytes() &&
           _num_allocated_blocks() == M.nAllocBlocks() &&
           _num_allocated_bytes()  == M.nAllocBytes() &&
           _num_meta_data_bytes()  == M.nAllocBlocks() * META;
}
static char* heap_ptr(size_t off){ return g_base + off + META; }

int main(){
    printf("=== malloc_3 extreme tests ===\n");
    META = _size_meta_data();
    BASE_BYTES = 32 * (BLOCK - META);
    for (int i = 0; i <= 10; i++) ORD[i] = (size_t)128 << i;

    SECTION("discover base");
    {
        char* p = (char*)smalloc(40);
        g_base = p - META;
        CHECK(((uintptr_t)g_base) % (32 * BLOCK) == 0, "region aligned to 32*128KB");
        sfree(p);
        CHECK(at_baseline(), "baseline");
    }

    SECTION("heap exhaustion: whole heap usable, no extra sbrk, mmap unaffected");
    {
        void* brk0 = sbrk(0);
        char* big[32];
        int ok_alloc = 1, ok_dist = 1, ok_data = 1;
        for (int i = 0; i < 32; i++) {
            big[i] = (char*)smalloc(BLOCK - META);
            if (!big[i]) ok_alloc = 0;
            else if (big[i] != heap_ptr((size_t)i * BLOCK)) ok_dist = 0;
        }
        CHECK(ok_alloc, "all 32 max-order blocks allocatable");
        CHECK(ok_dist, "blocks handed out in ascending address order, 128KB apart");
        CHECK(_num_free_blocks() == 0 && _num_free_bytes() == 0, "heap fully used");
        CHECK(smalloc(1) == NULL, "heap request with full heap returns NULL");
        CHECK(smalloc(BLOCK - META) == NULL, "max-order request with full heap returns NULL");
        CHECK(sbrk(0) == brk0, "allocator did NOT sbrk again under pressure");
        char* m = (char*)smalloc(200000);
        CHECK(m != NULL, "mmap path still works with exhausted heap");
        sfree(m);
        for (int i = 0; i < 32; i++) { fill(big[i], BLOCK - META, (unsigned char)i); }
        for (int i = 0; i < 32; i++) if (!verify(big[i], BLOCK - META, (unsigned char)i)) ok_data = 0;
        CHECK(ok_data, "every usable byte of all 4MB independent");
        for (int i = 0; i < 32; i++) sfree(big[i]);
        CHECK(at_baseline() && sbrk(0) == brk0, "baseline, break untouched");
    }

    SECTION("1024-block checkerboard: exact split/merge bookkeeping");
    {
        enum { N = 1024 };                       // fills exactly one 128KB block
        static char* p[N];
        int layout = 1;
        for (int i = 0; i < N; i++) {
            p[i] = (char*)smalloc(64);
            if (p[i] != heap_ptr((size_t)128 * i)) layout = 0;
        }
        CHECK(layout, "1024 order-0 blocks at consecutive 128-byte offsets");
        CHECK(_num_allocated_blocks() == 32 + 1023, "1023 splits => 1055 blocks");
        CHECK(_num_free_blocks() == 31, "31 max blocks left free");
        for (int i = 1; i < N; i += 2) sfree(p[i]);          // odd: buddies stay used
        CHECK(_num_free_blocks() == 31 + 512, "512 frees, ZERO merges (buddies busy)");
        CHECK(_num_allocated_blocks() == 1055, "no block count change without merges");
        char* q = (char*)smalloc(64);
        CHECK(q == p[1], "minimal-address free block reused");
        sfree(q);
        for (int i = 0; i < N; i += 2) sfree(p[i]);          // even: full merge cascade
        CHECK(at_baseline(), "1024 blocks cascade back to 32 max blocks");
    }

    SECTION("order ladder: exact fit and one-byte-over for every order");
    {
        for (int k = 0; k <= 10; k++) {
            char* p = (char*)smalloc(ORD[k] - META);         // exact fit for order k
            int splits = 10 - k;
            CHECK(p == heap_ptr(0), "exact-fit served at offset 0");
            CHECK(_num_allocated_blocks() == 32 + (size_t)splits, "split count = 10-k");
            CHECK(_num_free_bytes() == BASE_BYTES - (ORD[k] - META) - (size_t)splits * META,
                  "free bytes exact for this order");
            sfree(p);
            CHECK(at_baseline(), "ladder step returns to baseline");
        }
        for (int k = 0; k < 10; k++) {
            char* p = (char*)smalloc(ORD[k] - META + 1);     // one byte over -> order k+1
            CHECK(_num_allocated_blocks() == 32 + (size_t)(10 - k - 1), "rounds up to next order");
            sfree(p);
        }
        CHECK(at_baseline(), "baseline after ladder");
    }

    SECTION("mmap list surgery: free middle, head, tail; double free; NULL");
    {
        char* m1 = (char*)smalloc(140000);
        char* m2 = (char*)smalloc(150000);
        char* m3 = (char*)smalloc(160000);
        CHECK(m1 && m2 && m3, "three mmap blocks");
        CHECK(_num_allocated_blocks() == 35 &&
              _num_allocated_bytes() == BASE_BYTES + 450000, "mmap stats exact");
        sfree(m2);                                            // middle of the list
        CHECK(_num_allocated_blocks() == 34 && _num_allocated_bytes() == BASE_BYTES + 300000,
              "middle unlink correct");
        char* m4 = (char*)smalloc(155000);
        CHECK(m4 != NULL, "list still consistent after middle unlink");
        sfree(m1);                                            // head
        sfree(m3);                                            // (old) tail
        sfree(m4);
        CHECK(at_baseline(), "all mmap blocks returned");
        char* h = (char*)smalloc(40);
        size_t fb = _num_free_blocks();
        sfree(h);
        size_t fb2 = _num_free_blocks();
        sfree(h);                                             // heap double free
        sfree(NULL);
        CHECK(_num_free_blocks() == fb2 && fb2 != fb, "double free and NULL are no-ops");
        CHECK(at_baseline(), "baseline");
    }

    SECTION("srealloc priorities: left-buddy merge; freed block merges on relocate");
    {
        char* a = (char*)smalloc(40);                         // offset 0
        char* b = (char*)smalloc(40);                         // offset 128 (buddy of a)
        fill(b, 40, 42);
        sfree(a);
        char* r = (char*)srealloc(b, 180);                    // buddy at LOWER address is free
        CHECK(r == a, "merge lands on the lower buddy's address");
        CHECK(verify(r, 40, 42), "data moved down into merged block");
        sfree(r);
        CHECK(at_baseline(), "baseline");

        a = (char*)smalloc(40);                               // offset 0
        char* blocker = (char*)smalloc(200);                  // takes order-1 at offset 256
        CHECK(blocker == heap_ptr(256), "blocker at offset 256");
        fill(a, 40, 43);
        r = (char*)srealloc(a, 900);                          // chain blocked at order 2
        CHECK(r == heap_ptr(1024), "relocates to the free order-3 block at 1024");
        CHECK(verify(r, 40, 43), "data copied on relocate");
        char* q = (char*)smalloc(200);                        // old block must have merged 0+128
        CHECK(q == heap_ptr(0), "freed source merged with its buddy (challenge 1 on free)");
        sfree(q); sfree(blocker); sfree(r);
        CHECK(at_baseline(), "baseline");
    }

    SECTION("model fuzzer: 4000 ops, exact address + stats prediction each op");
    {
        M.init();
        enum { SLOTS = 48, OPS = 4000 };
        struct Slot { char* p; size_t usable; size_t req; unsigned char seed; bool heap; bool live; };
        static Slot sl[SLOTS];
        memset(sl, 0, sizeof(sl));
        unsigned long rng = 24680;
        #define RND(n) (rng = rng*6364136223846793005UL + 1442695040888963407UL, (size_t)((rng >> 33) % (n)))
        int corrupt = 0, addr_bad = 0, stats_bad = 0;
        void* brk0 = sbrk(0);

        for (g_op = 0; g_op < OPS; g_op++) {
            if (!stats_match()) { stats_bad++; break; }

            if (g_op % 137 == 0) {                            // failure injections
                int s = -1;
                for (int i = 0; i < SLOTS; i++) if (sl[i].live && sl[i].heap) { s = i; break; }
                size_t fb = _num_free_blocks();
                if (smalloc(0) != NULL || smalloc(100000001) != NULL) stats_bad++;
                if (s >= 0 && (srealloc(sl[s].p, 0) != NULL || _num_free_blocks() != fb)) stats_bad++;
                sfree(NULL);
            }

            int i = (int)RND(SLOTS);
            if (!sl[i].live) {                                // ---------- allocate
                if (RND(100) < 88) {                          // heap
                    size_t sz = (RND(3) == 0) ? 1 + RND(120000) : 1 + RND(3000);
                    if (sz + META > BLOCK) sz = BLOCK - META;
                    if (!M.feasible(sz)) {
                        if (smalloc(sz) != NULL) addr_bad++;  // must fail: no sbrk fallback
                        continue;
                    }
                    size_t off = M.alloc(sz);
                    char* p = (char*)smalloc(sz);
                    if (p != heap_ptr(off)) addr_bad++;
                    sl[i].p = p; sl[i].usable = ORD[M.used[off]] - META; sl[i].req = sz;
                    sl[i].seed = (unsigned char)RND(256); sl[i].heap = true; sl[i].live = true;
                    fill(p, sl[i].usable, sl[i].seed);        // claim every usable byte
                } else {                                      // mmap
                    size_t sz = (BLOCK - META + 1) + RND(150000);
                    char* p = (char*)smalloc(sz);
                    if (!p) { addr_bad++; continue; }
                    M.mmap_blocks++; M.mmap_bytes += sz;
                    sl[i].p = p; sl[i].usable = sz; sl[i].req = sz;
                    sl[i].seed = (unsigned char)RND(256); sl[i].heap = false; sl[i].live = true;
                    fill(p, sz, sl[i].seed);
                }
            } else if (RND(10) < 6) {                         // ---------- free
                if (!verify(sl[i].p, sl[i].usable, sl[i].seed)) corrupt++;
                sfree(sl[i].p);
                if (sl[i].heap) M.freeHeap((size_t)(sl[i].p - g_base) - META);
                else { M.mmap_blocks--; M.mmap_bytes -= sl[i].req; }
                sl[i].live = false;
            } else {                                          // ---------- realloc
                if (sl[i].heap) {
                    size_t nsz = 1 + RND(BLOCK - META);
                    size_t off = (size_t)(sl[i].p - g_base) - META;
                    size_t old_usable = sl[i].usable;
                    unsigned char old_seed = sl[i].seed;
                    size_t noff;
                    if (M.tryGrow(off, nsz, &noff)) {         // in place, possibly merged
                        char* p = (char*)srealloc(sl[i].p, nsz);
                        if (p != heap_ptr(noff)) addr_bad++;
                        if (!verify(p, old_usable, old_seed)) corrupt++;
                        sl[i].p = p; sl[i].usable = ORD[M.used[noff]] - META;
                    } else if (!M.feasible(nsz)) {            // must fail, oldp intact
                        if (srealloc(sl[i].p, nsz) != NULL) addr_bad++;
                        if (!verify(sl[i].p, old_usable, old_seed)) corrupt++;
                        continue;
                    } else {                                  // relocate: alloc new, free old
                        size_t noff2 = M.alloc(nsz);
                        char* p = (char*)srealloc(sl[i].p, nsz);
                        if (p != heap_ptr(noff2)) addr_bad++;
                        if (!verify(p, old_usable, old_seed)) corrupt++;
                        M.freeHeap(off);
                        sl[i].p = p; sl[i].usable = ORD[M.used[noff2]] - META;
                    }
                    sl[i].seed = (unsigned char)RND(256);
                    fill(sl[i].p, sl[i].usable, sl[i].seed);
                } else {                                      // mmap realloc: always new block
                    size_t nsz = (BLOCK - META + 1) + RND(150000);
                    if (nsz == sl[i].req) {
                        if (srealloc(sl[i].p, nsz) != sl[i].p) addr_bad++;
                        continue;
                    }
                    size_t keep = sl[i].req < nsz ? sl[i].req : nsz;
                    char* p = (char*)srealloc(sl[i].p, nsz);
                    if (!p || p == sl[i].p) addr_bad++;
                    if (p && !verify(p, keep, sl[i].seed)) corrupt++;
                    M.mmap_bytes += nsz - sl[i].req;
                    sl[i].p = p; sl[i].usable = nsz; sl[i].req = nsz;
                    sl[i].seed = (unsigned char)RND(256);
                    fill(p, nsz, sl[i].seed);
                }
            }

            if (g_op % 200 == 199) {                          // full integrity sweep
                for (int s = 0; s < SLOTS; s++)
                    if (sl[s].live && !verify(sl[s].p, sl[s].usable, sl[s].seed)) corrupt++;
            }
        }

        CHECK(stats_bad == 0, "stats matched the model after every operation");
        CHECK(addr_bad == 0, "every returned address matched the model's prediction");
        CHECK(corrupt == 0, "zero data corruption across 4000 ops");
        for (int s = 0; s < SLOTS; s++) if (sl[s].live) sfree(sl[s].p);
        CHECK(at_baseline(), "exact baseline after freeing everything");
        CHECK(sbrk(0) == brk0, "program break never moved during fuzz");
    }

    printf("=== malloc_3 extreme: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
