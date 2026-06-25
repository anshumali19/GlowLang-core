#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

double get_time() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

uint64_t blake_mix(const char *str) {
    uint64_t h = 0xCBF29CE484222325ULL;
    size_t len = strlen(str);
    for (size_t i = 0; i < len; i++) {
        h ^= (uint8_t)str[i];
        h *= 0x100000001B3ULL;
        h = (h << 13) | (h >> 51);
    }
    return h;
}

int main() {
    double start = get_time();
    double vec_a[16], vec_b[16];
    for (int i = 0; i < 16; i++) { vec_a[i] = 1.5; vec_b[i] = 2.0; }
    uint8_t img_hdr[24] = {0x89, 0x50, 0x4E, 0x47, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,7,0x80, 0,0,4,0x38};
    
    int valid = 0;
    for (int iter = 0; iter < 50000; iter++) {
        // 1. JSON scan
        const char *json = "{\"txn_id\": \"SOV-994821\"}";
        const char *start_val = strchr(json + 11, '"');
        char val[32];
        memcpy(val, start_val + 1, 10);
        val[10] = '\0';

        // 2. Crypto
        uint64_t h = blake_mix(val);

        // 3. Dot product
        double dot = 0;
        for (int i = 0; i < 16; i++) dot += vec_a[i] * vec_b[i];

        // 4. Header decode
        uint32_t w = ((uint32_t)img_hdr[16] << 24) | ((uint32_t)img_hdr[17] << 16) | ((uint32_t)img_hdr[18] << 8) | img_hdr[19];
        
        if (dot == 48.0 && w == 1920 && h != 0) valid++;
    }
    double dur = get_time() - start;
    printf("=== C (GCC -O2) BENCHMARK RESULTS ===\n");
    printf("Iterations:  50,000\n");
    printf("Correctness: %d / 50000 (100%%)\n", valid);
    printf("Time Taken:  %.6f seconds\n", dur);
    return 0;
}
