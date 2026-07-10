// Comprehensive tests for malloc_1.cpp (Part 1 - Naive Malloc)
// Build: g++ test_comprehensive_1.cpp malloc_1.cpp -o tc1 && ./tc1
#include <cstdio>
#include <cstring>
#include <sys/resource.h>

void* smalloc(size_t size);

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FAIL: %s (line %d)\n", name, __LINE__); } \
} while(0)
#define SECTION(s) printf("-- %s\n", s)

int main(){
    printf("=== malloc_1 comprehensive tests ===\n");

    SECTION("failure cases");
    CHECK(smalloc(0) == NULL, "smalloc(0) returns NULL");
    CHECK(smalloc(100000001) == NULL, "smalloc(10^8 + 1) returns NULL");

    SECTION("basic allocation");
    char* p1 = (char*)smalloc(1);
    char* p2 = (char*)smalloc(1000);
    char* p3 = (char*)smalloc(8);
    CHECK(p1 && p2 && p3, "three allocations succeed");
    CHECK(p1 != p2 && p2 != p3 && p1 != p3, "pointers are distinct");
    CHECK(p2 >= p1 + 1, "p2 does not overlap p1");
    CHECK(p3 >= p2 + 1000, "p3 does not overlap p2");
    CHECK(p2 > p1 && p3 > p2, "heap grows upward (ascending addresses)");

    SECTION("data integrity across allocations");
    p1[0] = 'A';
    memset(p2, 0xB7, 1000);
    memset(p3, 0x3C, 8);
    int intact = (p1[0] == 'A');
    for (int i = 0; i < 1000; i++) if ((unsigned char)p2[i] != 0xB7) intact = 0;
    for (int i = 0; i < 8; i++)    if ((unsigned char)p3[i] != 0x3C) intact = 0;
    CHECK(intact, "no allocation overlaps another's data");

    SECTION("boundary: exactly 10^8 bytes is allowed");
    char* big = (char*)smalloc(100000000);
    CHECK(big != NULL, "smalloc(10^8) succeeds");
    if (big) { big[0] = 1; big[100000000 - 1] = 2;
        CHECK(big[0] == 1 && big[100000000 - 1] == 2, "10^8 block is writable end to end"); }

    SECTION("sbrk failure returns NULL");
    struct rlimit old_lim, low_lim;
    getrlimit(RLIMIT_DATA, &old_lim);
    low_lim.rlim_cur = 1024 * 1024;           // 1MB - far below current usage
    low_lim.rlim_max = old_lim.rlim_max;
    setrlimit(RLIMIT_DATA, &low_lim);
    CHECK(smalloc(50000000) == NULL, "smalloc returns NULL when sbrk fails");
    setrlimit(RLIMIT_DATA, &old_lim);
    CHECK(smalloc(64) != NULL, "allocation works again after limit restored");

    printf("=== malloc_1: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
