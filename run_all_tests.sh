#!/bin/bash
# Kept for existing scripts and docs: runs the test suite, comparing every
# test's output with tests/expected/ (see scripts/run-tests.sh).
exec "$(dirname "$0")/scripts/run-tests.sh" "$@"
