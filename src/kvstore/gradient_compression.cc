/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/*!
 * \file gradient_compression.cc
 * \brief Gradient compression for kvstore
 * \author Rahul Huilgol
 * Modifications Copyright (c) 2021 by Contributors at INET-RC
 */

#include <vector>
#include <random>
#include <algorithm>
#include <iterator>
#include <cmath>
#include <utility>
#include "kvstore_local.h"
#include "gradient_compression.h"
#include "gradient_compression-inl.h"

namespace mxnet {
namespace kvstore {

DMLC_REGISTER_PARAMETER(GradientCompressionParam);

GradientCompression::GradientCompression() {
  type_ = CompressionType::kNone;
}

void GradientCompression::SetParams(const std::vector<std::pair<std::string, std::string> >
                                    & kwargs) {
  GradientCompressionParam params;
  params.InitAllowUnknown(kwargs);
  CHECK_GT(params.threshold, 0) << "threshold must be greater than 0";
  if (params.type == "2bit") {
    SetTwoBitCompression(params.threshold);
  } else if (params.type == "bsc") {
    SetBiSparseCompression(params.threshold);
  } else {
    LOG(FATAL) << "Unknown type for gradient compression " << params.type;
  }
}

CompressionType GradientCompression::get_type() {
  return type_;
}

float GradientCompression::get_threshold(){
  return threshold_;
}

std::string GradientCompression::get_type_str() {
  return std::to_string(static_cast<int>(type_));
}

void GradientCompression::SetTwoBitCompression(const float threshold) {
  type_ = CompressionType::kTwoBit;
  threshold_ = threshold;
}

void GradientCompression::SetBiSparseCompression(const float threshold) {
    type_ = CompressionType::kBiSparseCompression;
    threshold_ = threshold;
}

std::string GradientCompression::EncodeParams() {
  using namespace std;  // to reduce length of next line
  string rval = get_type_str();
  if (type_ == CompressionType::kTwoBit || type_ == CompressionType::kBiSparseCompression) {
    rval += "," + to_string(threshold_);
  }
  return rval;
}

void GradientCompression::DecodeParams(const std::string &s) {
  std::vector<std::string> elems;
  mxnet::kvstore::split(s, ',', std::back_inserter(elems));
  type_ = static_cast<CompressionType>(stoi(elems[0]));
  if (elems.size() > 1) {
    if (!elems[1].empty()) {
      threshold_ = stof(elems[1]);
    }
  }
}

int GradientCompression::GetCompressionFactor() {
  if (type_ == CompressionType::kTwoBit) {
    return 16;
  } else {
    LOG(FATAL) << "Unsupported compression type: " << get_type_str();
    return 0;
  }
}

int64_t GradientCompression::GetCompressedSize(const int64_t original_size) {
  const int bits = GetCompressionFactor();
  return ((original_size % bits == 0) ?
          original_size / bits :
          original_size / bits + 1);
}

void GradientCompression::Quantize(const mxnet::NDArray &from, mxnet::NDArray *to,
                  mxnet::NDArray *residual, const int priority) {
  CHECK(from.shape().ndim() != 0) << "source operand has zero dimension shape";
  CHECK(to->shape().ndim() != 0) << "destination operand has zero dimension shape";
  CHECK(residual->shape().ndim() != 0) << "residual operand has zero dimension shape";
  const int a = from.ctx().dev_mask();
  const int b = to->ctx().dev_mask();
  const float threshold = threshold_;
  if (type_ == CompressionType::kTwoBit) {
    if (a == mshadow::cpu::kDevMask && b == mshadow::cpu::kDevMask) {
      mxnet::Engine::Get()->PushSync([from, to, residual, threshold](mxnet::RunContext ctx) {
        std::vector<mxnet::TBlob> inputs = {from.data(), residual->data(), to->data()};
        Quantize2BitImpl(ctx.get_stream<mshadow::cpu>(), inputs, threshold);
      }, from.ctx(), {from.var()}, {to->var(), residual->var()},
      mxnet::FnProperty::kNormal, priority, "QuantizeCPU");
    } else {
#if MXNET_USE_CUDA
      if (a == mshadow::gpu::kDevMask && b == mshadow::gpu::kDevMask) {
        mxnet::Engine::Get()->PushSync([from, to, residual, threshold](mxnet::RunContext ctx) {
          std::vector<mxnet::TBlob> inputs = {from.data(), residual->data(), to->data()};
          Quantize2BitImpl(ctx.get_stream<mshadow::gpu>(), inputs, threshold);
          // Wait GPU kernel to complete
          ctx.get_stream<mshadow::gpu>()->Wait();
        }, from.ctx(), {from.var()}, {to->var(), residual->var()},
        mxnet::FnProperty::kNormal, priority, "QuantizeGPU");
      } else {
        LOG(FATAL) << "unknown device mask";
      }
#else
    LOG(FATAL) << MXNET_GPU_NOT_ENABLED_ERROR;
#endif
    }
  } else {
    LOG(FATAL) << "Unsupported quantization of type " << get_type_str();
  }
}

void GradientCompression::Dequantize(const mxnet::NDArray &from, mxnet::NDArray *to,
                                     const int priority) {
  CHECK(from.shape().ndim() != 0) << "source operands has zero dimension shape";
  CHECK(to->shape().ndim() != 0) << "destination operand has zero dimension shape";
  const int a = from.ctx().dev_mask();
  const int b = to->ctx().dev_mask();
  const float threshold = threshold_;
  if (type_ == CompressionType::kTwoBit) {
    if (a == mshadow::cpu::kDevMask && b == mshadow::cpu::kDevMask) {
      mxnet::Engine::Get()->PushSync([from, to, threshold](mxnet::RunContext ctx) {
        std::vector<mxnet::TBlob> inputs = {from.data(), to->data()};
        Dequantize2BitImpl(ctx.get_stream<mshadow::cpu>(), inputs, threshold);
      }, from.ctx(), {from.var()}, {to->var()},
      mxnet::FnProperty::kNormal, priority, "DequantizeCPU");
    } else {
#if MXNET_USE_CUDA
      if (a == mshadow::gpu::kDevMask && b == mshadow::gpu::kDevMask) {
        mxnet::Engine::Get()->PushSync([from, to, threshold](mxnet::RunContext ctx) {
          std::vector<mxnet::TBlob> inputs = {from.data(), to->data()};
          Dequantize2BitImpl(ctx.get_stream<mshadow::gpu>(), inputs, threshold);
          // Wait GPU kernel to complete
          ctx.get_stream<mshadow::gpu>()->Wait();
        }, from.ctx(), {from.var()}, {to->var()},
        mxnet::FnProperty::kNormal, priority, "DequantizeGPU");
      } else {
        LOG(FATAL) << "unknown device mask";
      }
#else
      LOG(FATAL) << MXNET_GPU_NOT_ENABLED_ERROR;
#endif
    }
  } else {
    LOG(FATAL) << "Unsupported dequantization of type " << get_type_str();
  }
}

void GradientCompression::BSCompress(const mxnet::NDArray &from, mxnet::NDArray &to, 
                                      mxnet::NDArray &u_, mxnet::NDArray &v_, const int priority) {
  const float threshold = threshold_;
  if (type_ == CompressionType::kBiSparseCompression) {
    auto dg_compress = [this, from, to, u_, v_, threshold](mxnet::RunContext ctx) {
      const int original_size = from.data().Size();
      const int zipped_size = float(original_size) * threshold;

      float momentumn = 0.9;
      // const int sample_size = original_size * 0.005 * threshold >= 10 ? original_size * 0.005 : original_size * 0.025;
      // const unsigned int top_k = sample_size * threshold >= 2 ? sample_size * threshold : 2;
      const int sample_size = original_size * 0.005 * threshold >= 10
                                  ? original_size * 0.005
                                  : 10 / threshold;
      const unsigned int top_k = sample_size * threshold;

      float *grad = from.data().dptr<float>();
      float *u = u_.data().dptr<float>();
      float *v = v_.data().dptr<float>();
      // mshadow::half::half_t *out = to.data().dptr<mshadow::half::half_t>();
      float *out = to.data().dptr<float>();
      // first step sampling
      std::vector<float> grad_abs;
      for (int i = 0; i < original_size; i++) {
        u[i] = u[i] * momentumn + grad[i];
        v[i] = v[i] + u[i];
        grad_abs.push_back(fabs(v[i]));
      }
//       std::random_device rd;
//       std::mt19937 g(rd());
//       std::shuffle(grad_abs.begin(), grad_abs.end(), g);
      // std::mt19937 g(42);
      std::shuffle(grad_abs.begin(), grad_abs.end(), std::default_random_engine(42));
      std::vector<float>::const_iterator first_index = grad_abs.begin();
      std::vector<float>::const_iterator last_index = grad_abs.begin() + sample_size;
      std::vector<float> samples(first_index, last_index);

      // second step sampling
      std::priority_queue<float, std::vector<float>, std::greater<float> > q;
      for (float val : samples) {
        if (q.size() < top_k)
          q.push(val);
        else {
          if (val > q.top()) {
            q.pop();
            q.push(val);
          }
        }
      }

      // filtering data
      float boundary = q.top();
      int flag = 0;
      for (int i = 0; i < original_size; i++) {
        if (fabs(v[i]) >= boundary) {
          if (flag < zipped_size) {
            out[flag] = v[i];
            out[flag + zipped_size] = i;
            v[i] = 0;
            u[i] = 0;
            flag += 1;
          }
        }
      }
      if (flag < zipped_size) {
        for (; flag < zipped_size; flag++) {
          out[flag] = -65530;
          out[flag + zipped_size] = -1;
        }
      }
    };
    mxnet::Engine::Get()->PushSync(dg_compress, from.ctx(), {from.var()}, {to.var(), u_.var(), v_.var()},
                                   mxnet::FnProperty::kNormal, priority, "BSCompressCPU");
  } else {
    LOG(FATAL) << "Unsupported compression of type " << get_type_str();
  }
}

void GradientCompression::BSCSum(const mxnet::NDArray &from, mxnet::NDArray &to,
                                 const int multiplier, const int priority) {
  const float threshold = threshold_;
  if (type_ == CompressionType::kBiSparseCompression) {
    auto dg_compress = [this, from, to, threshold, multiplier](mxnet::RunContext ctx) {
      const int original_size = from.data().Size();
      const int zipped_size = float(original_size) * threshold * multiplier;

      float *grad = from.data().dptr<float>();
      float *out = to.data().dptr<float>();

      // filtering data
      int flag = 0;
      for (int i = 0; i < original_size; i++) {
        if (grad[i] != 0) {
          if (flag < zipped_size) {
            out[flag] = grad[i];
            out[flag + zipped_size] = i;
            flag += 1;
          }
        }
      }
      if (flag < zipped_size) {
        for (; flag < zipped_size; flag++) {
          out[flag] = -65530;
          out[flag + zipped_size] = -1;
        }
      }
    };
    mxnet::Engine::Get()->PushSync(dg_compress, from.ctx(), {from.var()}, {to.var()},
                                   mxnet::FnProperty::kNormal, priority, "BSCSumCPU");
  }
  else
  {
    LOG(FATAL) << "Unsupported compression of type " << get_type_str();
  }
}

void GradientCompression::BSDecompress(const mxnet::NDArray &from, mxnet::NDArray &to, const int priority){
  if (type_ == CompressionType::kBiSparseCompression) {
    auto dg_decompress = [this, from, to](mxnet::RunContext ctx) {
      int zipped_size = from.data().Size() / 2;
      int original_size = to.data().Size();

      // mshadow::half::half_t *zip = from.data().dptr<mshadow::half::half_t>();
      float *zip = from.data().dptr<float>();
      float *out = to.data().dptr<float>();
      
      for (int i = 0; i < original_size; i++) {
        out[i] = 0;
      }
      int index = 0;
      for (int i = 0; i < zipped_size; i++) {
        index = zip[i + zipped_size];
        if (index >= 0) {
          out[index] = zip[i];
        }
      }
    };
    mxnet::Engine::Get()->PushSync(dg_decompress, from.ctx(), {from.var()}, {to.var()},
                                   mxnet::FnProperty::kNormal, priority, "BSDecompressCPU");
  } else {
    LOG(FATAL) << "Unsupported compression of type " << get_type_str();
  }
}

}  // namespace kvstore
}  // namespace mxnet

