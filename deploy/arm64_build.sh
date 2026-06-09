#!/bin/bash
# GlowLang ARM64 Cross-Compilation Script
# Builds GlowLang binary for ARM64/aarch64 targets
#
# Prerequisites:
#   sudo apt install gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu
#
# Usage:
#   ./arm64_build.sh                # Build for ARM64
#   ./arm64_build.sh --test         # Build and run tests via QEMU
#   ./arm64_build.sh --static       # Static build for ARM64

set -euo pipefail

# Configuration
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_DIR="${PROJECT_ROOT}/src"
BIN_DIR="${PROJECT_ROOT}/bin"
BUILD_DIR="${PROJECT_ROOT}/build/arm64"

CROSS_CC="aarch64-linux-gnu-gcc"
CROSS_STRIP="aarch64-linux-gnu-strip"
OUTPUT_BINARY="${BIN_DIR}/glowlang-arm64"

# Compiler flags
CFLAGS="-O2 -Wall -Wextra -DNDEBUG"
LDFLAGS="-lm -lpthread"
STATIC_FLAG=""

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_info()  { echo -e "${GREEN}[INFO]${NC}  $1"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC}  $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# Check prerequisites
check_toolchain() {
    log_info "Checking ARM64 cross-compilation toolchain..."

    if ! command -v ${CROSS_CC} &> /dev/null; then
        log_error "Cross-compiler not found: ${CROSS_CC}"
        log_info "Install with: sudo apt install gcc-aarch64-linux-gnu"
        exit 1
    fi

    log_info "Toolchain found: $(${CROSS_CC} --version | head -1)"
}

# Parse arguments
parse_args() {
    RUN_TESTS=false
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --test)
                RUN_TESTS=true
                shift
                ;;
            --static)
                STATIC_FLAG="-static"
                log_info "Static build enabled"
                shift
                ;;
            --debug)
                CFLAGS="-g -O0 -Wall -Wextra -DDEBUG"
                log_info "Debug build enabled"
                shift
                ;;
            *)
                log_error "Unknown option: $1"
                echo "Usage: $0 [--test] [--static] [--debug]"
                exit 1
                ;;
        esac
    done
}

# Build
build_arm64() {
    log_info "Building GlowLang for ARM64..."

    mkdir -p "${BUILD_DIR}"
    mkdir -p "${BIN_DIR}"

    # Find all C source files
    local src_files
    src_files=$(find "${SRC_DIR}" -name "*.c" -type f 2>/dev/null | sort)

    if [ -z "${src_files}" ]; then
        log_error "No source files found in ${SRC_DIR}"
        exit 1
    fi

    local file_count
    file_count=$(echo "${src_files}" | wc -l)
    log_info "Compiling ${file_count} source files..."

    # Compile each file
    local obj_files=""
    for src in ${src_files}; do
        local basename
        basename=$(basename "${src}" .c)
        local obj="${BUILD_DIR}/${basename}.o"
        log_info "  CC ${basename}.c"
        ${CROSS_CC} ${CFLAGS} -c "${src}" -o "${obj}" -I"${SRC_DIR}"
        obj_files="${obj_files} ${obj}"
    done

    # Link
    log_info "Linking..."
    ${CROSS_CC} ${CFLAGS} ${STATIC_FLAG} ${obj_files} -o "${OUTPUT_BINARY}" ${LDFLAGS}

    # Strip in release mode
    if [[ "${CFLAGS}" == *"-DNDEBUG"* ]]; then
        log_info "Stripping debug symbols..."
        ${CROSS_STRIP} "${OUTPUT_BINARY}" 2>/dev/null || true
    fi

    local size
    size=$(stat -c%s "${OUTPUT_BINARY}" 2>/dev/null || echo "unknown")
    log_info "Build complete: ${OUTPUT_BINARY} (${size} bytes)"

    # Show file info
    file "${OUTPUT_BINARY}"
}

# Test with QEMU
run_tests() {
    if [ "${RUN_TESTS}" = true ]; then
        log_info "Running tests via QEMU user-mode emulation..."

        if ! command -v qemu-aarch64 &> /dev/null; then
            log_warn "qemu-aarch64 not found. Install with: sudo apt install qemu-user"
            return 1
        fi

        # Run a simple test
        echo 'show "ARM64 GlowLang works!"' > "${BUILD_DIR}/test_arm64.glow"
        if qemu-aarch64 -L /usr/aarch64-linux-gnu "${OUTPUT_BINARY}" "${BUILD_DIR}/test_arm64.glow"; then
            log_info "ARM64 test PASSED!"
        else
            log_error "ARM64 test FAILED!"
            return 1
        fi
    fi
}

# Main
main() {
    echo "========================================="
    echo "  GlowLang ARM64 Cross-Compilation"
    echo "========================================="
    echo ""

    parse_args "$@"
    check_toolchain
    build_arm64
    run_tests

    echo ""
    log_info "ARM64 build complete!"
    echo "========================================="
}

main "$@"
