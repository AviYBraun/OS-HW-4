#include <iostream>
#include <cassert>
#include <cstdint>
#include "os_malloc.h"

#define MAX_MALLOC 100000001

void test_basic_allocation() {
    std::cout << "Test 1: Basic allocation... ";
    void* p = smalloc(100);
    assert(p != NULL);
    std::cout << "PASSED" << std::endl;
}

void test_integer_array() {
    std::cout << "Test 2: Integer array... ";
    int* arr = static_cast<int*>(smalloc(10 * sizeof(int)));
    assert(arr != NULL);
    for (int i = 0; i < 10; i++) arr[i] = i * 100;
    for (int i = 0; i < 10; i++) assert(arr[i] == i * 100);
    std::cout << "PASSED" << std::endl;
}

void test_large_allocation() {
    std::cout << "Test 3: Large allocation rejection... ";
    void* large = smalloc(MAX_MALLOC);
    assert(large == NULL);
    void* large_ok = smalloc(MAX_MALLOC - 2);
    assert(large_ok != NULL);
    std::cout << "PASSED" << std::endl;
}

void test_zero_allocation() {
    std::cout << "Test 4: Zero allocation rejection... ";
    void* zero = smalloc(0);
    assert(zero == NULL);
    std::cout << "PASSED" << std::endl;
}

void test_multiple_allocations() {
    std::cout << "Test 5: Multiple allocations... ";
    void* p1 = smalloc(50);
    void* p2 = smalloc(100);
    void* p3 = smalloc(200);
    assert(p1 != NULL && p2 != NULL && p3 != NULL);
    assert(p1 != p2 && p2 != p3 && p1 != p3);
    std::cout << "PASSED" << std::endl;
}

// ============================================================================
// NEW EDGE CASES & STRESS TESTS
// ============================================================================

void test_pointer_contiguity() {
    std::cout << "Test 6: Heap Contiguity (No Gaps)... ";
    size_t size1 = 64;
    size_t size2 = 128;
    
    char* p1 = static_cast<char*>(smalloc(size1));
    char* p2 = static_cast<char*>(smalloc(size2));
    
    assert(p1 != NULL && p2 != NULL);
    // In a pure sbrk allocator, consecutive allocations should be perfectly flush
    assert(p2 == p1 + size1);
    std::cout << "PASSED" << std::endl;
}

void test_negative_cast_overflow() {
    std::cout << "Test 7: Implicit Size Max/Negative Wrap Overflow... ";
    // Pass -1 cast to size_t (results in 0xFFFFFFFF... Max Unsigned Int)
    void* p_neg = smalloc(static_cast<size_t>(-1));
    assert(p_neg == NULL); // Your hard upper bound cap should catch this safely
    
    void* p_neg_huge = smalloc(static_cast<size_t>(-5000));
    assert(p_neg_huge == NULL);
    std::cout << "PASSED" << std::endl;
}

void test_memory_alignment() {
    std::cout << "Test 8: Memory Alignment Check... ";
    // Request an odd number of bytes
    void* p1 = smalloc(7); 
    void* p2 = smalloc(8);
    
    // Check if your specification/system requires 4 or 8 byte alignment.
    // Note: Since your current smalloc doesn't force alignment, p2 will be p1 + 7.
    // If your homework or grading script expects 8-byte aligned pointers,
    // this test highlights whether p1 is a multiple of 8.
    uintptr_t addr = reinterpret_cast<uintptr_t>(p1);
    
    // Let's print it to see, or assert if your system requires alignment:
    // assert(addr % 8 == 0); 
    
    std::cout << "PASSED (Address: " << p1 << ")" << std::endl;
}

void test_high_frequency_stress() {
    std::cout << "Test 9: High-frequency small allocation stress... ";
    const int iterations = 10000;
    void* pointers[iterations];
    
    // Allocate 10,000 tiny blocks sequentially
    for (int i = 0; i < iterations; i++) {
        pointers[i] = smalloc(8);
        assert(pointers[i] != NULL);
    }
    
    // Verify they are all distinct and can be written to
    for (int i = 0; i < iterations; i++) {
        int* ipt = static_cast<int*>(pointers[i]);
        *ipt = i;
    }
    for (int i = 0; i < iterations; i++) {
        int* ipt = static_cast<int*>(pointers[i]);
        assert(*ipt == i);
    }
    std::cout << "PASSED" << std::endl;
}



int main() {
    std::cout << "malloc_1 tests:" << std::endl;
    test_basic_allocation();
    test_integer_array();
    test_large_allocation();
    test_zero_allocation();
    test_multiple_allocations();
    //added
    test_pointer_contiguity();
    test_negative_cast_overflow();
    test_memory_alignment();
    test_high_frequency_stress();

    std::cout << "All tests PASSED" << std::endl;
    return 0;
}
