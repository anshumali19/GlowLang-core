#include <cuda_runtime.h>
#include <cublas_v2.h>
#include <stdio.h>

extern "C" {

void* cuda_tensor_malloc(size_t size) {
    void* d_ptr = NULL;
    cudaMalloc(&d_ptr, size);
    return d_ptr;
}

void cuda_tensor_free(void* ptr) {
    cudaFree(ptr);
}

void cuda_tensor_to_device(void* dst, const void* src, size_t size) {
    cudaMemcpy(dst, src, size, cudaMemcpyHostToDevice);
}

void cuda_tensor_to_host(void* dst, const void* src, size_t size) {
    cudaMemcpy(dst, src, size, cudaMemcpyDeviceToHost);
}

void cuda_tensor_matmul(const double* A, int rA, int cA, const double* B, int cB, double* C) {
    cublasHandle_t handle;
    cublasCreate(&handle);

    // cuBLAS uses column-major order natively, but our arrays are row-major.
    // To compute C = A * B in row-major, we compute C^T = B^T * A^T in column-major.
    // So we pass B as the first matrix and A as the second matrix to cublasDgemm.
    double alpha = 1.0;
    double beta = 0.0;
    
    // Note: B is cA x cB (rB x cB where rB=cA)
    // A is rA x cA
    cublasDgemm(handle, CUBLAS_OP_N, CUBLAS_OP_N,
                cB, rA, cA,
                &alpha,
                B, cB,
                A, cA,
                &beta,
                C, cB);

    cublasDestroy(handle);
    cudaDeviceSynchronize();
}

}
