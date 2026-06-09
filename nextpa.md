# GlowLang: Next Phases (Hardware Acceleration)

The core engine is now completely capable of executing an Autograd graph, broadcasting tensor operations, memory-pooling allocations, and scaling operations across POSIX threads. The abstraction layer for hardware routing (`.to_device("cuda")`) has been implemented. 

The next phases should be executed on a machine equipped with an Nvidia GPU and the CUDA Toolkit.

## Phase 6 — Native GPU Hardware Acceleration
This phase replaces the CPU fallbacks in our mock abstraction layer with actual PCIe VRAM data transfers and hardware-level math kernels.

### 1. Build System & CUDA Toolkit Integration
*   Ensure the `nvcc` compiler is installed.
*   Update the `Makefile` to link against Nvidia's runtime libraries (`-lcudart`, `-lcublas`).
*   Configure the build system to compile both standard `.c` files and new `.cu` CUDA kernels.

### 2. Physical Memory Transfers
*   Modify `native_tensor_to` in `src/stdlib.c`.
*   When `device == "cuda"`, intercept the memory allocation.
*   Instead of `tensor_malloc` (System RAM), invoke `cudaMalloc(&device_ptr, size)` to reserve GPU VRAM.
*   Use `cudaMemcpy(device_ptr, host_ptr, size, cudaMemcpyHostToDevice)` to physically push the dataset across the PCIe bus into the GPU memory.

### 3. Writing Custom CUDA Kernels (`tensor_ops.cu`)
*   Create a new directory `src/cuda/`.
*   Write pure CUDA C++ kernels for element-wise operations (`Add`, `Sub`, `Mul`, `ReLU`).
*   **Example:** A `relu_kernel<<<blocks, threads>>>` that executes `max(0, x)` natively on thousands of GPU cores simultaneously.

### 4. High-Performance Matrix Multiplication
*   Modify `native_tensor_matmul` in `src/stdlib.c`.
*   Add a conditional check: `if (strcmp(pa->device, "cuda") == 0)`.
*   If true, skip the CPU POSIX `pthreads` & AVX2 loop. Instead, invoke Nvidia's Highly Optimized Basic Linear Algebra Subprograms (`cuBLAS`).
*   Specifically, bind the tensors to `cublasDgemm` to calculate the dot products directly on the GPU architecture.

## Phase 7 — Advanced Kernel Scheduling & Asynchronous Execution
*   Implement **CUDA Streams** to allow the CPU to queue multiple tensor operations without blocking the main GlowLang thread.
*   Use `cudaDeviceSynchronize()` only when absolutely necessary (e.g., when pulling data back to the CPU via `.to_device("cpu")` to print results to the terminal).
*   Implement a **JIT (Just-In-Time) Kernel Compiler**: Allow GlowLang scripts to write custom parallel logic that gets compiled into PTX assembly and executed on the GPU at runtime.
