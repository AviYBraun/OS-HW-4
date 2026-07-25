#!/bin/bash
# Build and run the comprehensive + extreme test suites for all parts.
cd "$(dirname "$0")"
OUT=${TMPDIR:-/tmp}/malloc_comp_tests
mkdir -p "$OUT"
fail=0
run() {  # run <test-src> <malloc-src> <bin-name>
    echo "==================================================="
    if ! g++ -Wall -Wno-unused-result "$1" "$2" -o "$OUT/$3"; then
        echo "$1: BUILD FAILED"; fail=1; return
    fi
    timeout 120 "$OUT/$3" || fail=1
}
run test_malloc_1.cpp        malloc_1.cpp ct1   # course-provided basic tests
run test_malloc_2.cpp        malloc_2.cpp ct2
run test_malloc_3.cpp        malloc_3.cpp ct3
run test_comprehensive_1.cpp malloc_1.cpp tc1
run test_comprehensive_2.cpp malloc_2.cpp tc2
run test_comprehensive_3.cpp malloc_3.cpp tc3
run test_extreme_2.cpp       malloc_2.cpp tx2
run test_extreme_3.cpp       malloc_3.cpp tx3
echo "==================================================="
[ $fail -eq 0 ] && echo "ALL SUITES PASSED" || echo "SOME TESTS FAILED"
exit $fail
