#include <cuda_runtime.h>
#include <cublas_v2.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK_CUDA(call) \
do { \
    cudaError_t err = call; \
    if (err != cudaSuccess) { \
        fprintf(stderr, "CUDA error at %s:%d: %s\n", __FILE__, __LINE__, cudaGetErrorString(err)); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

#define CHECK_CUBLAS(call) \
do { \
    cublasStatus_t status = call; \
    if (status != CUBLAS_STATUS_SUCCESS) { \
        fprintf(stderr, "cuBLAS error at %s:%d: %d\n", __FILE__, __LINE__, status); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

extern "C" {

void* cuda_tensor_malloc(size_t size) {
    void* ptr;
    CHECK_CUDA(cudaMalloc(&ptr, size));
    return ptr;
}

void cuda_tensor_free(void* ptr) {
    if (ptr) {
        CHECK_CUDA(cudaFree(ptr));
    }
}

void cuda_tensor_to_device(void* dst, const void* src, size_t size) {
    CHECK_CUDA(cudaMemcpy(dst, src, size, cudaMemcpyHostToDevice));
}

void cuda_tensor_to_host(void* dst, const void* src, size_t size) {
    CHECK_CUDA(cudaMemcpy(dst, src, size, cudaMemcpyDeviceToHost));
}

void cuda_tensor_matmul(const double* A_d, int rA, int cA, const double* B_d, int cB, double* C_d) {
    cublasHandle_t handle;
    CHECK_CUBLAS(cublasCreate(&handle));
    
    double alpha = 1.0;
    double beta = 0.0;
    
    CHECK_CUBLAS(cublasDgemm(handle,
                             CUBLAS_OP_N, CUBLAS_OP_N,
                             cB, rA, cA,
                             &alpha,
                             B_d, cB,
                             A_d, cA,
                             &beta,
                             C_d, cB));
                             
    CHECK_CUBLAS(cublasDestroy(handle));
}

}
