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
 * Copyright (c) 2017 by Contributors
 * \file softmax.cc
 * \brief CPU Implementation of softmax
 */
#include "./softmax-inl.h"
#include "../tensor/elemwise_unary_op.h"
#include "../tensor/elemwise_binary_op.h"
#include "../operator_common.h"
#if MXNET_USE_MKLDNN == 1
#include "mkldnn/mkldnn_base-inl.h"
#include "mkldnn/mkldnn_ops-inl.h"
#endif

namespace mxnet {
namespace op {
DMLC_REGISTER_PARAMETER(SoftmaxParam);

#if MXNET_USE_MKLDNN == 1
static void SoftmaxComputeExCPU(const nnvm::NodeAttrs& attrs,
                                const OpContext& ctx,
                                const std::vector<NDArray>& inputs,
                                const std::vector<OpReqType>& req,
                                const std::vector<NDArray>& outputs) {
  // It seems MKLDNN softmax doesn't support training.
  const SoftmaxParam& param = nnvm::get<SoftmaxParam>(attrs.parsed);
  if (SupportMKLDNN(inputs[0]) && !ctx.is_train && SupportMKLDNNSoftmax(param)) {
    MKLDNN_OPCHECK_INIT(false, outputs.size(), inputs, outputs);
    MKLDNNSoftmaxForward(attrs, ctx, inputs[0], req[0], outputs[0]);
    auto fn = SoftmaxCompute<cpu, mxnet_op::softmax_fwd>;
    MKLDNN_OPCHECK_RUN(fn, attrs, ctx, inputs, req, outputs);
    return;
  }
  FallBackCompute(SoftmaxCompute<cpu, mxnet_op::softmax_fwd>, attrs, ctx,
                  inputs, req, outputs);
}

inline static bool SoftmaxStorageType(const nnvm::NodeAttrs& attrs,
                                      const int dev_mask,
                                      DispatchMode* dispatch_mode,
                                      std::vector<int> *in_attrs,
                                      std::vector<int> *out_attrs) {
  CHECK_EQ(in_attrs->size(), 1);
  CHECK_EQ(out_attrs->size(), 1);

  return MKLDNNStorageType(attrs, dev_mask, true, dispatch_mode, in_attrs,
                           out_attrs);
}
#endif

MXNET_OPERATOR_REGISTER_UNARY(softmax)
.describe(R"code(Applies the softmax function.

The resulting array contains elements in the range (0,1) and the elements along the given axis sum up to 1.

.. math::
   softmax(\mathbf{z/t})_j = \frac{e^{z_j/t}}{\sum_{k=1}^K e^{z_k/t}}

for :math:`j = 1, ..., K`

t is the temperature parameter in softmax function. By default, t equals 1.0

Example::

  x = [[ 1.  1.  1.]
       [ 1.  1.  1.]]

  softmax(x,axis=0) = [[ 0.5  0.5  0.5]
                       [ 0.5  0.5  0.5]]

  softmax(x,axis=1) = [[ 0.33333334,  0.33333334,  0.33333334],
                       [ 0.33333334,  0.33333334,  0.33333334]]

)code" ADD_FILELINE)
.set_attr_parser(ParamParser<SoftmaxParam>)
.set_attr<nnvm::FListOutputNames>("FListOutputNames",
    [](const NodeAttrs& attrs) {
    return std::vector<std::string>{"output"};
})
.set_attr<FCompute>("FCompute<cpu>", SoftmaxCompute<cpu, mxnet_op::softmax_fwd>)
#if MXNET_USE_MKLDNN == 1
.set_attr<bool>("TIsMKLDNN", true)
.set_attr<FComputeEx>("FComputeEx<cpu>", SoftmaxComputeExCPU)
.set_attr<FInferStorageType>("FInferStorageType", SoftmaxStorageType)
#endif
.set_attr<nnvm::FGradient>("FGradient", ElemwiseGradUseOut{"_backward_softmax"})
.add_arguments(SoftmaxParam::__FIELDS__());

MXNET_OPERATOR_REGISTER_BINARY(_backward_softmax)
.set_attr_parser(ParamParser<SoftmaxParam>)
.set_attr<FCompute>("FCompute<cpu>", SoftmaxGradCompute<cpu, op::mshadow_op::mul,
                                                        mxnet_op::softmax_bwd>);

MXNET_OPERATOR_REGISTER_UNARY(softmin)
.describe(R"code(Applies the softmin function.

The resulting array contains elements in the range (0,1) and the elements along the given axis sum
up to 1.

.. math::
   softmin(\mathbf{z/t})_j = \frac{e^{-z_j/t}}{\sum_{k=1}^K e^{-z_k/t}}

for :math:`j = 1, ..., K`

t is the temperature parameter in softmax function. By default, t equals 1.0

Example::

  x = [[ 1.  2.  3.]
       [ 3.  2.  1.]]

  softmin(x,axis=0) = [[ 0.88079703,  0.5,  0.11920292],
                       [ 0.11920292,  0.5,  0.88079703]]

  softmin(x,axis=1) = [[ 0.66524094,  0.24472848,  0.09003057],
                       [ 0.09003057,  0.24472848,  0.66524094]]

)code" ADD_FILELINE)
.set_attr_parser(ParamParser<SoftmaxParam>)
.set_attr<nnvm::FListOutputNames>("FListOutputNames",
    [](const NodeAttrs& attrs) {
    return std::vector<std::string>{"output"};
})
.set_attr<FCompute>("FCompute<cpu>", SoftmaxCompute<cpu, mxnet_op::softmax_fwd, true>)
.set_attr<nnvm::FGradient>("FGradient", ElemwiseGradUseOut{"_backward_softmin"})
.add_arguments(SoftmaxParam::__FIELDS__());

MXNET_OPERATOR_REGISTER_BINARY(_backward_softmin)
.set_attr_parser(ParamParser<SoftmaxParam>)
.set_attr<FCompute>("FCompute<cpu>", SoftmaxGradCompute<cpu, op::mshadow_op::mul,
                                                        mxnet_op::softmax_bwd, true>);

MXNET_OPERATOR_REGISTER_UNARY(log_softmax)
.describe(R"code(Computes the log softmax of the input.
This is equivalent to computing softmax followed by log.

Examples::

  >>> x = mx.nd.array([1, 2, .1])
  >>> mx.nd.log_softmax(x).asnumpy()
  array([-1.41702998, -0.41702995, -2.31702995], dtype=float32)

  >>> x = mx.nd.array( [[1, 2, .1],[.1, 2, 1]] )
  >>> mx.nd.log_softmax(x, axis=0).asnumpy()
  array([[-0.34115392, -0.69314718, -1.24115396],
         [-1.24115396, -0.69314718, -0.34115392]], dtype=float32)


)code")
.set_attr_parser(ParamParser<SoftmaxParam>)
.set_attr<FCompute>("FCompute<cpu>", SoftmaxCompute<cpu, mxnet_op::log_softmax_fwd>)
.set_attr<nnvm::FGradient>("FGradient", ElemwiseGradUseOut{"_backward_log_softmax"})
.add_arguments(SoftmaxParam::__FIELDS__());

MXNET_OPERATOR_REGISTER_BINARY(_backward_log_softmax)
.set_attr_parser(ParamParser<SoftmaxParam>)
.set_attr<FCompute>("FCompute<cpu>", SoftmaxGradCompute<cpu, mshadow_op::left,
                                                        mxnet_op::log_softmax_bwd>);

}  // namespace op
}  // namespace mxnet
