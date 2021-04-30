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
 * Copyright (c) 2015 by Contributors
 * \file fully_connect_op-inl.h
 * \brief fully connect operator and symbol
*/
#ifndef MXNET_OPERATOR_NN_FULLY_CONNECTED_INL_H_
#define MXNET_OPERATOR_NN_FULLY_CONNECTED_INL_H_

#include <dmlc/logging.h>
#include <dmlc/parameter.h>
#include <mxnet/operator.h>
#include <map>
#include <vector>
#include <string>
#include <utility>
#include "../operator_common.h"
#include "../elemwise_op_common.h"
#include "../linalg.h"
#include "../../common/utils.h"

namespace mxnet {
namespace op {

// Declare enumeration of input order to make code more intuitive.
// These enums are only visible within this header
namespace fullc {
enum FullyConnectedOpInputs {kData, kWeight, kBias};
enum FullyConnectedOpResource {kTempSpace};
enum FullyConnectedOpOutputs {kOut};
}  // fullc

struct FullyConnectedParam : public dmlc::Parameter<FullyConnectedParam> {
  int num_hidden;
  bool no_bias;
  bool flatten;
  DMLC_DECLARE_PARAMETER(FullyConnectedParam) {
    // TODO(bing) add support for boolean
    DMLC_DECLARE_FIELD(num_hidden).set_lower_bound(1)
    .describe("Number of hidden nodes of the output.");
    DMLC_DECLARE_FIELD(no_bias).set_default(false)
    .describe("Whether to disable bias parameter.");
    DMLC_DECLARE_FIELD(flatten).set_default(true)
    .describe("Whether to collapse all but the first axis of the input data tensor.");
  }
  bool operator==(const FullyConnectedParam& other) const {
    return this->num_hidden == other.num_hidden &&
           this->no_bias == other.no_bias &&
           this->flatten == other.flatten;
  }
};

template<typename xpu, typename DType>
void FCForward(const OpContext &ctx, const FullyConnectedParam &param,
               const std::vector<TBlob> &in_data, const std::vector<OpReqType> &req,
               const std::vector<TBlob> &out_data) {
  using namespace mshadow;
  using namespace mshadow::expr;
  if (req[fullc::kOut] == kNullOp) return;
  CHECK_EQ(req[fullc::kOut], kWriteTo);
  // TODO(bing): check the BLAS Handle, be careful
  // maybe need blas handle from context
  // TODO(bing): judge shape to remove flatten op
  Stream<xpu> *s = ctx.get_stream<xpu>();
#if defined(__CUDACC__)
  CHECK_EQ(s->blas_handle_ownership_, Stream<xpu>::OwnHandle)
      << "Must init CuBLAS handle in stream";
#endif  // __CUDACC__
  const TShape& ishape = in_data[fullc::kData].shape_;
  const TShape& oshape = out_data[fullc::kOut].shape_;

  Tensor<xpu, 2, DType> wmat = in_data[fullc::kWeight].get<xpu, 2, DType>(s);
  Tensor<xpu, 2, DType> data, out;
  if (!param.flatten) {
    data = in_data[fullc::kData].get_with_shape<xpu, 2, DType>(
        Shape2(ishape.ProdShape(0, ishape.ndim()-1), ishape[ishape.ndim()-1]), s);
    out = out_data[fullc::kOut].get_with_shape<xpu, 2, DType>(
        Shape2(oshape.ProdShape(0, oshape.ndim()-1), oshape[oshape.ndim()-1]), s);
  } else {
    data = in_data[fullc::kData].get_with_shape<xpu, 2, DType>(
        Shape2(ishape[0], ishape.ProdShape(1, ishape.ndim())), s);
    out = out_data[fullc::kOut].get_with_shape<xpu, 2, DType>(
        Shape2(oshape[0], oshape.ProdShape(1, oshape.ndim())), s);
  }

  CHECK_EQ(data.shape_[1], wmat.shape_[1])
    << "Incomplete weight tensor detected: weight.data().shape[1] != prod(data.data().shape[1:])."
       " This is not supported by FCForward. If weight is in row_sparse format,"
       " please make sure all row ids are present.";
  // Legacy approach shown here for comparison:
  //   out = dot(data, wmat.T());
  linalg_gemm(data, wmat, out, false, true, s);
  if (!param.no_bias) {
    Tensor<xpu, 1, DType> bias = in_data[fullc::kBias].get_with_shape<xpu, 1, DType>(
      Shape1(wmat.shape_[0]), s);
    CHECK_EQ(bias.shape_[0], wmat.shape_[0])
      << "Incomplete bias tensor detected: bias.data().shape[1] != weight.data().shape[0]."
         " This is not supported by FCForward. If bias is in row_sparse format, please"
         " make sure all row ids are present.";
    out += repmat(bias, data.size(0));
  }
}

template<typename xpu, typename DType>
void FCBackward(const OpContext &ctx, const FullyConnectedParam &param,
                const std::vector<TBlob> &out_grad, const std::vector<TBlob> &in_data,
                const std::vector<OpReqType> &req, const std::vector<TBlob> &in_grad) {
  using namespace mshadow;
  using namespace mshadow::expr;
  // TODO(bing): check the BLAS Handle, be careful
  //  maybe need blas handle from context
  Stream<xpu> *s = ctx.get_stream<xpu>();
  const TShape& ishape = in_data[fullc::kData].shape_;
  const TShape& oshape = out_grad[fullc::kOut].shape_;

  Tensor<xpu, 2, DType> wmat = in_data[fullc::kWeight].get<xpu, 2, DType>(s);
  Tensor<xpu, 2, DType> data, grad, gdata;
  if (!param.flatten) {
    data = in_data[fullc::kData].get_with_shape<xpu, 2, DType>(
        Shape2(ishape.ProdShape(0, ishape.ndim()-1), ishape[ishape.ndim()-1]), s);
    grad = out_grad[fullc::kOut].get_with_shape<xpu, 2, DType>(
        Shape2(oshape.ProdShape(0, oshape.ndim()-1), oshape[oshape.ndim()-1]), s);
    gdata = in_grad[fullc::kData].get_with_shape<xpu, 2, DType>(
        Shape2(ishape.ProdShape(0, ishape.ndim()-1), ishape[ishape.ndim()-1]), s);
  } else {
    data = in_data[fullc::kData].get_with_shape<xpu, 2, DType>(
        Shape2(ishape[0], ishape.ProdShape(1, ishape.ndim())), s);
    grad = out_grad[fullc::kOut].get_with_shape<xpu, 2, DType>(
        Shape2(oshape[0], oshape.ProdShape(1, oshape.ndim())), s);
    gdata = in_grad[fullc::kData].get_with_shape<xpu, 2, DType>(
        Shape2(ishape[0], ishape.ProdShape(1, ishape.ndim())), s);
  }

#if defined(__CUDACC__)
  CHECK_EQ(s->blas_handle_ownership_, Stream<xpu>::OwnHandle)
      << "Must init CuBLAS handle in stream";
#endif
  //  backprop
  CHECK_NE(req[fullc::kWeight], kWriteInplace) << "cannot write weight inplace";
  // gradient of weight
  Tensor<xpu, 2, DType> gwmat = in_grad[fullc::kWeight].get<xpu, 2, DType>(s);
  // Legacy approach shown here for comparison:
  //   out = Assign(gwmat, req[fullc::kWeight], dot(grad.T(), data));
  linalg_gemm(grad, data, gwmat, true, false, s, req[fullc::kWeight]);
  // gradient of bias
  if (!param.no_bias) {
    Tensor<xpu, 1, DType> gbias = in_grad[fullc::kBias].get<xpu, 1, DType>(s);
    Assign(gbias, req[fullc::kBias], sum_rows(grad));
  }
  // gradient of data
  // Legacy approach shown here for comparison:
  //   Assign(gdata, req[fullc::kData], dot(grad, wmat));
  linalg_gemm(grad, wmat, gdata, false, false, s, req[fullc::kData]);
}

template<typename xpu>
void FullyConnectedCompute(const nnvm::NodeAttrs& attrs,
                           const OpContext& ctx,
                           const std::vector<TBlob>& inputs,
                           const std::vector<OpReqType>& req,
                           const std::vector<TBlob>& outputs) {
  const FullyConnectedParam& param = nnvm::get<FullyConnectedParam>(attrs.parsed);
  uint32_t in_expected = param.no_bias ? 2 : 3;
  CHECK_EQ(inputs.size(), in_expected);
  CHECK_EQ(outputs.size(), 1U);
  int dtype = inputs[0].type_flag_;

  switch (dtype) {
  case mshadow::kFloat32:
    FCForward<xpu, float>(ctx, param, inputs, req, outputs);
    break;
  case mshadow::kFloat64:
    FCForward<xpu, double>(ctx, param, inputs, req, outputs);
    break;
  case mshadow::kFloat16:
    LOG(FATAL) << "float16 fully connected layer is currently"
                  "only supported by CuDNN version.";
    break;
  default:
    LOG(FATAL) << "Unsupported type " << dtype;
  }
}

template<typename xpu>
void FullyConnectedGradCompute(const nnvm::NodeAttrs& attrs,
                               const OpContext& ctx,
                               const std::vector<TBlob>& inputs,
                               const std::vector<OpReqType>& req,
                               const std::vector<TBlob>& outputs) {
  const FullyConnectedParam& param = nnvm::get<FullyConnectedParam>(attrs.parsed);
  uint32_t out_expected = param.no_bias ? 2 : 3;
  CHECK_EQ(inputs.size(), 3U);
  CHECK_EQ(outputs.size(), out_expected);
  CHECK_EQ(req.size(), out_expected);

  std::vector<TBlob> out_grad{inputs[0]};
  std::vector<TBlob> in_data(inputs.begin() + 1, inputs.end());
  int dtype = inputs[0].type_flag_;

  switch (dtype) {
  case mshadow::kFloat32:
    FCBackward<xpu, float>(ctx, param, out_grad, in_data, req, outputs);
    break;
  case mshadow::kFloat64:
    FCBackward<xpu, double>(ctx, param, out_grad, in_data, req, outputs);
    break;
  case mshadow::kFloat16:
    LOG(FATAL) << "float16 fully connected layer is currently"
                  "only supported by CuDNN version.";
    break;
  default:
    LOG(FATAL) << "Unsupported type " << dtype;
  }
}

}  // namespace op
}  // namespace mxnet
namespace std {
template<>
struct hash<mxnet::op::FullyConnectedParam> {
  size_t operator()(const mxnet::op::FullyConnectedParam& val) {
    size_t ret = 0;
    ret = dmlc::HashCombine(ret, val.num_hidden);
    ret = dmlc::HashCombine(ret, val.no_bias);
    ret = dmlc::HashCombine(ret, val.flatten);
    return ret;
  }
};
}  // namespace std
#endif  // MXNET_OPERATOR_NN_FULLY_CONNECTED_INL_H_
