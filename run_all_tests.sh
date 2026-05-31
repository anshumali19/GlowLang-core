#!/bin/bash
BIN="./bin/glowlang"
TEST_DIR="tests"
PASSED=0
FAILED=0
TOTAL=0

for test_file in "$TEST_DIR"/*.glow; do
    echo "Running $test_file..."
    timeout 30 $BIN "$test_file" > /dev/null 2>&1
    RET=$?
    if [ $RET -eq 0 ]; then
        echo "✅ PASS"
        ((PASSED++))
    elif [ $RET -eq 124 ]; then
        echo "⏳ TIMEOUT"
        ((FAILED++))
    else
        echo "❌ FAIL (Code: $RET)"
        ((FAILED++))
    fi
    ((TOTAL++))
done

echo "-----------------------"
echo "Total: $TOTAL"
echo "Passed: $PASSED"
echo "Failed: $FAILED"
