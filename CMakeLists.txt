cmake_minimum_required(VERSION 3.14)
project(GlowLang C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED True)

if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    # Also disable some specific type conversion warnings to clear the build output
    add_compile_options(/wd4267 /wd4244 /wd4133 /wd4013 /wd4102)
endif()

# Set common compiler flags like in Makefile
if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_C_COMPILER_ID MATCHES "Clang")
    # Enable optimization by default for release
    set(CMAKE_C_FLAGS_RELEASE "-O2")
    set(CMAKE_C_FLAGS_DEBUG "-O1 -g -fsanitize=address -fno-omit-frame-pointer")

    # Only apply AVX2 and SSE4.2 if on x86_64 to remain cross-platform compatible
    include(CheckCCompilerFlag)
    check_c_compiler_flag("-mavx2" COMPILER_SUPPORTS_AVX2)
    check_c_compiler_flag("-msse4.2" COMPILER_SUPPORTS_SSE42)
    if(COMPILER_SUPPORTS_AVX2 AND COMPILER_SUPPORTS_SSE42)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mavx2 -msse4.2")
    endif()
endif()

# Find external dependencies
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED)
include(CheckLanguage)
check_language(CUDA)
if(CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    find_package(CUDAToolkit)
endif()

# X11 is specific to Unix (and not macOS)
if(UNIX AND NOT APPLE)
    find_package(X11)
endif()

# Find Flex and Bison
find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)

# Generate Lexer and Parser
BISON_TARGET(GlowParser src/glowlang.y ${CMAKE_CURRENT_BINARY_DIR}/glowlang.tab.c COMPILE_FLAGS "-Wnone -d")
FLEX_TARGET(GlowLexer src/glowlang.l ${CMAKE_CURRENT_BINARY_DIR}/lex.yy.c)
ADD_FLEX_BISON_DEPENDENCY(GlowLexer GlowParser)

# Main Executable Sources
set(GLOW_SOURCES
    ${BISON_GlowParser_OUTPUTS}
    ${FLEX_GlowLexer_OUTPUTS}
    src/lsp.c
    src/arena.c
    src/jit.c
    src/opt.c
    src/compiler.c
    src/vm.c
    src/gc.c
    src/stdlib.c
    src/bigint.c
)

if(CUDAToolkit_FOUND)
    add_compile_definitions(GLOW_USE_CUDA)
    list(APPEND GLOW_SOURCES src/cuda/tensor_ops.cu)
endif()

# Output binaries directly into the bin/ folder like the Makefile does
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# Target: glowlang
add_executable(glowlang ${GLOW_SOURCES})

# Include directories
target_include_directories(glowlang PRIVATE src ${CMAKE_CURRENT_BINARY_DIR})

# Libraries to link
target_link_libraries(glowlang PRIVATE
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
    SQLite::SQLite3
)

if(CUDAToolkit_FOUND)
    target_link_libraries(glowlang PRIVATE CUDA::cudart CUDA::cublas)
endif()

if(NOT MSVC)
    target_link_libraries(glowlang PRIVATE m)
else()
    target_link_libraries(glowlang PRIVATE ws2_32)
endif()

if(UNIX AND NOT APPLE AND X11_FOUND)
    target_link_libraries(glowlang PRIVATE X11::X11)
endif()

# Target: gdu (from tools/gdu)
add_executable(gdu tools/gdu/gdu.c)
target_link_libraries(gdu PRIVATE SQLite::SQLite3 OpenSSL::Crypto OpenSSL::SSL)

# Other tools from Makefile
add_executable(glow_profiler tools/profiler/glow_profiler.c)
add_executable(glow_inspector tools/inspector/glow_inspector.c)
add_executable(glow_debugger tools/debugger/glow_debugger.c)
