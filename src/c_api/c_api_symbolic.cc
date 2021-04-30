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
 *  Copyright (c) 2016 by Contributors
 * \file c_api_symbolic.cc
 * \brief C API of mxnet
 */
#include <mxnet/base.h>
#include <mxnet/c_api.h>
#include <nnvm/c_api.h>
#include <nnvm/pass.h>
#include <nnvm/pass_functions.h>
#include <nnvm/symbolic.h>
#include "./c_api_common.h"
#include "../operator/operator_common.h"
#include "../executor/exec_pass.h"
#include "../operator/subgraph/subgraph_property.h"

namespace mxnet {
namespace op {
void RegisterLegacyOpProp();
void RegisterLegacyNDFunc();
}
const std::vector<std::string> kHiddenKeys = {
  "ctx_group", "lr_mult", "wd_mult", "force_mirroring", "mirror_stage"
};
const std::vector<std::string> kReplacedHiddenKeys = {
  "__ctx_group__", "__lr_mult__", "__wd_mult__", "__force_mirroring__", "__mirror_stage__"
};
const char *kNamespaceSeparator = "$";


DMLC_JSON_ENABLE_ANY(int, int);

// convert nnvm symbol to a nnvm graph.
nnvm::Graph Symbol2Graph(const nnvm::Symbol &s) {
  nnvm::Graph g;
  g.outputs = s.outputs;
  g.attrs["mxnet_version"] = std::make_shared<nnvm::any>(static_cast<int>(MXNET_VERSION));
  return g;
}

std::vector<uint32_t> ReadOnlyArgIndices(const nnvm::IndexedGraph& idx) {
  std::vector<uint32_t> ret;
  auto& arg_nodes = idx.input_nodes();
  for (uint32_t i = 0; i < arg_nodes.size(); ++i) {
    if (idx.mutable_input_nodes().count(arg_nodes[i]) == 0) {
      ret.push_back(i);
    }
  }
  return ret;
}

}  // namespace mxnet

// symbolic configuration generation API.
// Redirect to NNVM's C API
int MXListAllOpNames(nn_uint *out_size,
                     const char ***out_array) {
  mxnet::op::RegisterLegacyOpProp();
  mxnet::op::RegisterLegacyNDFunc();
  return NNListAllOpNames(out_size, out_array);
}

int MXSymbolListAtomicSymbolCreators(mx_uint *out_size,
                                     AtomicSymbolCreator **out_array) {
  mxnet::op::RegisterLegacyOpProp();
  mxnet::op::RegisterLegacyNDFunc();
  return NNListUniqueOps(out_size, out_array);
}

int MXSymbolGetAtomicSymbolInfo(AtomicSymbolCreator creator,
                                const char **name,
                                const char **description,
                                mx_uint *num_args,
                                const char ***arg_names,
                                const char ***arg_type_infos,
                                const char ***arg_descriptions,
                                const char **key_var_num_args,
                                const char **return_type) {
  static auto& map_key_var_args = nnvm::Op::GetAttr<std::string>("key_var_num_args");
  const Op* op = static_cast<Op*>(creator);
  MXAPIThreadLocalEntry *ret = MXAPIThreadLocalStore::Get();
  ret->ret_str.resize(0);

  if (map_key_var_args.count(op) != 0) {
    *key_var_num_args = map_key_var_args[op].c_str();
  } else {
    *key_var_num_args = ret->ret_str.c_str();
  }
  return NNGetOpInfo(
      creator, name, description,
      num_args, arg_names, arg_type_infos,
      arg_descriptions, return_type);
}

int MXSymbolCreateAtomicSymbol(AtomicSymbolCreator creator,
                               mx_uint num_param,
                               const char **keys,
                               const char **vals,
                               SymbolHandle *out) {
  nnvm::Symbol *s = new nnvm::Symbol();
  API_BEGIN();
  const nnvm::Op* op = static_cast<const nnvm::Op*>(creator);
  std::unordered_map<std::string, std::string> kwargs;
  for (nn_uint i = 0; i < num_param; ++i) {
    bool flag = false;
    for (const auto &k : kHiddenKeys) {
      std::string tmp(keys[i]);
      size_t pos = tmp.rfind(k);
      if (pos == 0) {
        kwargs.insert({"__" + tmp + "__", std::string(vals[i])});
        flag = true;
        break;
      } else if (pos != std::string::npos && pos == tmp.length() - k.length()) {
        std::ostringstream os;
        os << "setting variable attributes with " << keys[i] << " is deprecated. "
           << "please instead use\nw = Variable(" << k << "=" << vals[i] << ")\n"
           << "sym = YourSymbolName(" << tmp.substr(0, pos-1) << "=w)";
        throw dmlc::Error(os.str());
      }
    }
    if (!flag)
      kwargs.insert({std::string(keys[i]), std::string(vals[i])});
  }
  *s = nnvm::Symbol::CreateFunctor(op, std::move(kwargs));
  *out = s;
  API_END_HANDLE_ERROR(delete s;);
}

int MXSymbolCreateVariable(const char *name, SymbolHandle *out) {
  return NNSymbolCreateVariable(name, out);
}

int MXSymbolCreateGroup(mx_uint num_symbols,
                        SymbolHandle *symbols,
                        SymbolHandle *out) {
  return NNSymbolCreateGroup(num_symbols, symbols, out);
}

int MXSymbolGetOutput(SymbolHandle symbol,
                      mx_uint index,
                      SymbolHandle *out) {
  return NNSymbolGetOutput(symbol, index, out);
}

int MXSymbolGetInternals(SymbolHandle symbol,
                         SymbolHandle *out) {
  nnvm::Symbol *s = new nnvm::Symbol();
  API_BEGIN();
  *s = static_cast<nnvm::Symbol*>(symbol)->GetInternals();
  *out = s;
  API_END_HANDLE_ERROR(delete s);
}

int MXSymbolGetChildren(SymbolHandle symbol,
                        SymbolHandle *out) {
  nnvm::Symbol *s = new nnvm::Symbol();
  API_BEGIN();
  *s = static_cast<nnvm::Symbol*>(symbol)->GetChildren();
  *out = s;
  API_END_HANDLE_ERROR(delete s);
}

int MXSymbolFree(SymbolHandle symbol) {
  return NNSymbolFree(symbol);
}

int MXSymbolCopy(SymbolHandle symbol, SymbolHandle *out) {
  return NNSymbolCopy(symbol, out);
}

int MXSymbolPrint(SymbolHandle symbol, const char **out_str) {
  return NNSymbolPrint(symbol, out_str);
}

int MXSymbolGetName(SymbolHandle symbol,
                    const char** out,
                    int* success) {
  return NNSymbolGetAttr(symbol, "name", out, success);
}

int MXSymbolGetAttr(SymbolHandle symbol,
                    const char* key,
                    const char** out,
                    int* success) {
  nnvm::Symbol *s = static_cast<nnvm::Symbol*>(symbol);
  MXAPIThreadLocalEntry *ret = MXAPIThreadLocalStore::Get();
  API_BEGIN();
  if (s->GetAttr(key, &(ret->ret_str))) {
    *out = (ret->ret_str).c_str();
    *success = 1;
  } else {
    *out = nullptr;
    *success = 0;
    if (std::find(kHiddenKeys.begin(), kHiddenKeys.end(), key) != kHiddenKeys.end()) {
      std::string skey = "__" + std::string(key) + "__";
      if (s->GetAttr(skey, &(ret->ret_str))) {
        *out = (ret->ret_str).c_str();
        *success = 1;
      }
    }
  }
  API_END();
}

int MXSymbolSetAttr(SymbolHandle symbol,
                    const char* key,
                    const char* value) {
  nnvm::Symbol *s = static_cast<nnvm::Symbol*>(symbol);
  API_BEGIN();
  std::vector<std::pair<std::string, std::string> > kwargs;
  std::string skey(key), sval(value);
  for (const auto &k : kHiddenKeys) {
    size_t pos = skey.rfind(k);
    if (pos == 0 && k.length() == skey.length()) {
      skey = "__" + skey + "__";
      break;
    } else if (pos != std::string::npos && pos + k.length() == skey.length()) {
      std::ostringstream os;
      os << "setting variable attributes with " << key << " is deprecated. "
         << "please instead use\nw = Variable(" << k << "=" << value << ")\n"
         << "sym = YourSymbolName(" << skey.substr(0, pos-1) << "=w)";
      throw dmlc::Error(os.str());
    }
  }
  kwargs.emplace_back(std::make_pair(std::move(skey), std::move(sval)));
  s->SetAttrs(kwargs);
  API_END();
}

int MXSymbolListAttr(SymbolHandle symbol,
                     mx_uint *out_size,
                     const char*** out) {
  nnvm::Symbol *s = static_cast<nnvm::Symbol*>(symbol);
  MXAPIThreadLocalEntry *ret = MXAPIThreadLocalStore::Get();
  API_BEGIN();
  std::vector<std::tuple<std::string, std::string, std::string> > attr =
      s->ListAttrsRecursive();

  std::vector<std::string>& attr_list = ret->ret_vec_str;
  attr_list.clear();
  for (const auto& tp : attr) {
    attr_list.emplace_back(std::get<0>(tp) + kNamespaceSeparator + std::get<1>(tp));
    attr_list.emplace_back(std::get<2>(tp));
    if (find(kReplacedHiddenKeys.begin(), kReplacedHiddenKeys.end(), std::get<1>(tp))
          != kReplacedHiddenKeys.end()) {
      attr_list.push_back(std::get<0>(tp) + kNamespaceSeparator +
                          std::get<1>(tp).substr(2, std::get<1>(tp).length() - 4));
      attr_list.push_back(std::get<2>(tp));
    }
  }
  *out_size = attr_list.size()/2;
  ret->ret_vec_charp.clear();
  for (const auto& attr : attr_list) {
    ret->ret_vec_charp.push_back(attr.c_str());
  }
  *out = dmlc::BeginPtr(ret->ret_vec_charp);
  API_END();
}

int MXSymbolListAttrShallow(SymbolHandle symbol,
                            mx_uint *out_size,
                            const char*** out) {
  nnvm::Symbol *s = static_cast<nnvm::Symbol*>(symbol);
  MXAPIThreadLocalEntry *ret = MXAPIThreadLocalStore::Get();
  API_BEGIN();
  std::unordered_map<std::string, std::string> attr =
      s->ListAttrs(static_cast<nnvm::Symbol::ListAttrOption>(1));  // NOLINT(*)

  std::vector<std::string>& attr_list = ret->ret_vec_str;
  attr_list.clear();
  for (const auto& kv : attr) {
    attr_list.push_back(kv.first);
    attr_list.push_back(kv.second);
    if (find(kReplacedHiddenKeys.begin(), kReplacedHiddenKeys.end(), kv.first)
          != kReplacedHiddenKeys.end()) {
      attr_list.push_back(kv.first.substr(2, kv.first.length() - 4));
      attr_list.push_back(kv.second);
    }
  }
  *out_size = attr_list.size()/2;
  ret->ret_vec_charp.clear();
  for (auto &attr : attr_list) {
    ret->ret_vec_charp.push_back(attr.c_str());
  }
  *out = dmlc::BeginPtr(ret->ret_vec_charp);
  API_END();
}

int MXSymbolListOutputs(SymbolHandle symbol,
                        mx_uint *out_size,
                        const char ***out_str_array) {
  return NNSymbolListOutputNames(symbol, out_size, out_str_array);
}

int MXSymbolGetNumOutputs(SymbolHandle symbol,
                           mx_uint *output_count) {
  return NNSymbolGetNumOutputs(symbol, output_count);
}

int MXSymbolCompose(SymbolHandle sym,
                    const char *name,
                    mx_uint num_args,
                    const char** keys,
                    SymbolHandle* args) {
  return NNSymbolCompose(sym, name, num_args, keys, args);
}

// adapter functions that re-implements the functions.
int MXSymbolListArguments(SymbolHandle symbol,
                          mx_uint *out_size,
                          const char ***out_str_array) {
  return NNSymbolListInputNames(symbol, 1, out_size, out_str_array);
}

int MXSymbolListAuxiliaryStates(SymbolHandle symbol,
                                mx_uint *out_size,
                                const char ***out_str_array) {
  return NNSymbolListInputNames(symbol, 2, out_size, out_str_array);
}

int MXSymbolGetAtomicSymbolName(AtomicSymbolCreator creator,
                                const char **out) {
  API_BEGIN();
  Op *e = static_cast<Op *>(creator);
  *out = e->name.c_str();
  API_END();
}

namespace mxnet {

extern std::vector<nnvm::Symbol *> GetInputSymbols(const nnvm::Symbol &sym);
extern bool CutGraphInputs(const std::vector<nnvm::NodeEntry *> &input_entries,
                           bool skip_var, std::vector<nnvm::NodeEntry> *orig_entries);

}

int MXSymbolGetInputSymbols(SymbolHandle sym, SymbolHandle **input_arr, int *input_size) {
  API_BEGIN();
  nnvm::Symbol *s = static_cast<nnvm::Symbol*>(sym);
  std::vector<nnvm::Symbol *> input_syms = mxnet::GetInputSymbols(*s);
  *input_size = input_syms.size();

  MXAPIThreadLocalEntry *ret = MXAPIThreadLocalStore::Get();
  ret->ret_handles.clear();
  ret->ret_handles.reserve(*input_size);
  for (int i = 0; i < *input_size; ++i) ret->ret_handles.push_back(input_syms[i]);
  *input_arr = reinterpret_cast<SymbolHandle*>(dmlc::BeginPtr(ret->ret_handles));
  API_END_HANDLE_ERROR();
}

int MXSymbolCutSubgraph(SymbolHandle sym, SymbolHandle **input_symbols,
                        int *input_size) {
  // Given a graph, we want to fetch the nodes that have been marked as part of
  // a subgraph.
  API_BEGIN();
  nnvm::Symbol *s = static_cast<nnvm::Symbol*>(sym);
  const std::string subg_attr = "__subgraph_name__";
  auto out_node = s->outputs[0].node;
  auto it = out_node->attrs.dict.find(subg_attr);
  if (it != out_node->attrs.dict.end()) {
    const std::string &subg_name = it->second;
    std::vector<nnvm::NodeEntry *> input_entries;
    DFSVisit(s->outputs, [&subg_attr, &subg_name, &input_entries]
             (nnvm::NodePtr n) {
      // If the node itself isn't in the subgraph, we ignore it.
      auto it = n->attrs.dict.find(subg_attr);
      if (it == n->attrs.dict.end() || it->second != subg_name)
        return;

      // We search for nodes whose node entries aren't in the subgraph.
      for (size_t j = 0; j < n->inputs.size(); j++) {
        auto in_node = n->inputs[j].node;
        auto it = in_node->attrs.dict.find(subg_attr);
        if (it == in_node->attrs.dict.end() || it->second != subg_name)
          input_entries.push_back(&n->inputs[j]);
      }
    });

    std::vector<nnvm::NodeEntry> orig_entries;
    CutGraphInputs(input_entries, false, &orig_entries);
    std::vector<nnvm::Symbol *> input_syms(orig_entries.size());
    for (size_t i = 0; i < input_syms.size(); i++) {
      input_syms[i] = new nnvm::Symbol();
      input_syms[i]->outputs.push_back(orig_entries[i]);
    }
    *input_size = input_syms.size();

    MXAPIThreadLocalEntry *ret = MXAPIThreadLocalStore::Get();
    ret->ret_handles.clear();
    ret->ret_handles.reserve(*input_size);
    for (int i = 0; i < *input_size; ++i) ret->ret_handles.push_back(input_syms[i]);
    *input_symbols = reinterpret_cast<SymbolHandle*>(dmlc::BeginPtr(ret->ret_handles));
  } else {
    *input_size = 0;
  }

  API_END_HANDLE_ERROR();
}

int MXSymbolCreateFromFile(const char *fname, SymbolHandle *out) {
  nnvm::Symbol *s = new nnvm::Symbol();
  API_BEGIN();
  std::unique_ptr<dmlc::Stream> fi(dmlc::Stream::Create(fname, "r"));
  dmlc::istream is(fi.get());
  nnvm::Graph g;
  g.attrs["json"] = std::make_shared<nnvm::any>(
    std::string(std::istreambuf_iterator<char>(is), std::istreambuf_iterator<char>()));
  s->outputs = nnvm::ApplyPass(g, "LoadLegacyJSON").outputs;
  *out = s;
  is.set_stream(nullptr);
  API_END_HANDLE_ERROR(delete s);
}

int MXSymbolCreateFromJSON(const char *json, SymbolHandle *out) {
  nnvm::Symbol *s = new nnvm::Symbol();
  API_BEGIN();
  nnvm::Graph g;
  g.attrs["json"] = std::make_shared<nnvm::any>(std::string(json));
  s->outputs = nnvm::ApplyPass(g, "LoadLegacyJSON").outputs;
  *out = s;
  API_END_HANDLE_ERROR(delete s);
}

int MXSymbolSaveToFile(SymbolHandle symbol, const char *fname) {
  nnvm::Symbol *s = static_cast<nnvm::Symbol*>(symbol);
  API_BEGIN();
  std::unique_ptr<dmlc::Stream> fo(dmlc::Stream::Create(fname, "w"));
  dmlc::ostream os(fo.get());
  os << nnvm::pass::SaveJSON(Symbol2Graph(*s));
  // reset file pointer, force flush
  os.set_stream(nullptr);
  API_END();
}

int MXSymbolSaveToJSON(SymbolHandle symbol, const char **out_json) {
  nnvm::Symbol *s = static_cast<nnvm::Symbol*>(symbol);
  MXAPIThreadLocalEntry *ret = MXAPIThreadLocalStore::Get();
  API_BEGIN();
  ret->ret_str = nnvm::pass::SaveJSON(Symbol2Graph(*s));
  *out_json = ret->ret_str.c_str();
  API_END();
}

namespace mxnet {

template<typename AttrType>
void MatchArguments(
    const nnvm::IndexedGraph& idx,
    const std::unordered_map<std::string, AttrType>& known_arg_attrs,
    std::vector<AttrType>* arg_attrs,
    const char* source) {
  auto& arg_nodes = idx.input_nodes();
  CHECK_EQ(arg_attrs->size(), arg_nodes.size());
  size_t nmatched = 0;
  for (size_t i = 0; i < arg_nodes.size(); ++i) {
    const std::string& name = idx[arg_nodes[i]].source->attrs.name;
    auto it = known_arg_attrs.find(name);
    if (it != known_arg_attrs.end()) {
      arg_attrs->at(i) = it->second;
      ++nmatched;
    }
  }
  if (nmatched != known_arg_attrs.size()) {
    std::unordered_set<std::string> keys;
    std::ostringstream head, msg;
    msg << "\nCandidate arguments:\n";
    for (size_t i = 0; i < arg_nodes.size(); ++i) {
      std::string arg_name = idx[arg_nodes[i]].source->attrs.name;
      keys.insert(arg_name);
      msg << "\t[" << i << ']' << arg_name << '\n';
    }
    for (const auto& kv : known_arg_attrs) {
      const std::string& key = kv.first;
      if (keys.count(key) == 0) {
        LOG(FATAL) << source
                   << "Keyword argument name " << key << " not found."
                   << msg.str();
      }
    }
  }
}

}  // namespace mxnet

int MXSymbolInferShape(SymbolHandle sym,
                       mx_uint num_args,
                       const char** keys,
                       const mx_uint *arg_ind_ptr,
                       const mx_uint *arg_shape_data,
                       mx_uint *in_shape_size,
                       const mx_uint **in_shape_ndim,
                       const mx_uint ***in_shape_data,
                       mx_uint *out_shape_size,
                       const mx_uint **out_shape_ndim,
                       const mx_uint ***out_shape_data,
                       mx_uint *aux_shape_size,
                       const mx_uint **aux_shape_ndim,
                       const mx_uint ***aux_shape_data,
                       int *complete) {
  nnvm::Symbol *s = static_cast<nnvm::Symbol*>(sym);
  MXAPIThreadLocalEntry *ret = MXAPIThreadLocalStore::Get();
  API_BEGIN();
  nnvm::Graph g = Symbol2Graph(*s);
  nnvm::ShapeVector arg_shapes(g.indexed_graph().input_nodes().size(), TShape());
  if (keys == nullptr && num_args != 0) {
    std::vector<uint32_t> read_only_args = mxnet::ReadOnlyArgIndices(g.indexed_graph());
    CHECK_LE(num_args, read_only_args.size());
    for (mx_uint i = 0; i < num_args; ++i) {
      arg_shapes[read_only_args[i]] = nnvm::ShapeTypeCast(
          arg_shape_data + arg_ind_ptr[i], arg_shape_data + arg_ind_ptr[i+1]);
    }
  } else {
    std::unordered_map<std::string, TShape> kwargs;
    for (mx_uint i = 0; i < num_args; ++i) {
      kwargs[keys[i]] = nnvm::ShapeTypeCast(
          arg_shape_data + arg_ind_ptr[i], arg_shape_data + arg_ind_ptr[i+1]);
    }
    mxnet::MatchArguments(g.indexed_graph(), kwargs, &arg_shapes, "InferShape");
  }

  try {
    g = mxnet::exec::InferShape(std::move(g), std::move(arg_shapes), "__shape__");
  } catch (const mxnet::op::InferShapeError &err) {
    throw dmlc::Error(err.msg);
  }

  // copy back
  CopyAttr(g.indexed_graph(), g.GetAttr<nnvm::ShapeVector>("shape"),
           &(ret->arg_shapes), &(ret->out_shapes), &(ret->aux_shapes));

  // copy data back
  MXAPIThreadLocalEntry::SetupShapeArrayReturnWithBuffer(ret->arg_shapes,
      &(ret->arg_shape_ndim), &(ret->arg_shape_data), &(ret->arg_shape_buffer));
  MXAPIThreadLocalEntry::SetupShapeArrayReturnWithBuffer(ret->out_shapes,
      &(ret->out_shape_ndim), &(ret->out_shape_data), &(ret->out_shape_buffer));
  MXAPIThreadLocalEntry::SetupShapeArrayReturnWithBuffer(ret->aux_shapes,
      &(ret->aux_shape_ndim), &(ret->aux_shape_data), &(ret->aux_shape_buffer));
  *in_shape_size = static_cast<mx_uint>(ret->arg_shapes.size());
  *in_shape_ndim = dmlc::BeginPtr(ret->arg_shape_ndim);
  *in_shape_data = dmlc::BeginPtr(ret->arg_shape_data);
  *out_shape_size = static_cast<mx_uint>(ret->out_shapes.size());
  *out_shape_ndim = dmlc::BeginPtr(ret->out_shape_ndim);
  *out_shape_data = dmlc::BeginPtr(ret->out_shape_data);
  *aux_shape_size = static_cast<mx_uint>(ret->aux_shapes.size());
  *aux_shape_ndim = dmlc::BeginPtr(ret->aux_shape_ndim);
  *aux_shape_data = dmlc::BeginPtr(ret->aux_shape_data);
  // mark complete
  *complete = (g.GetAttr<size_t>("shape_num_unknown_nodes") == 0);
  API_END();
}

int MXSymbolInferShapePartial(SymbolHandle sym,
                              mx_uint num_args,
                              const char** keys,
                              const mx_uint *arg_ind_ptr,
                              const mx_uint *arg_shape_data,
                              mx_uint *in_shape_size,
                              const mx_uint **in_shape_ndim,
                              const mx_uint ***in_shape_data,
                              mx_uint *out_shape_size,
                              const mx_uint **out_shape_ndim,
                              const mx_uint ***out_shape_data,
                              mx_uint *aux_shape_size,
                              const mx_uint **aux_shape_ndim,
                              const mx_uint ***aux_shape_data,
                              int *complete) {
  int succ;
  *complete = 1;
  return MXSymbolInferShape(sym, num_args, keys,
                            arg_ind_ptr, arg_shape_data,
                            in_shape_size, in_shape_ndim, in_shape_data,
                            out_shape_size, out_shape_ndim, out_shape_data,
                            aux_shape_size, aux_shape_ndim, aux_shape_data,
                            &succ);
}

int MXSymbolInferType(SymbolHandle sym,
                      mx_uint num_args,
                      const char** keys,
                      const int *arg_type_data,
                      mx_uint *in_type_size,
                      const int **in_type_data,
                      mx_uint *out_type_size,
                      const int **out_type_data,
                      mx_uint *aux_type_size,
                      const int **aux_type_data,
                      int *complete) {
  nnvm::Symbol *s = static_cast<nnvm::Symbol*>(sym);
  MXAPIThreadLocalEntry *ret = MXAPIThreadLocalStore::Get();
  API_BEGIN();
  nnvm::Graph g = Symbol2Graph(*s);
  nnvm::DTypeVector arg_types(g.indexed_graph().input_nodes().size(), -1);
  if (keys == nullptr && num_args != 0) {
    std::vector<uint32_t> read_only_args = mxnet::ReadOnlyArgIndices(g.indexed_graph());
    CHECK_LE(num_args, read_only_args.size());
    for (mx_uint i = 0; i < num_args; ++i) {
      arg_types[read_only_args[i]] = arg_type_data[i];
    }
  } else {
    std::unordered_map<std::string, int> kwargs;
    for (mx_uint i = 0; i < num_args; ++i) {
      kwargs[keys[i]] = arg_type_data[i];
    }
    mxnet::MatchArguments(g.indexed_graph(), kwargs, &arg_types, "InferType");
  }

  g = mxnet::exec::InferType(std::move(g), std::move(arg_types), "__dtype__");
  // copy back
  CopyAttr(g.indexed_graph(), g.GetAttr<nnvm::DTypeVector>("dtype"),
           &(ret->arg_types), &(ret->out_types), &(ret->aux_types));

  *in_type_size = static_cast<mx_uint>(ret->arg_types.size());
  *in_type_data = dmlc::BeginPtr(ret->arg_types);
  *out_type_size = static_cast<mx_uint>(ret->out_types.size());
  *out_type_data = dmlc::BeginPtr(ret->out_types);
  *aux_type_size = static_cast<mx_uint>(ret->aux_types.size());
  *aux_type_data = dmlc::BeginPtr(ret->aux_types);
  *complete = (g.GetAttr<size_t>("dtype_num_unknown_nodes") == 0);
  API_END();
}

int MXSymbolGrad(SymbolHandle sym, mx_uint num_wrt, const char** wrt, SymbolHandle* out) {
  API_BEGIN();
  LOG(FATAL) << "not implemented";
  API_END();
}

int MXQuantizeSymbol(SymbolHandle sym_handle,
                     SymbolHandle *ret_sym_handle,
                     const mx_uint num_excluded_op_names,
                     const char **excluded_op_names,
                     const mx_uint num_offline,
                     const char **offline_params,
                     const char *quantized_dtype,
                     const bool calib_quantize) {
  nnvm::Symbol *s = new nnvm::Symbol();
  API_BEGIN();
  nnvm::Symbol *sym = static_cast<nnvm::Symbol*>(sym_handle);
  nnvm::Graph g = Symbol2Graph(*sym);
  std::unordered_set<std::string> excluded_node_names;
  for (size_t i = 0; i < num_excluded_op_names; ++i) {
    excluded_node_names.emplace(excluded_op_names[i]);
  }
  std::unordered_set<std::string> offline;
  for (size_t i = 0; i < num_offline; ++i) {
    offline.emplace(offline_params[i]);
  }
  std::string quantized_type(quantized_dtype);
  g.attrs["excluded_nodes"] = std::make_shared<nnvm::any>(std::move(excluded_node_names));
  g.attrs["offline_params"] = std::make_shared<nnvm::any>(std::move(offline));
  g.attrs["quantized_dtype"] = std::make_shared<nnvm::any>(std::move(quantized_type));
  g.attrs["calib_quantize"] = std::make_shared<nnvm::any>(calib_quantize);
  g = ApplyPass(std::move(g), "QuantizeGraph");
  s->outputs = g.outputs;
  *ret_sym_handle = s;
  API_END_HANDLE_ERROR(delete s);
}

int MXSetCalibTableToQuantizedSymbol(SymbolHandle qsym_handle,
                                     const mx_uint num_layers,
                                     const char** layer_names,
                                     const float* min_ranges,
                                     const float* max_ranges,
                                     SymbolHandle* ret_qsym_handle) {
  nnvm::Symbol* s = new nnvm::Symbol();
  API_BEGIN();
  nnvm::Symbol* sym = static_cast<nnvm::Symbol*>(qsym_handle);
  nnvm::Graph g = Symbol2Graph(*sym);
  const std::string prefix = "quantized_";
  std::unordered_map<std::string, std::pair<float, float>> calib_table;
  for (size_t i = 0; i < num_layers; ++i) {
    calib_table.emplace(prefix+layer_names[i], std::make_pair(min_ranges[i], max_ranges[i]));
  }
  g.attrs["calib_table"] = std::make_shared<nnvm::any>(std::move(calib_table));
  g = ApplyPass(std::move(g), "SetCalibTableToQuantizedGraph");
  s->outputs = g.outputs;
  *ret_qsym_handle = s;
  API_END_HANDLE_ERROR(delete s);
}

int MXGenBackendSubgraph(SymbolHandle sym_handle, const char *backend,
                         SymbolHandle *ret_sym_handle) {
  nnvm::Symbol *s = new nnvm::Symbol();
  API_BEGIN();
  nnvm::Symbol *sym = static_cast<nnvm::Symbol *>(sym_handle);
  *s = sym->Copy();
  nnvm::Graph g = Symbol2Graph(*s);
  mxnet::op::SubgraphPropertyPtr property =
      mxnet::op::SubgraphPropertyRegistry::Get()->CreateSubgraphProperty(
          backend);
  g.attrs["subgraph_property"] =
      std::make_shared<nnvm::any>(std::move(property));
  g = ApplyPass(std::move(g), "PartitionGraph");
  s->outputs = g.outputs;
  *ret_sym_handle = s;
  API_END_HANDLE_ERROR(delete s);
}
