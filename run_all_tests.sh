#!/bin/bash
BIN="${GLOWLANG_BIN:-./bin/glowlang}"
TEST_DIR="tests"
PASSED=0
FAILED=0
TOTAL=0

for test_file in "$TEST_DIR"/*.glow; do
    echo "Running $test_file..."
    timeout 30 $BIN "$test_file" > /dev/null 2>&1
    RET=$?
    EXPECTED=0
    if [[ "$test_file" == *"test_std.glow"* ]]; then
        EXPECTED=42
    elif [[ "$test_file" == *"test_div0_stack.glow"* || "$test_file" == *"test_error_stack.glow"* || "$test_file" == *"test_jit_div0.glow"* || "$test_file" == *"test_throw_stack.glow"* || "$test_file" == *"loop_rec_test.glow"* ]]; then
        EXPECTED=1
    fi

    if [ $RET -eq $EXPECTED ]; then
        echo "✅ PASS"
        ((PASSED++))
    elif [ $RET -eq 124 ]; then
        echo "⏳ TIMEOUT"
        ((FAILED++))
    else
        echo "❌ FAIL (Code: $RET, Expected: $EXPECTED)"
        ((FAILED++))
    fi
    ((TOTAL++))
done

echo "-----------------------"
echo "Total: $TOTAL"
echo "Passed: $PASSED"
echo "Failed: $FAILED"
