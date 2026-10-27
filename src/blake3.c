/* BLAKE3 hash (portable, single-threaded), following the reference
   implementation in the BLAKE3 specification. Used by crypto.blake3. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define BLAKE3_OUT_LEN 32
#define BLAKE3_BLOCK_LEN 64
#define BLAKE3_CHUNK_LEN 1024

enum { CHUNK_START = 1, CHUNK_END = 2, PARENT = 4, ROOT = 8 };

static const uint32_t IV[8] = {
    0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
    0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19,
};
static const uint8_t MSG_PERMUTATION[16] = {2, 6, 3, 10, 7, 0, 4, 13, 1, 11, 12, 5, 9, 14, 15, 8};

static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

static void g(uint32_t s[16], int a, int b, int c, int d, uint32_t mx, uint32_t my) {
    s[a] = s[a] + s[b] + mx; s[d] = rotr(s[d] ^ s[a], 16);
    s[c] = s[c] + s[d];      s[b] = rotr(s[b] ^ s[c], 12);
    s[a] = s[a] + s[b] + my; s[d] = rotr(s[d] ^ s[a], 8);
    s[c] = s[c] + s[d];      s[b] = rotr(s[b] ^ s[c], 7);
}

static void round_fn(uint32_t s[16], const uint32_t m[16]) {
    g(s, 0, 4, 8, 12, m[0], m[1]);   g(s, 1, 5, 9, 13, m[2], m[3]);
    g(s, 2, 6, 10, 14, m[4], m[5]);  g(s, 3, 7, 11, 15, m[6], m[7]);
    g(s, 0, 5, 10, 15, m[8], m[9]);  g(s, 1, 6, 11, 12, m[10], m[11]);
    g(s, 2, 7, 8, 13, m[12], m[13]); g(s, 3, 4, 9, 14, m[14], m[15]);
}

static void compress(const uint32_t cv[8], const uint32_t block[16], uint64_t counter,
                     uint32_t block_len, uint32_t flags, uint32_t out[16]) {
    uint32_t s[16] = {
        cv[0], cv[1], cv[2], cv[3], cv[4], cv[5], cv[6], cv[7],
        IV[0], IV[1], IV[2], IV[3],
        (uint32_t)counter, (uint32_t)(counter >> 32), block_len, flags,
    };
    uint32_t m[16], t[16];
    memcpy(m, block, sizeof(m));
    for (int r = 0; r < 7; r++) {
        round_fn(s, m);
        if (r < 6) {
            for (int i = 0; i < 16; i++) t[i] = m[MSG_PERMUTATION[i]];
            memcpy(m, t, sizeof(m));
        }
    }
    for (int i = 0; i < 8; i++) { s[i] ^= s[i + 8]; s[i + 8] ^= cv[i]; }
    memcpy(out, s, sizeof(s));
}

static void words_from_le(const uint8_t *bytes, size_t n_words, uint32_t *words) {
    for (size_t i = 0; i < n_words; i++)
        words[i] = (uint32_t)bytes[4 * i] | ((uint32_t)bytes[4 * i + 1] << 8) |
                   ((uint32_t)bytes[4 * i + 2] << 16) | ((uint32_t)bytes[4 * i + 3] << 24);
}

typedef struct {
    uint32_t input_cv[8];
    uint32_t block[16];
    uint64_t counter;
    uint32_t block_len;
    uint32_t flags;
} Output;

static void output_cv(const Output *o, uint32_t cv[8]) {
    uint32_t out[16];
    compress(o->input_cv, o->block, o->counter, o->block_len, o->flags, out);
    memcpy(cv, out, 8 * sizeof(uint32_t));
}

static void output_root_bytes(const Output *o, uint8_t *dst, size_t len) {
    uint64_t block_counter = 0;
    while (len > 0) {
        uint32_t words[16];
        compress(o->input_cv, o->block, block_counter, o->block_len, o->flags | ROOT, words);
        for (int w = 0; w < 16 && len > 0; w++)
            for (int b = 0; b < 4 && len > 0; b++, len--) *dst++ = (uint8_t)(words[w] >> (8 * b));
        block_counter++;
    }
}

typedef struct {
    uint32_t cv[8];
    uint64_t chunk_counter;
    uint8_t block[BLAKE3_BLOCK_LEN];
    uint32_t block_len;
    uint32_t blocks_compressed;
    uint32_t flags;
} ChunkState;

static void chunk_init(ChunkState *c, const uint32_t key[8], uint64_t counter, uint32_t flags) {
    memcpy(c->cv, key, 8 * sizeof(uint32_t));
    c->chunk_counter = counter;
    memset(c->block, 0, sizeof(c->block));
    c->block_len = 0;
    c->blocks_compressed = 0;
    c->flags = flags;
}

static size_t chunk_len(const ChunkState *c) { return (size_t)BLAKE3_BLOCK_LEN * c->blocks_compressed + c->block_len; }
static uint32_t chunk_start_flag(const ChunkState *c) { return c->blocks_compressed == 0 ? CHUNK_START : 0; }

static void chunk_update(ChunkState *c, const uint8_t *input, size_t len) {
    while (len > 0) {
        if (c->block_len == BLAKE3_BLOCK_LEN) {
            uint32_t words[16], out[16];
            words_from_le(c->block, 16, words);
            compress(c->cv, words, c->chunk_counter, BLAKE3_BLOCK_LEN, c->flags | chunk_start_flag(c), out);
            memcpy(c->cv, out, 8 * sizeof(uint32_t));
            c->blocks_compressed++;
            memset(c->block, 0, sizeof(c->block));
            c->block_len = 0;
        }
        size_t take = BLAKE3_BLOCK_LEN - c->block_len;
        if (take > len) take = len;
        memcpy(c->block + c->block_len, input, take);
        c->block_len += (uint32_t)take;
        input += take;
        len -= take;
    }
}

static Output chunk_output(const ChunkState *c) {
    Output o;
    memcpy(o.input_cv, c->cv, sizeof(o.input_cv));
    words_from_le(c->block, 16, o.block);
    o.counter = c->chunk_counter;
    o.block_len = c->block_len;
    o.flags = c->flags | chunk_start_flag(c) | CHUNK_END;
    return o;
}

static Output parent_output(const uint32_t left[8], const uint32_t right[8], const uint32_t key[8], uint32_t flags) {
    Output o;
    memcpy(o.input_cv, key, sizeof(o.input_cv));
    memcpy(o.block, left, 8 * sizeof(uint32_t));
    memcpy(o.block + 8, right, 8 * sizeof(uint32_t));
    o.counter = 0;
    o.block_len = BLAKE3_BLOCK_LEN;
    o.flags = PARENT | flags;
    return o;
}

/* Hash `len` bytes of `input` into `out_len` bytes of `out`. */
void glow_blake3(const void *input, size_t len, uint8_t *out, size_t out_len) {
    const uint8_t *in = (const uint8_t*)input;
    uint32_t cv_stack[54][8];
    int stack_len = 0;
    ChunkState chunk;
    chunk_init(&chunk, IV, 0, 0);
    while (len > 0) {
        if (chunk_len(&chunk) == BLAKE3_CHUNK_LEN) {
            uint32_t chunk_cv[8];
            Output o = chunk_output(&chunk);
            output_cv(&o, chunk_cv);
            uint64_t total_chunks = chunk.chunk_counter + 1;
            /* Merge completed subtrees: one parent per trailing zero bit. */
            while ((total_chunks & 1) == 0) {
                Output p = parent_output(cv_stack[--stack_len], chunk_cv, IV, 0);
                output_cv(&p, chunk_cv);
                total_chunks >>= 1;
            }
            memcpy(cv_stack[stack_len++], chunk_cv, sizeof(chunk_cv));
            chunk_init(&chunk, IV, chunk.chunk_counter + 1, 0);
        }
        size_t take = BLAKE3_CHUNK_LEN - chunk_len(&chunk);
        if (take > len) take = len;
        chunk_update(&chunk, in, take);
        in += take;
        len -= take;
    }
    Output o = chunk_output(&chunk);
    for (int i = stack_len - 1; i >= 0; i--) {
        uint32_t right[8];
        output_cv(&o, right);
        o = parent_output(cv_stack[i], right, IV, 0);
    }
    output_root_bytes(&o, out, out_len);
}
