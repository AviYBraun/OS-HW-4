// Comprehensive tests for malloc_3.cpp (Part 3 - Buddy Allocator)
// Build: g++ test_comprehensive_3.cpp malloc_3.cpp -o tc3 && ./tc3
//
// The buddy allocator restores itself exactly to its initial state when
// everything is freed, so every section starts and ends at "baseline".
#include <cstdio>
#include <cstring>
#include <cstdint>

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
    else { g_fail++; printf("  FAIL: %s (line %d)\n", name, __LINE__); } \
} while(0)
#define SECTION(s) printf("-- %s\n", s)

static const size_t BLOCK = 128 * 1024;          // order-10 block, metadata included
static size_t META;                               // set in main
static size_t BASE_BYTES;                         // 32 * (BLOCK - META)

static int at_baseline(){
    return _num_free_blocks() == 32 && _num_allocated_blocks() == 32 &&
           _num_free_bytes() == BASE_BYTES && _num_allocated_bytes() == BASE_BYTES &&
           _num_meta_data_bytes() == 32 * META;
}
static void fill(void* p, size_t n, unsigned char seed){
    for (size_t i = 0; i < n; i++) ((unsigned char*)p)[i] = (unsigned char)(seed + i * 7);
}
static int verify(const void* p, size_t n, unsigned char seed){
    for (size_t i = 0; i < n; i++)
        if (((const unsigned char*)p)[i] != (unsigned char)(seed + i * 7)) return 0;
    return 1;
}

int main(){
    printf("=== malloc_3 comprehensive tests ===\n");
    META = _size_meta_data();
    BASE_BYTES = 32 * (BLOCK - META);

    SECTION("initial state");
    CHECK(META <= 64, "metadata <= 64 bytes");
    CHECK(_num_free_blocks() == 32, "32 free blocks");
    CHECK(_num_allocated_blocks() == 32, "32 allocated blocks");
    CHECK(_num_free_bytes() == BASE_BYTES, "free bytes = 32*(128KB - meta)");
    CHECK(_num_allocated_bytes() == BASE_BYTES, "allocated bytes = 32*(128KB - meta)");
    CHECK(_num_meta_data_bytes() == 32 * META, "metadata bytes = 32 * meta");

    SECTION("heap region alignment (XOR-buddy prerequisite)");
    {
        char* p = (char*)smalloc(40);
        CHECK(((uintptr_t)(p - META)) % (32 * BLOCK) == 0, "region aligned to 32*128KB");
        sfree(p);
        CHECK(at_baseline(), "baseline restored");
    }

    SECTION("failure cases");
    CHECK(smalloc(0) == NULL, "smalloc(0)");
    CHECK(smalloc(100000001) == NULL, "smalloc(10^8+1)");
    CHECK(scalloc(0, 5) == NULL, "scalloc(0, n)");
    CHECK(scalloc(5, 0) == NULL, "scalloc(n, 0)");
    CHECK(scalloc(100000000, 2) == NULL, "scalloc: 2*10^8 rejected");
    CHECK(scalloc(4611686018427387905ULL, 4) == NULL, "scalloc: num*size overflow rejected");
    CHECK(srealloc(NULL, 0) == NULL, "srealloc(NULL, 0)");
    CHECK(at_baseline(), "failed calls changed nothing");
    {
        char* p = (char*)smalloc(40);
        fill(p, 40, 5);
        size_t fb = _num_free_blocks();
        CHECK(srealloc(p, 0) == NULL, "srealloc(p, 0) returns NULL");
        CHECK(_num_free_blocks() == fb, "srealloc(p, 0) did NOT free p");
        CHECK(srealloc(p, 100000001) == NULL, "srealloc(p, 10^8+1) returns NULL");
        CHECK(_num_free_blocks() == fb && verify(p, 40, 5), "p untouched after failures");
        sfree(p);
        CHECK(at_baseline(), "baseline restored");
    }

    SECTION("splitting: addresses and exact statistics");
    {
        char* p1 = (char*)smalloc(40);            // splits order 10 -> 0: 10 new blocks
        CHECK(_num_allocated_blocks() == 42, "first split: 32 -> 42 blocks");
        CHECK(_num_free_blocks() == 41, "41 free blocks after split");
        CHECK(_num_allocated_bytes() == BASE_BYTES - 10 * META, "each split spends one metadata");
        CHECK(_num_free_bytes() == BASE_BYTES - (128 - META) - 10 * META, "free bytes exact");
        CHECK(_num_meta_data_bytes() == 42 * META, "metadata bytes track block count");
        char* p2 = (char*)smalloc(40);            // buddy of p1, no split
        CHECK(p2 - p1 == 128, "buddy allocated exactly 128 bytes away");
        CHECK(_num_allocated_blocks() == 42, "no split for exact-fit free block");
        char* p3 = (char*)smalloc(40);            // splits the order-1 block at +256
        CHECK(p3 - p1 == 256, "third block at +256 (order-1 split)");
        CHECK(_num_allocated_blocks() == 43, "one more split");
        sfree(p1); sfree(p2); sfree(p3);
        CHECK(at_baseline(), "full merge cascade back to baseline");
    }

    SECTION("tightest fit, minimal address");
    {
        char* p1 = (char*)smalloc(40);
        char* p2 = (char*)smalloc(40);
        sfree(p1);                                 // order-0 free at offset 0 (buddy p2 in use)
        char* q = (char*)smalloc(40);
        CHECK(q == p1, "order-0 block preferred over larger blocks (tightest fit)");
        sfree(q); sfree(p2);
        CHECK(at_baseline(), "baseline restored");
    }

    SECTION("adjacent-but-not-buddies do not merge");
    {
        char* p[4];
        for (int i = 0; i < 4; i++) p[i] = (char*)smalloc(40);   // offsets 0,128,256,384
        size_t fb = _num_free_blocks();
        sfree(p[1]); sfree(p[2]);                  // 128 and 256: adjacent, NOT buddies
        CHECK(_num_free_blocks() == fb + 2, "two frees, zero merges");
        char* r = (char*)smalloc(200);             // needs order 1
        CHECK(r - p[0] == 512, "order-1 request skips the two unmergeable order-0 blocks");
        char* q1 = (char*)smalloc(40);
        char* q2 = (char*)smalloc(40);
        CHECK(q1 == p[1] && q2 == p[2], "order-0 holes reused in address order");
        sfree(p[0]); sfree(q1); sfree(q2); sfree(p[3]); sfree(r);
        CHECK(at_baseline(), "baseline restored");
    }

    SECTION("partial merge: only true buddy pairs combine");
    {
        char* p[8];
        int layout_ok = 1;
        for (int i = 0; i < 8; i++) p[i] = (char*)smalloc(40);
        for (int i = 0; i < 8; i++) if (p[i] - p[0] != 128 * i) layout_ok = 0;
        CHECK(layout_ok, "8 order-0 blocks packed contiguously");
        size_t ab = _num_allocated_blocks();
        sfree(p[0]); sfree(p[1]);                  // buddies -> merge to order 1 at offset 0
        CHECK(_num_allocated_blocks() == ab - 1, "exactly one merge happened");
        char* s = (char*)smalloc(200);
        CHECK(s == p[0], "merged order-1 block reused at minimal address");
        sfree(s);
        for (int i = 2; i < 8; i++) sfree(p[i]);
        CHECK(at_baseline(), "baseline restored");
    }

    SECTION("mmap threshold boundary");
    {
        char* hb = (char*)smalloc(BLOCK - META);   // size + meta == 128KB -> heap
        CHECK(hb != NULL, "128KB-meta served from heap");
        CHECK(_num_allocated_blocks() == 32, "whole order-10 block used, no mmap");
        CHECK(_num_free_blocks() == 31 && _num_free_bytes() == BASE_BYTES - (BLOCK - META),
              "heap stats reflect whole-block use");
        char* mb = (char*)smalloc(BLOCK - META + 1);   // one byte over -> mmap
        CHECK(mb != NULL, "128KB-meta+1 served by mmap");
        CHECK(_num_allocated_blocks() == 33, "mmap adds a block");
        CHECK(_num_allocated_bytes() == BASE_BYTES + (BLOCK - META + 1), "mmap adds its bytes");
        CHECK(_num_free_blocks() == 31, "mmap does not touch free stats");
        char* huge = (char*)smalloc(100000000);
        CHECK(huge != NULL, "10^8 mmap allocation succeeds");
        if (huge) { huge[0] = 1; huge[100000000 - 1] = 2; }
        sfree(huge); sfree(mb); sfree(hb);
        CHECK(at_baseline(), "munmap + merge restore baseline");
        char* sc = (char*)scalloc(1000, 200);      // 200000 bytes -> mmap path
        int z = sc != NULL;
        if (sc) for (int i = 0; i < 200000; i++) if (sc[i] != 0) { z = 0; break; }
        CHECK(z, "scalloc via mmap is zeroed");
        sfree(sc);
        CHECK(at_baseline(), "baseline restored");
    }

    SECTION("scalloc zeroes a dirty reused heap block");
    {
        char* p = (char*)smalloc(40);
        fill(p, 40, 0x21);
        sfree(p);
        char* q = (char*)scalloc(10, 4);
        CHECK(q == p, "same block reused");
        int z = 1;
        for (int i = 0; i < 40; i++) if (q[i] != 0) z = 0;
        CHECK(z, "dirty block zeroed by scalloc");
        sfree(q);
        CHECK(at_baseline(), "baseline restored");
    }

    SECTION("srealloc: reuse, buddy-merge, relocate");
    {
        // (a) fits in place
        char* p = (char*)smalloc(100);             // 100+meta > 128 -> order 1 block (usable 224)
        char* r = (char*)srealloc(p, 200);
        CHECK(r == p, "grow within block size reuses in place");
        sfree(r);
        CHECK(at_baseline(), "baseline restored");

        // (b) single buddy merge in place
        p = (char*)smalloc(40);
        fill(p, 40, 3);
        size_t ab = _num_allocated_blocks();
        r = (char*)srealloc(p, 180);               // needs order 1; buddy at +128 is free
        CHECK(r == p, "grow merges with free buddy in place");
        CHECK(verify(r, 40, 3), "data intact after merge");
        CHECK(_num_allocated_blocks() == ab - 1, "merge removed one block");

        // (b) multi-level merge in place (order 1 -> order 3)
        fill(r, 180, 4);
        ab = _num_allocated_blocks();
        char* r2 = (char*)srealloc(r, 900);        // needs order 3 (usable 1024-meta)
        CHECK(r2 == p, "iterative merge up to order 3 in place");
        CHECK(verify(r2, 180, 4), "data intact after multi-level merge");
        CHECK(_num_allocated_blocks() == ab - 2, "two more merges");
        // returned block must NOT be sitting in a free list
        char* other = (char*)smalloc(900);
        CHECK(other != r2, "merged block is not handed out again");
        sfree(other); sfree(r2);
        CHECK(at_baseline(), "baseline restored");

        // (c) buddy in use -> relocate
        char* a = (char*)smalloc(40);
        char* b = (char*)smalloc(40);              // buddy of a
        fill(a, 40, 6);
        char* c = (char*)srealloc(a, 180);
        CHECK(c != a, "blocked buddy forces relocation");
        CHECK(verify(c, 40, 6), "data copied on relocation");
        CHECK(_num_free_blocks() > 0, "old block was freed");
        sfree(b); sfree(c);
        CHECK(at_baseline(), "baseline restored");

        // srealloc(NULL, n)
        char* n = (char*)srealloc(NULL, 40);
        CHECK(n != NULL, "srealloc(NULL, n) allocates");
        sfree(n);
        CHECK(at_baseline(), "baseline restored");
    }

    SECTION("srealloc on mmap blocks");
    {
        char* m = (char*)smalloc(200000);
        fill(m, 256, 8);
        CHECK(srealloc(m, 200000) == m, "same size reuses the mmap block");
        size_t ab = _num_allocated_blocks();
        char* m2 = (char*)srealloc(m, 250000);
        CHECK(m2 != NULL && m2 != m, "different size always gets a new mmap block");
        CHECK(verify(m2, 256, 8), "data copied across mmap realloc");
        CHECK(_num_allocated_blocks() == ab, "old unmapped, new mapped: net zero blocks");
        sfree(m2);
        CHECK(at_baseline(), "baseline restored");
    }

    SECTION("randomized stress (deterministic seed)");
    {
        enum { SLOTS = 64, OPS = 3000 };
        char* ptr[SLOTS] = {0};
        size_t  sz[SLOTS] = {0};
        unsigned char seed[SLOTS] = {0};
        unsigned long rng = 12345;
        int corrupt = 0, alloc_fail = 0;
        for (int op = 0; op < OPS; op++) {
            rng = rng * 6364136223846793005UL + 1442695040888963407UL;
            int slot = (rng >> 33) % SLOTS;
            if (ptr[slot]) {
                if (!verify(ptr[slot], sz[slot], seed[slot])) corrupt++;
                sfree(ptr[slot]);
                ptr[slot] = 0;
            } else {
                rng = rng * 6364136223846793005UL + 1442695040888963407UL;
                size_t size = (op % 13 == 0) ? 140000 + (rng >> 33) % 50000
                                             : 1 + (rng >> 33) % 2500;
                char* p = (char*)smalloc(size);
                if (!p) { alloc_fail++; continue; }
                ptr[slot] = p; sz[slot] = size; seed[slot] = (unsigned char)(rng >> 17);
                fill(p, size, seed[slot]);
            }
            if (_num_meta_data_bytes() != _num_allocated_blocks() * META) corrupt++;
        }
        for (int i = 0; i < SLOTS; i++) {
            if (!ptr[i]) continue;
            if (!verify(ptr[i], sz[i], seed[i])) corrupt++;
            sfree(ptr[i]);
        }
        CHECK(corrupt == 0, "no data corruption across 3000 random ops");
        CHECK(alloc_fail == 0, "no spurious allocation failures");
        CHECK(at_baseline(), "exact baseline after freeing everything");
    }

    printf("=== malloc_3: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
