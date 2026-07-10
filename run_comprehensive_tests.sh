#!/bin/bash
# Build and run the comprehensive test suite for all three parts.
cd "$(dirname "$0")"
OUT=${TMPDIR:-/tmp}/malloc_comp_tests
mkdir -p "$OUT"
fail=0
for i in 1 2 3; do
    echo "==================================================="
    if ! g++ -Wall -Wno-unused-result "test_comprehensive_$i.cpp" "malloc_$i.cpp" -o "$OUT/tc$i"; then
        echo "malloc_$i: BUILD FAILED"; fail=1; continue
    fi
    timeout 60 "$OUT/tc$i" || fail=1
done
echo "==================================================="
[ $fail -eq 0 ] && echo "ALL SUITES PASSED" || echo "SOME TESTS FAILED"
exit $fail
