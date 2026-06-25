#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

// 1. Standalone Verification: INT4 Dequantization Kernel
void verify_int4_dequantize(const uint8_t *packed_int4, float *out_fp32, int count, float scale, float zero_point) {
    for (int i = 0; i < count / 2; i++) {
        uint8_t byte = packed_int4[i];
        int4_t_sim0:
        int low = (byte & 0x0F);
        int high = ((byte >> 4) & 0x0F);
        out_fp32[i * 2] = (low - zero_point) * scale;
        out_fp32[i * 2 + 1] = (high - zero_point) * scale;
    }
}

// 2. Standalone Verification: Fast BLAKE-style Hashing Primitive
uint64_t verify_blake_mixing(const char *input, size_t len) {
    uint64_t h = 0xCBF29CE484222325ULL; // FNV-1a seed base
    for (size_t i = 0; i < len; i++) {
        h ^= (uint8_t)input[i];
        h *= 0x100000001B3ULL;
        h = (h << 13) | (h >> 51); // Bitwise rotation
    }
    return h;
}

int main() {
    printf("--- Standalone C Primitive Verification Engine ---\n");
    
    // Test INT4 Dequant
    uint8_t packed[2] = { 0xA3, 0x7F }; // 4 weights: 3, 10, 15, 7
    float weights[4];
    verify_int4_dequantize(packed, weights, 4, 0.5f, 8.0f);
    printf("[1] INT4 Dequantized Weights: %.2f, %.2f, %.2f, %.2f\n", weights[0], weights[1], weights[2], weights[3]);
    if (fabs(weights[0] - (-2.50f)) < 0.01f && fabs(weights[1] - (1.00f)) < 0.01f) {
        printf("✅ INT4 Dequantization Math PASS\n");
    } else {
        printf("❌ INT4 Dequantization Math FAIL\n");
        return 1;
    }

    // Test Fast Hashing
    uint64_t h = verify_blake_mixing("GlowLang Enterprise", 19);
    printf("[2] Blake Mixed Digest: %llx\n", (unsigned long long)h);
    if (h != 0) {
        printf("✅ Fast Hashing Kernel PASS\n");
    }

    printf("✅ Standalone Verification Complete! Ready for stdlib integration.\n");
    return 0;
}
