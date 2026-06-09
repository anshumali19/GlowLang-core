with open("src/stdlib.c", "r") as f:
    content = f.read()

import re

# 1. string_copy
content = re.sub(
    r'(for \(; i \+ 32 <= len; i \+= 32\) \{\s*__m256i v = _mm256_loadu_si256[^\}]+\})',
    r'#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)\n\1\n#endif',
    content
)

# 2. PackedArray add pd
content = re.sub(
    r'(for \(; i <= sz - 4; i \+= 4\) \{\s*_mm256_storeu_pd[^;]+;\s*\})',
    r'#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)\n\1\n#endif',
    content
)

# 3. PackedArray add si256
content = re.sub(
    r'(for \(; i <= sz - 4; i \+= 4\) \{\s*_mm256_storeu_si256[^;]+;\s*\})',
    r'#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)\n\1\n#endif',
    content
)

# 4. Dot product sum_vec init, loop, store
content = re.sub(
    r'(__m256d sum_vec = _mm256_setzero_pd\(\);\s*int i = 0;\s*for \(; i <= sz - 4; i \+= 4\) \{\s*sum_vec = _mm256_add_pd[^\}]+\}\s*double temp\[4\];\s*_mm256_storeu_pd\(temp, sum_vec\);\s*double sum = temp\[0\] \+ temp\[1\] \+ temp\[2\] \+ temp\[3\];)',
    r'double sum = 0;\nint i = 0;\n#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)\n__m256d sum_vec = _mm256_setzero_pd();\nfor (; i <= sz - 4; i += 4) {\n    sum_vec = _mm256_add_pd(sum_vec, _mm256_mul_pd(_mm256_loadu_pd(&da[i]), _mm256_loadu_pd(&db[i])));\n}\ndouble temp[4];\n_mm256_storeu_pd(temp, sum_vec);\nsum = temp[0] + temp[1] + temp[2] + temp[3];\n#endif',
    content
)

# 5. Matrix mul
content = re.sub(
    r'(__m256d sum_vec = _mm256_setzero_pd\(\);\s*int k = 0;\s*for \(; k <= cA - 4; k \+= 4\) \{[^\}]+\}\s*double temp\[4\];\s*_mm256_storeu_pd\(temp, sum_vec\);\s*double sum = temp\[0\] \+ temp\[1\] \+ temp\[2\] \+ temp\[3\];)',
    r'double sum = 0;\nint k = 0;\n#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)\n__m256d sum_vec = _mm256_setzero_pd();\nfor (; k <= cA - 4; k += 4) {\n    __m256d va = _mm256_loadu_pd(&da[i * cA + k]);\n    __m256d vb = _mm256_set_pd(db[(k+3) * cB + j], db[(k+2) * cB + j], db[(k+1) * cB + j], db[k * cB + j]);\n    sum_vec = _mm256_add_pd(sum_vec, _mm256_mul_pd(va, vb));\n}\ndouble temp[4];\n_mm256_storeu_pd(temp, sum_vec);\nsum = temp[0] + temp[1] + temp[2] + temp[3];\n#endif',
    content
)

with open("src/stdlib.c", "w") as f:
    f.write(content)
