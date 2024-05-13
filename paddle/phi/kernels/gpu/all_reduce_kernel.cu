// Copyright (c) 2023 PaddlePaddle Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "paddle/phi/kernels/all_reduce_kernel.h"

#include "paddle/phi/backends/all_context.h"
#include "paddle/phi/core/enforce.h"
#include "paddle/phi/core/kernel_registry.h"

#if defined(PADDLE_WITH_NCCL) || defined(PADDLE_WITH_RCCL) || defined(PADDLE_WITH_MCCL)
#include "paddle/phi/core/distributed/nccl_comm_context.h"
#endif

namespace phi {

template <typename T, typename Context>
void AllReduceKernel(const Context& dev_ctx,
                     const DenseTensor& x,
                     int reduce_type,
                     DenseTensor* out) {
#if defined(PADDLE_WITH_NCCL) || defined(PADDLE_WITH_RCCL) || defined(PADDLE_WITH_MCCL)
  out->Resize(x.dims());
  dev_ctx.template Alloc<T>(out);

  auto comm_ctx =
      static_cast<distributed::NCCLCommContext*>(dev_ctx.GetCommContext());
  PADDLE_ENFORCE_NE(
      comm_ctx,
      nullptr,
      errors::Unavailable("NCCLCommContext is nullptr, collective op should "
                          "has ring_id attr."));
  gpuStream_t stream = dev_ctx.stream();
  PADDLE_ENFORCE_NOT_NULL(stream,
                          errors::NotFound("Should initialize NCCL firstly."));

  mcclRedOp_t red_type = mcclSum;
  switch (static_cast<ReduceType>(reduce_type)) {
    case ReduceType::kRedSum:
      red_type = mcclSum;
      break;
    case ReduceType::kRedMax:
      red_type = mcclMax;
      break;
    case ReduceType::kRedMin:
      red_type = mcclMin;
      break;
    case ReduceType::kRedProd:
      red_type = mcclProd;
      break;
    case ReduceType::kRedAll:
      // NOTE(zhonghui): There is no reduce_all type of mcclRedOp_t, just use
      // min to replace
      red_type = mcclMin;
      break;
    default:
    PADDLE_ENFORCE(false, "unsupported type");
  }
  comm_ctx->AllReduce(out, x, red_type, stream);
#else
  PADDLE_THROW(
      errors::PreconditionNotMet("PaddlePaddle should compile with GPU."));
#endif
}

}  // namespace phi

#if NCCL_VERSION_CODE >= 21000
PD_REGISTER_KERNEL(all_reduce,
                   GPU,
                   ALL_LAYOUT,
                   phi::AllReduceKernel,
                   float,
                   double,
                   int,
                   bool,
                   int8_t,
                   uint8_t,
                   int16_t,
                   int64_t,
                   phi::dtype::bfloat16,
                   phi::dtype::float16) {}
#else
PD_REGISTER_KERNEL(all_reduce,
                   GPU,
                   ALL_LAYOUT,
                   phi::AllReduceKernel,
                   float,
                   double,
                   int,
                   bool,
                   int8_t,
                   uint8_t,
                   int16_t,
                   int64_t,
                   phi::dtype::float16) {}
#endif
