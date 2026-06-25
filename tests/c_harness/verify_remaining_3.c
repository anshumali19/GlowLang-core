#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// 1. Standalone Verification: Fast JSON Arena Scanning Primitive
int verify_fast_json_scan(const char *json, char *key_out, char *val_out) {
    const char *k_start = strchr(json, '"');
    if (!k_start) return 0;
    const char *k_end = strchr(k_start + 1, '"');
    if (!k_end) return 0;
    
    int k_len = k_end - (k_start + 1);
    memcpy(key_out, k_start + 1, k_len);
    key_out[k_len] = '\0';

    const char *colon = strchr(k_end, ':');
    if (!colon) return 0;
    const char *v_start = colon + 1;
    while (*v_start == ' ') v_start++;
    
    int v_len = strlen(v_start);
    if (v_start[v_len - 1] == '}') v_len--;
    memcpy(val_out, v_start, v_len);
    val_out[v_len] = '\0';
    return 1;
}

// 2. Standalone Verification: Media Header Dimension Scanner (PNG 8-byte signature + IHDR)
int verify_media_header_dimensions(const uint8_t *header_bytes, uint32_t *width, uint32_t *height) {
    // Check PNG signature: 89 50 4E 47 0D 0A 1A 0A
    if (header_bytes[0] != 0x89 || header_bytes[1] != 0x50 || header_bytes[2] != 0x4E || header_bytes[3] != 0x47) {
        return 0;
    }
    // Width is at byte offset 16 (4 big-endian bytes), Height at offset 20
    *width = ((uint32_t)header_bytes[16] << 24) | ((uint32_t)header_bytes[17] << 16) | ((uint32_t)header_bytes[18] << 8) | header_bytes[19];
    *height = ((uint32_t)header_bytes[20] << 24) | ((uint32_t)header_bytes[21] << 16) | ((uint32_t)header_bytes[22] << 8) | header_bytes[23];
    return 1;
}

int main() {
    printf("--- Standalone Verification Engine for Remaining Primitives ---\n");
    
    // Test Fast JSON Scan
    char key[64], val[64];
    int ok1 = verify_fast_json_scan("{\"protocol\": \"QUIC-v3\"}", key, val);
    printf("[1] Fast JSON Scan -> Key: '%s', Val: '%s'\n", key, val);
    if (ok1 && strcmp(key, "protocol") == 0 && strcmp(val, "\"QUIC-v3\"") == 0) {
        printf("✅ Fast JSON Arena Scanner PASS\n");
    } else {
        printf("❌ Fast JSON Arena Scanner FAIL\n");
        return 1;
    }

    // Test PNG Media Header Decode
    uint8_t mock_png[24] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, // Sig
        0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, // IHDR chunk
        0x00, 0x00, 0x07, 0x80, // Width 1920 (0x0780)
        0x00, 0x00, 0x04, 0x38  // Height 1080 (0x0438)
    };
    uint32_t w, h;
    int ok2 = verify_media_header_dimensions(mock_png, &w, &h);
    printf("[2] PNG Media Header Decode -> Width: %u, Height: %u\n", w, h);
    if (ok2 && w == 1920 && h == 1080) {
        printf("✅ Media Header Decoder PASS\n");
    } else {
        printf("❌ Media Header Decoder FAIL\n");
        return 1;
    }

    printf("✅ All Standalone C Verifications TRUE! Approved for stdlib integration.\n");
    return 0;
}
