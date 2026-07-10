// Comprehensive tests for malloc_2.cpp (Part 2 - Basic Malloc)
// Build: g++ test_comprehensive_2.cpp malloc_2.cpp -o tc2 && ./tc2
//
// State note: the heap never resets, so each section is designed to end with
// zero free blocks - later sections then see deterministic state.
#include <cstdio>
#include <cstring>
#include <sys/resource.h>

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

static void fill(void* p, size_t n, unsigned char seed){
    for (size_t i = 0; i < n; i++) ((unsigned char*)p)[i] = (unsigned char)(seed + i);
}
static int verify(const void* p, size_t n, unsigned char seed){
    for (size_t i = 0; i < n; i++)
        if (((const unsigned char*)p)[i] != (unsigned char)(seed + i)) return 0;
    return 1;
}

int main(){
    printf("=== malloc_2 comprehensive tests ===\n");
    const size_t META = _size_meta_data();

    SECTION("initial state");
    CHECK(_num_free_blocks() == 0 && _num_free_bytes() == 0, "no free blocks initially");
    CHECK(_num_allocated_blocks() == 0 && _num_allocated_bytes() == 0, "no allocated blocks initially");
    CHECK(_num_meta_data_bytes() == 0, "no metadata initially");

    SECTION("failure cases (must not change stats)");
    CHECK(smalloc(0) == NULL, "smalloc(0)");
    CHECK(smalloc(100000001) == NULL, "smalloc(10^8+1)");
    CHECK(scalloc(0, 10) == NULL, "scalloc(0, n)");
    CHECK(scalloc(10, 0) == NULL, "scalloc(n, 0)");
    CHECK(scalloc(100000000, 2) == NULL, "scalloc: num*size = 2*10^8 rejected");
    CHECK(scalloc(4611686018427387905ULL, 4) == NULL, "scalloc: num*size overflow rejected");
    CHECK(srealloc(NULL, 0) == NULL, "srealloc(NULL, 0)");
    CHECK(srealloc(NULL, 100000001) == NULL, "srealloc(NULL, 10^8+1)");
    CHECK(_num_allocated_blocks() == 0 && _num_free_blocks() == 0, "failed calls changed nothing");

    SECTION("basic allocation + stats");
    char* a = (char*)smalloc(100);
    char* b = (char*)smalloc(200);
    char* c = (char*)smalloc(300);
    CHECK(a && b && c, "three allocations succeed");
    CHECK(_num_allocated_blocks() == 3, "allocated blocks = 3");
    CHECK(_num_allocated_bytes() == 600, "allocated bytes = 600 (metadata excluded)");
    CHECK(_num_meta_data_bytes() == 3 * META, "metadata bytes = 3 * meta size");
    CHECK(_num_free_blocks() == 0 && _num_free_bytes() == 0, "nothing free");
    CHECK((char*)b - (char*)a == (long)(100 + META), "blocks contiguous: gap = size + metadata");
    fill(a, 100, 1); fill(b, 200, 2); fill(c, 300, 3);
    CHECK(verify(a, 100, 1) && verify(b, 200, 2) && verify(c, 300, 3), "no data overlap");

    SECTION("sfree semantics");
    sfree(b);
    CHECK(_num_free_blocks() == 1 && _num_free_bytes() == 200, "free middle block: 1 block / 200 bytes");
    sfree(b);
    CHECK(_num_free_blocks() == 1 && _num_free_bytes() == 200, "double free is a no-op");
    sfree(NULL);
    CHECK(_num_free_blocks() == 1, "sfree(NULL) is a no-op");
    char* d = (char*)smalloc(150);
    CHECK(d == b, "freed block reused for smaller request");
    CHECK(_num_free_blocks() == 0 && _num_free_bytes() == 0, "entire block marked used (note 12)");
    CHECK(_num_allocated_blocks() == 3 && _num_allocated_bytes() == 600, "block size never shrinks (note 1)");

    SECTION("first-fit in ascending address order");
    sfree(a); sfree(d);              // two free blocks: a(100) at low addr, d(200) above it
    CHECK(_num_free_blocks() == 2 && _num_free_bytes() == 300, "two blocks free");
    char* e = (char*)smalloc(50);
    CHECK(e == a, "lowest-address fitting block chosen first");
    char* f = (char*)smalloc(180);
    CHECK(f == b, "next request takes the remaining (higher) block");
    CHECK(_num_free_blocks() == 0, "all reused");

    SECTION("scalloc zeroes reused dirty blocks");
    char* g = (char*)smalloc(100);
    fill(g, 100, 0x55);
    sfree(g);
    char* h = (char*)scalloc(50, 2);  // 100 bytes -> must reuse g's block
    CHECK(h == g, "scalloc reuses the freed block");
    int zeroed = 1;
    for (int i = 0; i < 100; i++) if (h[i] != 0) zeroed = 0;
    CHECK(zeroed, "scalloc zeroed the dirty block");
    CHECK(_num_free_blocks() == 0, "section ends with nothing free");

    SECTION("srealloc");
    fill(c, 300, 7);
    char* r = (char*)srealloc(c, 100);
    CHECK(r == c, "shrink reuses the same block");
    CHECK(_num_allocated_blocks() == 4 && _num_free_blocks() == 0, "shrink changed no stats");
    char* r2 = (char*)srealloc(c, 500);            // no free block >= 500 -> new block
    CHECK(r2 && r2 != c, "grow allocates a new block");
    CHECK(verify(r2, 300, 7), "content copied to the new block");
    CHECK(_num_free_blocks() == 1 && _num_free_bytes() == 300, "old block freed after grow");
    char* r3 = (char*)smalloc(250);
    CHECK(r3 == c, "freed old block is reusable");
    char* n1 = (char*)srealloc(NULL, 80);
    CHECK(n1 != NULL, "srealloc(NULL, n) allocates");
    CHECK(_num_allocated_blocks() == 6, "block count consistent");
    fill(n1, 80, 9);
    sfree(r2);                                     // 500-byte block now free
    char* r4 = (char*)srealloc(n1, 400);
    CHECK(r4 == r2, "grow reuses a large-enough free block");
    CHECK(verify(r4, 80, 9), "content copied on grow-reuse");
    CHECK(_num_free_blocks() == 1 && _num_free_bytes() == 80, "n1 freed after successful grow");
    char* n2 = (char*)smalloc(80);
    CHECK(n2 == n1, "reclaim n1's block");
    CHECK(_num_free_blocks() == 0, "section ends with nothing free");

    SECTION("allocation failure: NULL, and oldp untouched");
    fill(r4, 400, 11);
    struct rlimit old_lim, low_lim;
    getrlimit(RLIMIT_DATA, &old_lim);
    low_lim.rlim_cur = 1024 * 1024;
    low_lim.rlim_max = old_lim.rlim_max;
    setrlimit(RLIMIT_DATA, &low_lim);
    size_t blocks_before = _num_allocated_blocks();
    CHECK(smalloc(90000000) == NULL, "smalloc NULL when sbrk fails");
    CHECK(srealloc(r4, 90000000) == NULL, "srealloc NULL when sbrk fails");
    CHECK(verify(r4, 400, 11), "oldp data intact after failed srealloc");
    CHECK(_num_free_blocks() == 0, "oldp NOT freed after failed srealloc");
    CHECK(_num_allocated_blocks() == blocks_before, "failed calls added no blocks");
    setrlimit(RLIMIT_DATA, &old_lim);
    CHECK(smalloc(64) != NULL, "allocation works after limit restored");

    SECTION("final consistency");
    CHECK(_num_meta_data_bytes() == _num_allocated_blocks() * META, "meta bytes == blocks * meta size");
    CHECK(_num_free_bytes() <= _num_allocated_bytes(), "free bytes <= allocated bytes");

    printf("=== malloc_2: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
