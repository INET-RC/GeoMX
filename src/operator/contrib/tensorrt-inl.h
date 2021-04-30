#ifndef MXNET_OPERATOR_CONTRIB_TENSORRT_INL_H_
#define MXNET_OPERATOR_CONTRIB_TENSORRT_INL_H_
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
 * Copyright (c) 2018 by Contributors
 * \file tensorrt-inl.h
 * \brief TensorRT Operator
 * \author Marek Kolodziej, Clement Fuji Tsang
*/

#if MXNET_USE_TENSORRT

#include <dmlc/logging.h>
#include <dmlc/memory_io.h>
#include <dmlc/serializer.h>
#include <dmlc/parameter.h>
#include <mxnet/base.h>
#include <mxnet/operator.h>
#include <nnvm/graph.h>
#include <nnvm/pass_functions.h>

#include <NvInfer.h>
#include <onnx/onnx_pb.h>

#include <algorithm>
#include <iostream>
#include <map>
#include <vector>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <string>

#include "../operator_common.h"
#include "../../common/utils.h"
#include "../../common/serialization.h"
#include "../../executor/exec_pass.h"
#include "../../executor/graph_executor.h"
#include "../../executor/onnx_to_tensorrt.h"

namespace mxnet {
namespace op {

using namespace nnvm;
using namespace ::onnx;
using int64 = ::google::protobuf::int64;

namespace tensorrt {
  enum class TypeIO { Inputs = 0, Outputs = 1 };
  using NameToIdx_t = std::map<std::string, int32_t>;
  using InferenceTuple_t = std::tuple<uint32_t, TShape, int, int>;
  using InferenceMap_t = std::map<std::string, InferenceTuple_t>;
}  // namespace tensorrt

using trt_name_to_idx = std::map<std::string, uint32_t>;

struct TRTParam : public dmlc::Parameter<TRTParam> {
  std::string serialized_onnx_graph;
  std::string serialized_input_map;
  std::string serialized_output_map;
  tensorrt::NameToIdx_t input_map;
  tensorrt::InferenceMap_t output_map;
  ::onnx::ModelProto onnx_pb_graph;

  TRTParam() {}

  TRTParam(const ::onnx::ModelProto& onnx_graph,
           const tensorrt::InferenceMap_t& input_map,
           const tensorrt::NameToIdx_t& output_map) {
    common::Serialize(input_map, &serialized_input_map);
    common::Serialize(output_map, &serialized_output_map);
    onnx_graph.SerializeToString(&serialized_onnx_graph);
  }

DMLC_DECLARE_PARAMETER(TRTParam) {
    DMLC_DECLARE_FIELD(serialized_onnx_graph)
    .describe("Serialized ONNX graph");
    DMLC_DECLARE_FIELD(serialized_input_map)
    .describe("Map from inputs to topological order as input.");
    DMLC_DECLARE_FIELD(serialized_output_map)
    .describe("Map from outputs to order in g.outputs.");
  }
};

struct TRTEngineParam {
  nvinfer1::IExecutionContext* trt_executor;
  std::vector<std::pair<uint32_t, tensorrt::TypeIO> > binding_map;
};

}  // namespace op
}  // namespace mxnet

#endif  // MXNET_USE_TENSORRT

#endif  // MXNET_OPERATOR_CONTRIB_TENSORRT_INL_H_
