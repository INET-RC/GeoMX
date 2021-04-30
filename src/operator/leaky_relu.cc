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
 * \file leaky_relu.cc
 * \brief
 * \author Bing Xu
*/

#include "./leaky_relu-inl.h"

#include <nnvm/op_attr_types.h>
namespace mxnet {
namespace op {
template<>
Operator *CreateOp<cpu>(LeakyReLUParam param, int dtype) {
  Operator* op = nullptr;
  MSHADOW_REAL_TYPE_SWITCH(dtype, DType, {
    op = new LeakyReLUOp<cpu, DType>(param);
  });
  return op;
}

Operator *LeakyReLUProp::CreateOperatorEx(Context ctx, std::vector<TShape> *in_shape,
                                          std::vector<int> *in_type) const {
  DO_BIND_DISPATCH(CreateOp, param_, in_type->at(0));
}

DMLC_REGISTER_PARAMETER(LeakyReLUParam);

MXNET_REGISTER_OP_PROPERTY(LeakyReLU, LeakyReLUProp)
.describe(R"code(Applies Leaky rectified linear unit activation element-wise to the input.

Leaky ReLUs attempt to fix the "dying ReLU" problem by allowing a small `slope`
when the input is negative and has a slope of one when input is positive.

The following modified ReLU Activation functions are supported:

- *elu*: Exponential Linear Unit. `y = x > 0 ? x : slope * (exp(x)-1)`
- *selu*: Scaled Exponential Linear Unit. `y = lambda * (x > 0 ? x : alpha * (exp(x) - 1))` where
  *lambda = 1.0507009873554804934193349852946* and *alpha = 1.6732632423543772848170429916717*.
- *leaky*: Leaky ReLU. `y = x > 0 ? x : slope * x`
- *prelu*: Parametric ReLU. This is same as *leaky* except that `slope` is learnt during training.
- *rrelu*: Randomized ReLU. same as *leaky* but the `slope` is uniformly and randomly chosen from
  *[lower_bound, upper_bound)* for training, while fixed to be
  *(lower_bound+upper_bound)/2* for inference.

)code" ADD_FILELINE)
.add_argument("data", "NDArray-or-Symbol", "Input data to activation function.")
.add_argument("gamma", "NDArray-or-Symbol",
              "Slope parameter for PReLU. Only required "
              "when act_type is 'prelu'. It should be either a vector of size 1, "
              "or the same size as the second dimension of data.")
.add_arguments(LeakyReLUParam::__FIELDS__());

NNVM_REGISTER_OP(LeakyReLU)
.set_attr<nnvm::FSetInputVarAttrOnCompose>("FSetInputVarAttrOnCompose",
    [](const nnvm::NodeAttrs& attrs, nnvm::NodePtr var, const int index) {
      if (index == 1 && var->attrs.dict.find("__init__") == var->attrs.dict.end()) {
        var->attrs.dict["__init__"] = "[\"Constant\", {\"value\": 0.25}]";
      }
    });

}  // namespace op
}  // namespace mxnet
