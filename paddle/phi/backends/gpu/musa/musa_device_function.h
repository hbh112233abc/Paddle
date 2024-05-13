/* Copyright (c) 2022 PaddlePaddle Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License. */

#pragma once

#define PADDLE_CUDA_FP16
// NOTE(): support float16 to half in header file.
#include "paddle/phi/common/bfloat16.h"
#include "paddle/phi/common/complex.h"
#include "paddle/phi/common/float16.h"
#include "paddle/phi/core/enforce.h"

namespace phi {
namespace backends {
namespace gpu {

#define FULL_WARP_MASK 0xFFFFFFFF
#define CREATE_SHFL_MASK(mask, predicate) \
  mask = __ballot_sync(FULL_WARP_MASK, (predicate))

#define CUDA_LAUNCH_KERNEL_BASE(dim, ...)  \
  case (dim): {                            \
    constexpr auto kPowerOfTwoDim = (dim); \
    __VA_ARGS__;                           \
  } break

#define CUDA_LAUNCH_KERNEL_HELPER(...)          \
  CUDA_LAUNCH_KERNEL_BASE(1024, ##__VA_ARGS__); \
  CUDA_LAUNCH_KERNEL_BASE(512, ##__VA_ARGS__);  \
  CUDA_LAUNCH_KERNEL_BASE(256, ##__VA_ARGS__);  \
  CUDA_LAUNCH_KERNEL_BASE(128, ##__VA_ARGS__);  \
  CUDA_LAUNCH_KERNEL_BASE(64, ##__VA_ARGS__);   \
  CUDA_LAUNCH_KERNEL_BASE(32, ##__VA_ARGS__);

template <typename T>
__forceinline__ __device__ T
CudaShuffleDownSync(unsigned mask, T val, int delta, int width = warpSize) {
  return __shfl_down_sync(mask, val, static_cast<unsigned>(delta), width);
}

template <typename T>
__forceinline__ __device__ T CudaShuffleXorSync(unsigned mask,
                                                T val,
                                                int width = warpSize) {
  return __shfl_xor_sync(mask, val, width);
}


#if defined(PADDLE_WITH_MUSA)
// Due to the inconsistency between mcc and nvcc, certain type conversions are not implicitly performed, so we specialize here.
template <>
__forceinline__ __device__ phi::dtype::float16 CudaShuffleXorSync(unsigned mask,
                                                phi::dtype::float16 val,
                                                int width) {
  return (phi::dtype::float16)(__shfl_xor_sync(mask, float(val), width));
}
#endif

template <>
__forceinline__ __device__ phi::dtype::float16 CudaShuffleDownSync(
    unsigned mask, phi::dtype::float16 val, int delta, int width) {
  return phi::dtype::float16(__shfl_down_sync(
      mask, val.to_half(), static_cast<unsigned>(delta), width));
}

template <>
__forceinline__ __device__ phi::dtype::bfloat16 CudaShuffleDownSync(
    unsigned mask, phi::dtype::bfloat16 val, int delta, int width) {
#if defined(PADDLE_MUSA_BF16) && defined(__MUSA_ARCH__) && __MUSA_ARCH__ >= 220
  return phi::dtype::bfloat16(__shfl_down_sync(
      mask, val.to_mt_bfloat16(), static_cast<unsigned>(delta), width));
#else
  PADDLE_ENFORCE(
      false, "__shfl_down_sync with bfloat16 is not supported on cuda <= 11.");
#endif
}

template <>
__forceinline__ __device__ phi::dtype::complex<float> CudaShuffleDownSync(
    unsigned mask, phi::dtype::complex<float> val, int delta, int width) {
  float real = static_cast<float>(__shfl_down_sync(
      mask, static_cast<float>(val.real), static_cast<unsigned>(delta), width));
  float imag = static_cast<float>(__shfl_down_sync(
      mask, static_cast<float>(val.imag), static_cast<unsigned>(delta), width));
  return phi::dtype::complex<float>(real, imag);
}

template <>
__forceinline__ __device__ phi::dtype::complex<double> CudaShuffleDownSync(
    unsigned mask, phi::dtype::complex<double> val, int delta, int width) {
  double real =
      static_cast<double>(__shfl_down_sync(mask,
                                           static_cast<double>(val.real),
                                           static_cast<unsigned>(delta),
                                           width));
  double imag =
      static_cast<double>(__shfl_down_sync(mask,
                                           static_cast<double>(val.imag),
                                           static_cast<unsigned>(delta),
                                           width));
  return phi::dtype::complex<double>(real, imag);
}

template <>
__forceinline__ __device__ phi::dtype::bfloat16 CudaShuffleXorSync(
    unsigned mask, phi::dtype::bfloat16 val, int width) {
#if defined(PADDLE_MUSA_BF16)
  return phi::dtype::bfloat16(
      __shfl_xor_sync(mask, val.to_mt_bfloat16(), width));
#else
  PADDLE_ENFORCE(
      false, "__shfl_xor_sync with bfloat16 is not supported on cuda <= 11.");
#endif
}

template <>
__forceinline__ __device__ phi::dtype::complex<float> CudaShuffleXorSync(
    unsigned mask, phi::dtype::complex<float> val, int width) {
  float real = static_cast<float>(
      __shfl_xor_sync(mask, static_cast<float>(val.real), width));
  float imag = static_cast<float>(
      __shfl_xor_sync(mask, static_cast<float>(val.imag), width));
  return phi::dtype::complex<float>(real, imag);
}

template <>
__forceinline__ __device__ phi::dtype::complex<double> CudaShuffleXorSync(
    unsigned mask, phi::dtype::complex<double> val, int width) {
  double real = static_cast<double>(
      __shfl_xor_sync(mask, static_cast<double>(val.real), width));
  double imag = static_cast<double>(
      __shfl_xor_sync(mask, static_cast<double>(val.imag), width));
  return phi::dtype::complex<double>(real, imag);
}

template <typename T>
__forceinline__ __device__ T
CudaShuffleSync(unsigned mask, T val, int src_line, int width = 32) {
  return __shfl_sync(mask, val, src_line, width);
}

template <typename T>
HOSTDEVICE T Infinity() {
  return INFINITY;
}

template <typename T>
__device__ T reduceSum(T val, int tid, int len) {
  // NOTE(zcd): The warp size should be taken from the
  // parameters of the GPU but not specified as 32 simply.
  // To make the reduceSum more efficiently,
  // I use Warp-Level Parallelism and assume the Warp size
  // is 32 which may be different for different GPU,
  // but most card's warp size is 32.
  const int warpSize = 32;
  __shared__ T shm[warpSize];
  unsigned mask = 0u;
  CREATE_SHFL_MASK(mask, tid < len);

  for (int offset = warpSize / 2; offset > 0; offset /= 2)
    val += phi::backends::gpu::CudaShuffleDownSync(mask, val, offset);

  if (tid < warpSize) shm[tid] = 0;
  __syncthreads();

  if (tid % warpSize == 0) {
    shm[tid / warpSize] = val;
  }
  __syncthreads();

  CREATE_SHFL_MASK(mask, tid < warpSize);

  if (tid < warpSize) {
    val = shm[tid];
    for (int offset = warpSize / 2; offset > 0; offset /= 2)
      val += phi::backends::gpu::CudaShuffleDownSync(mask, val, offset);
  }
  return val;
}
}  // namespace gpu
}  // namespace backends
}  // namespace phi
