# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

from __future__ import print_function
import sys
import os
import time
import multiprocessing as mp
import unittest
import mxnet as mx
import numpy as np
import unittest
from nose.tools import assert_raises
from mxnet.test_utils import check_consistency, set_default_context, assert_almost_equal
from mxnet.base import MXNetError
from mxnet import autograd
from numpy.testing import assert_allclose

curr_path = os.path.dirname(os.path.abspath(os.path.expanduser(__file__)))
sys.path.insert(0, os.path.join(curr_path, '../unittest'))
from common import setup_module, with_seed, teardown, assert_raises_cudnn_not_satisfied
from test_operator import *
from test_optimizer import *
from test_random import *
from test_exc_handling import *
#from test_rnn import *
from test_sparse_ndarray import *
from test_sparse_operator import *
from test_ndarray import *
from test_subgraph_op import *
from test_contrib_operator import test_multibox_target_op

set_default_context(mx.gpu(0))
del test_support_vector_machine_l1_svm  # noqa
del test_support_vector_machine_l2_svm  # noqa


def check_countsketch(in_dim,out_dim,n):
    data = mx.sym.Variable("data")
    h = mx.sym.Variable("h")
    s = mx.sym.Variable("s")
    sym = mx.sym.contrib.count_sketch(data=data, h=h, s=s, name='countsketch',out_dim = out_dim)
    shape = [(n,in_dim), (1,in_dim),(1,in_dim)]     #shape of input x, hash h and hash s

    arr = [mx.nd.empty(shape[i]) for i in range(3)]
    arr_grad = [mx.nd.empty(shape[i]) for i in range(3)]
    x = np.random.uniform(-10, 10, shape[0])
    arr[0][:] = x                                 #input x
    h = np.random.randint(0, out_dim, shape[1])
    arr[1][:] = h                                 #hash h
    s = np.random.randint(0, 2, shape[2])*2-np.ones(shape[2])
    arr[2][:] = s                                 #hash s
    locations = {"data": x, "h": h, "s": s}
    a = np.zeros((n,out_dim))
    temp = np.multiply(x, s)
    for num_sample in np.arange(0,n):
        for idx in np.arange(0,in_dim):
            a[num_sample][h[0][idx]] += temp[num_sample][idx]
    check_symbolic_forward(sym, locations, [a], rtol=1e-3, atol=1e-5, ctx=mx.gpu(0))
    out_grad = mx.nd.empty((n,out_dim))
    out_grad[:] = np.random.normal(-3, 3, (n,out_dim))
    a = np.zeros((n,in_dim))
    for j in np.arange(0,n):
        for i in np.arange(0,in_dim):
            a[j,i] = out_grad.asnumpy()[j, h[0,i]] * s[0,i]
    check_symbolic_backward(sym, locations, [out_grad], [a], rtol=1e-3, atol=1e-5, ctx=mx.gpu(0))


@with_seed()
def test_countsketch():
    minindim = 40
    maxindim = 100
    minoutdim = 5
    maxoutdim = 30
    maxn = 200
    in_dim = np.random.randint(minindim, maxindim)
    out_dim = np.random.randint(minoutdim, maxoutdim)
    n = np.random.randint(1, maxn)
    check_countsketch(in_dim, out_dim, n)


def check_ifft(shape):
    shape_old = shape
    if len(shape) == 2:
        if shape[1]%2 != 0:
            lst = list(shape)
            lst[1] = lst[1]*2
            shape = tuple(lst)
            shape_old = shape
        shape = (shape[0],shape[1]*2)
    if len(shape) == 4:
        if shape[3]%2 != 0:
            lst = list(shape)
            lst[3] = lst[3]*2
            shape = tuple(lst)
            shape_old = shape
        shape = (shape[0],shape[1],shape[2],shape[3]*2)
    sym = mx.sym.contrib.ifft(name='ifft', compute_size = 128)
    init = [np.random.normal(size=shape, scale=1.0)]
    arr_grad = [mx.nd.empty(shape)]
    ctx_list = [{'ctx': mx.gpu(0),'ifft_data': shape, 'type_dict': {'ifft_data': np.float32}}]
    exe_list = [sym.simple_bind(args_grad=arr_grad,**ctx) for ctx in ctx_list]

    for exe in exe_list:
        for arr, iarr in zip(exe.arg_arrays, init):
            arr[:] = iarr.astype(arr.dtype)
    # forward
    for exe in exe_list:
        exe.forward(is_train= True)
        out1 = [exe.outputs[0].asnumpy() for exe in exe_list]

    if len(shape) == 2:
        init_complex = np.zeros(shape_old,dtype = np.complex64)
        for i in range(0,shape_old[1]):
            init_complex.real[:,i] = init[0][:,2*i]
            init_complex.imag[:,i] = init[0][:,2*i+1]
        a = np.fft.ifft(init_complex, n=None, axis=-1, norm=None)
        assert_almost_equal(a.real, out1[0]/shape_old[1],rtol=1e-3, atol=1e-5)

    if len(shape) == 4:
        init_complex = np.zeros(shape_old,dtype = np.complex64)
        for i in range(0,shape_old[3]):
            init_complex.real[:,:,:,i] = init[0][:,:,:,2*i]
            init_complex.imag[:,:,:,i] = init[0][:,:,:,2*i+1]
        a = np.fft.ifft(init_complex, n=None, axis=-1, norm=None)
        assert_almost_equal(a.real, out1[0]/shape_old[3],rtol=1e-3, atol=1e-5)
    # backward
    if len(shape) == 2:
        out_grad = mx.nd.empty(shape_old)
        out_grad[:] = np.random.normal(-3, 3, shape_old)
        for exe in exe_list:
            exe.backward([out_grad])
            temp = exe.grad_arrays[0].asnumpy()
            temp = np.zeros(shape_old)
            for i in range(shape_old[1]):
                temp[:,i] = exe.grad_arrays[0].asnumpy()[:,2*i]

        a = np.fft.fft(out_grad.asnumpy(), n=None, axis=-1, norm=None)
        assert_almost_equal(a.real, temp, rtol=1e-3, atol=1e-5)
    if len(shape) == 4:
        out_grad = mx.nd.empty(shape_old)
        out_grad[:] = np.random.normal(-3, 3, shape_old)
        for exe in exe_list:
            exe.backward([out_grad])
            temp = exe.grad_arrays[0].asnumpy()
            temp = np.zeros(shape_old)
            for i in range(shape_old[3]):
                temp[:,:,:,i] = exe.grad_arrays[0].asnumpy()[:,:,:,2*i]

        a = np.fft.fft(out_grad.asnumpy(), n=None, axis=-1, norm=None)
        assert_almost_equal(a.real, temp, rtol=1e-3, atol=1e-5)

@with_seed()
def test_ifft():
    nrepeat = 2
    maxdim = 10
    for repeat in range(nrepeat):
        for order in [2,4]:
            shape = tuple(np.random.randint(1, maxdim, size=order))
            check_ifft(shape)


def check_fft(shape):
    sym = mx.sym.contrib.fft(name='fft', compute_size = 128)
    if len(shape) == 2:
        if shape[1]%2 != 0:
            lst = list(shape)
            lst[1] = lst[1]*2
            shape = tuple(lst)
            shape_old = shape
    if len(shape) == 4:
        if shape[3]%2 != 0:
            lst = list(shape)
            lst[3] = lst[3]*2
            shape = tuple(lst)
            shape_old = shape
    init = [np.random.normal(size=shape, scale=1.0)]
    arr_grad = [mx.nd.empty(shape)]
    ctx_list = [{'ctx': mx.gpu(0),'fft_data': shape, 'type_dict': {'fft_data': np.float32}}]
    exe_list = [sym.simple_bind(args_grad=arr_grad,**ctx) for ctx in ctx_list]

    for exe in exe_list:
        for arr, iarr in zip(exe.arg_arrays, init):
            arr[:] = iarr.astype(arr.dtype)
    # forward
    for exe in exe_list:
        exe.forward(is_train=True)
    out1 = [exe.outputs[0].asnumpy() for exe in exe_list]
    out = np.fft.fft(init, n=None, axis=-1, norm=None)
    if len(shape) == 2:
        out = np.reshape(out,(out.shape[1],out.shape[2]))
        out2 = np.append(out.real, out.imag, axis = 1)
        a = np.zeros(out1[0].shape)
        p = 0
        for i in range(out2.shape[1]//2):
            a[:,p] = out2[:,i]
            a[:,p+1] = out2[:,i+out2.shape[1]//2]
            p = p+2

    if len(shape) == 4:
        out = np.reshape(out,(out.shape[1],out.shape[2],out.shape[3],out.shape[4]))
        out2 = np.append(out.real, out.imag, axis = 1)
        a = np.zeros(out1[0].shape)
        for i in range(out1[0].shape[0]):
            for j in range(out1[0].shape[1]):
                p = 0
                for k in range(out2.shape[3]):
                    a[i,j,:,p] = out2[i,j,:,k]
                    a[i,j,:,p+1] = out2[i,j+out1[0].shape[1],:,k]
                    p = p+2

    assert_almost_equal(a, out1[0],rtol=1e-3, atol=1e-5)

    # backward
    if len(shape) == 2:
        out_grad = mx.nd.empty((shape[0],2*shape[1]))
        out_grad[:] = np.random.normal(-3, 3, (shape[0],2*shape[1]))
        # out_grad_to_complex
        out_grad_complex = np.zeros(shape,dtype = np.complex64)
        for i in range(0,shape[1]):
            out_grad_complex.real[:,i] = out_grad.asnumpy()[:,2*i]
            out_grad_complex.imag[:,i] = out_grad.asnumpy()[:,2*i+1]
        for exe in exe_list:
            exe.backward([out_grad])
        a = np.fft.ifft(out_grad_complex, n=None, axis=-1, norm=None)
        assert_almost_equal(a.real, exe.grad_arrays[0].asnumpy()/shape[1],rtol=1e-3, atol=1e-5)

    if len(shape) == 4:
        out_grad = mx.nd.empty(out1[0].shape)
        out_grad[:] = np.random.normal(-3, 3, out1[0].shape)
        # out_grad_to_complex
        out_grad_complex = np.zeros(shape,dtype = np.complex64)
        for i in range(0,shape[3]):
            out_grad_complex.real[:,:,:,i] = out_grad.asnumpy()[:,:,:,2*i]
            out_grad_complex.imag[:,:,:,i] = out_grad.asnumpy()[:,:,:,2*i+1]
        for exe in exe_list:
            exe.backward([out_grad])
        a = np.fft.ifft(out_grad_complex, n=None, axis=-1, norm=None)
        assert_almost_equal(a.real, exe.grad_arrays[0].asnumpy()/shape[3],rtol=1e-3, atol=1e-5)

@with_seed()
def test_fft():
    nrepeat = 2
    maxdim = 10
    for repeat in range(nrepeat):
        for order in [2,4]:
            shape = tuple(np.random.randint(1, maxdim, size=order))
            check_fft(shape)


@with_seed()
def test_batchnorm_with_type():
  ctx_list_v1_2D = [
    {'ctx': mx.cpu(0), 'norm_data': (10, 2, 10, 10), 'type_dict': {'norm_data': np.float32}},
    {'ctx': mx.gpu(0), 'norm_data': (10, 2, 10, 10), 'type_dict': {'norm_data': np.float32}},
  ]

  ctx_list_v2_2D = [
    {'ctx': mx.cpu(0), 'norm_data': (5, 2, 5, 5), 'type_dict': {'norm_data': np.float32}},
    {'ctx': mx.cpu(0), 'norm_data': (5, 2, 5, 5), 'type_dict': {'norm_data': np.float16}},
    {'ctx': mx.cpu(0), 'norm_data': (5, 2, 5, 5), 'type_dict': {'norm_data': np.float64}},
    {'ctx': mx.gpu(0), 'norm_data': (5, 2, 5, 5), 'type_dict': {'norm_data': np.float32}},
    {'ctx': mx.gpu(0), 'norm_data': (5, 2, 5, 5), 'type_dict': {'norm_data': np.float16}},
    {'ctx': mx.gpu(0), 'norm_data': (5, 2, 5, 5), 'type_dict': {'norm_data': np.float64}},
  ]

  ctx_list_v2_1D = [
    {'ctx': mx.cpu(0), 'norm_data': (5, 2, 5), 'type_dict': {'norm_data': np.float16}},
    {'ctx': mx.cpu(0), 'norm_data': (5, 2, 5), 'type_dict': {'norm_data': np.float32}},
    {'ctx': mx.cpu(0), 'norm_data': (5, 2, 5), 'type_dict': {'norm_data': np.float64}},
    {'ctx': mx.gpu(0), 'norm_data': (5, 2, 5), 'type_dict': {'norm_data': np.float16}},
    {'ctx': mx.gpu(0), 'norm_data': (5, 2, 5), 'type_dict': {'norm_data': np.float32}},
    {'ctx': mx.gpu(0), 'norm_data': (5, 2, 5), 'type_dict': {'norm_data': np.float64}},
  ]

  ctx_list_v2_3D = [
    {'ctx': mx.cpu(0), 'norm_data': (3, 2, 3, 2, 3), 'type_dict': {'norm_data': np.float16}},
    {'ctx': mx.cpu(0), 'norm_data': (3, 2, 3, 2, 3), 'type_dict': {'norm_data': np.float32}},
    {'ctx': mx.cpu(0), 'norm_data': (3, 2, 3, 2, 3), 'type_dict': {'norm_data': np.float64}},
    {'ctx': mx.gpu(0), 'norm_data': (3, 2, 3, 2, 3), 'type_dict': {'norm_data': np.float16}},
    {'ctx': mx.gpu(0), 'norm_data': (3, 2, 3, 2, 3), 'type_dict': {'norm_data': np.float32}},
    {'ctx': mx.gpu(0), 'norm_data': (3, 2, 3, 2, 3), 'type_dict': {'norm_data': np.float64}}
  ]

  # V1, 2D
  sym = mx.sym.BatchNorm_v1(name='norm', fix_gamma=False)
  check_consistency(sym, ctx_list_v1_2D)
  sym = mx.sym.BatchNorm_v1(name='norm', fix_gamma=True)
  check_consistency(sym, ctx_list_v1_2D)


  # V2, 2D
  sym = mx.sym.BatchNorm(name='norm', fix_gamma=False, cudnn_off=True)
  check_consistency(sym, ctx_list_v2_2D)
  sym = mx.sym.BatchNorm(name='norm', fix_gamma=False, cudnn_off=True)
  check_consistency(sym, ctx_list_v2_2D)
  sym = mx.sym.BatchNorm(name='norm', fix_gamma=True, cudnn_off=True)
  check_consistency(sym, ctx_list_v2_2D)
  sym = mx.sym.BatchNorm(name='norm', fix_gamma=True, cudnn_off=True)
  check_consistency(sym, ctx_list_v2_2D)

  # V2, 1D
  sym = mx.sym.BatchNorm(name='norm', fix_gamma=False, cudnn_off=True)
  check_consistency(sym, ctx_list_v2_1D)
  sym = mx.sym.BatchNorm(name='norm', fix_gamma=False, cudnn_off=True)
  check_consistency(sym, ctx_list_v2_1D)
  sym = mx.sym.BatchNorm(name='norm', fix_gamma=True, cudnn_off=True)
  check_consistency(sym, ctx_list_v2_1D)
  sym = mx.sym.BatchNorm(name='norm', fix_gamma=True, cudnn_off=True)
  check_consistency(sym, ctx_list_v2_1D)
  #
  # # V2, 3D
  sym = mx.sym.BatchNorm(name='norm', fix_gamma=False, cudnn_off=True)
  check_consistency(sym, ctx_list_v2_3D)
  sym = mx.sym.BatchNorm(name='norm', fix_gamma=True, cudnn_off=True)
  check_consistency(sym, ctx_list_v2_3D)


@with_seed()
def test_batchnorm_versions():
  def test_batchnorm_versions_helper(batchnorm_op_list, data, fix_gamma, use_global_stats):
    ctx_list = []
    sym_list = []
    # BatchNormV1 cpu
    if 'batchnorm_v1_cpu' in batchnorm_op_list:
      ctx_list.append({'ctx': mx.cpu(0), 'batchnorm_data': data, 'type_dict': {'batchnorm_data': np.float32}})
      sym_list.append(mx.sym.BatchNorm_v1(fix_gamma=fix_gamma,
                                          use_global_stats=use_global_stats,
                                          name='batchnorm'))

    # BatchNormV1 gpu (organic)
    if 'batchnorm_v1_gpu' in batchnorm_op_list:
      ctx_list.append({'ctx': mx.gpu(0), 'batchnorm_data': data, 'type_dict': {'batchnorm_data': np.float32}})
      sym_list.append(mx.sym.BatchNorm_v1(fix_gamma=fix_gamma,
                                          use_global_stats=use_global_stats,
                                          name='batchnorm'))

    # BatchNorm cpu
    if 'batchnorm_cpu' in batchnorm_op_list:
      ctx_list.append({'ctx': mx.cpu(0), 'batchnorm_data': data, 'type_dict': {'batchnorm_data': np.float32}})
      sym_list.append(mx.sym.BatchNorm(fix_gamma=fix_gamma,
                                       use_global_stats=use_global_stats,
                                       name='batchnorm'))

    # BatchNorm gpu (organic)
    if 'batchnorm_gpu' in batchnorm_op_list:
      ctx_list.append({'ctx': mx.gpu(0), 'batchnorm_data': data, 'type_dict': {'batchnorm_data': np.float32}})
      sym_list.append(mx.sym.BatchNorm(fix_gamma=fix_gamma,
                                       use_global_stats=use_global_stats,
                                       name='batchnorm', cudnn_off=True))

    # BatchNorm gpu cudnn (if cudnn is enabled)
    if 'batchnorm_cudnn' in batchnorm_op_list:
      ctx_list.append({'ctx': mx.gpu(0), 'batchnorm_data': data, 'type_dict': {'batchnorm_data': np.float32}})
      sym_list.append(mx.sym.BatchNorm(fix_gamma=fix_gamma,
                                       use_global_stats=use_global_stats,
                                       name='batchnorm', cudnn_off=False))

    check_consistency(sym_list, ctx_list)

  def test_1d_batchnorm(fix_gamma, use_global_stats):
    data = (2, 3, 20)
    test_batchnorm_versions_helper(batchnorm_op_list=['batchnorm_cpu',
                                                      'batchnorm_gpu', 'batchnorm_cudnn'],
                                   data=data,
                                   fix_gamma=fix_gamma, use_global_stats=use_global_stats)

  def test_2d_batchnorm(fix_gamma, use_global_stats):
    data = (2, 3, 10, 10)
    test_batchnorm_versions_helper(batchnorm_op_list=['batchnorm_v1_cpu', 'batchnorm_v1_gpu',
                                                      'batchnorm_cpu',
                                                      'batchnorm_gpu', 'batchnorm_cudnn'],
                                   data=data,
                                   fix_gamma=fix_gamma, use_global_stats=use_global_stats)

  def test_3d_batchnorm(fix_gamma, use_global_stats):
    data = (2, 3, 3, 5, 5)
    test_batchnorm_versions_helper(batchnorm_op_list=['batchnorm_cpu',
                                                      'batchnorm_gpu'],
                                   data=data,
                                   fix_gamma=fix_gamma, use_global_stats=use_global_stats)

  test_1d_batchnorm(True,  False)
  test_1d_batchnorm(False, False)
  test_1d_batchnorm(False, True)
  test_1d_batchnorm(True,  True)

  test_2d_batchnorm(True,  False)
  test_2d_batchnorm(False, False)
  test_2d_batchnorm(False, True)
  test_2d_batchnorm(True,  True)

  test_3d_batchnorm(True,  False)
  test_3d_batchnorm(False, False)
  test_3d_batchnorm(False, True)
  test_3d_batchnorm(True,  True)


@with_seed(1234)
@assert_raises_cudnn_not_satisfied(min_version='5.1.10')
def test_convolution_with_type():
    sym1 = mx.sym.Convolution(num_filter=3, kernel=(3,3), name='conv')

    data = mx.sym.Variable('conv_data')
    w = mx.sym.Variable('conv_weight')
    b = mx.sym.Variable('conv_bias')
    w = mx.sym.transpose(w, axes=(0,2,3,1))
    sym2 = mx.sym.transpose(data, axes=(0,2,3,1))
    sym2 = mx.sym.Convolution(sym2, w, b, layout='NHWC', num_filter=3, kernel=(3,3))
    sym2 = mx.sym.transpose(sym2, axes=(0,3,1,2), name='conv')

    sym = [sym1, sym1, sym1, sym1, sym1, sym2, sym2]
    ctx_list = [{'ctx': mx.gpu(0), 'conv_data': (2, 2, 10, 10), 'type_dict': {'conv_data': np.float64}},
                {'ctx': mx.gpu(0), 'conv_data': (2, 2, 10, 10), 'type_dict': {'conv_data': np.float32}},
                {'ctx': mx.gpu(0), 'conv_data': (2, 2, 10, 10), 'type_dict': {'conv_data': np.float16}},
                {'ctx': mx.cpu(0), 'conv_data': (2, 2, 10, 10), 'type_dict': {'conv_data': np.float64}},
                {'ctx': mx.cpu(0), 'conv_data': (2, 2, 10, 10), 'type_dict': {'conv_data': np.float32}},
                # NHWC
                {'ctx': mx.gpu(0), 'conv_data': (2, 2, 10, 10), 'conv_weight': (3, 2, 3, 3),
                 'type_dict': {'conv_data': np.float32, 'conv_weight': np.float32}},
                {'ctx': mx.gpu(0), 'conv_data': (2, 2, 10, 10), 'conv_weight': (3, 2, 3, 3),
                 'type_dict': {'conv_data': np.float16, 'conv_weight': np.float16}}
                ]
    # wider tolerance needed for true-fp16 NCHW test above
    tol = {np.dtype(np.float16): 0.5,
               np.dtype(np.float32): 1e-3,
               np.dtype(np.float64): 1e-5,
               np.dtype(np.uint8): 0,
               np.dtype(np.int32): 0}
    check_consistency(sym, ctx_list, tol=tol)
    # test ability to turn off training on bias
    check_consistency(sym, ctx_list, grad_req={'conv_data': 'write', 'conv_weight': 'write', 'conv_bias': 'null'}, tol=tol)


# Apply N symbols against each of M contexts, checking that all NxM combinations match.
def check_consistency_NxM(sym_list, ctx_list):
    # e.g. if sym_list=[sym1, sym2] and ctx_list=[ctx1, ctx2, ctx3], then resulting lists are:
    # sym_list=[sym1, sym1, sym1, sym2, sym2, sym2] and ctx_list=[ctx1, ctx2, ctx3, ctx1, ctx2, ctx3]
    check_consistency(np.repeat(sym_list, len(ctx_list)), ctx_list * len(sym_list), scale=0.5)


@unittest.skip("test fails intermittently. temporarily disabled till it gets fixed. tracked at https://github.com/apache/incubator-mxnet/issues/10141")
@with_seed()
def test_convolution_options():
    # 1D convolution
    ctx_list = [{'ctx': mx.gpu(0), 'conv_data': (2, 2, 7), 'type_dict': {'conv_data': np.float64}},
                {'ctx': mx.gpu(0), 'conv_data': (2, 2, 7), 'type_dict': {'conv_data': np.float32}},
                {'ctx': mx.gpu(0), 'conv_data': (2, 2, 7), 'type_dict': {'conv_data': np.float16}},
                {'ctx': mx.cpu(0), 'conv_data': (2, 2, 7), 'type_dict': {'conv_data': np.float64}},
                {'ctx': mx.cpu(0), 'conv_data': (2, 2, 7), 'type_dict': {'conv_data': np.float32}}]
    # Pad > 0
    sym = mx.sym.Convolution(layout='NCW', num_filter=3, kernel=(3,), pad=(1,), name='conv')
    sym_no_cudnn = mx.sym.Convolution(num_filter=3, kernel=(3,), pad=(1,), cudnn_off=True, name='conv')
    check_consistency_NxM([sym, sym_no_cudnn], ctx_list)
    # Stride > 1
    sym = mx.sym.Convolution(layout='NCW', num_filter=3, kernel=(3,), stride=(2,), name='conv')
    sym_no_cudnn = mx.sym.Convolution(num_filter=3, kernel=(3,), stride=(2,), cudnn_off=True, name='conv')
    check_consistency_NxM([sym, sym_no_cudnn], ctx_list)
    # Dilate > 1
    sym = mx.sym.Convolution(layout='NCW', num_filter=3, kernel=(3,), dilate=(2,), name='conv')
    sym_no_cudnn = mx.sym.Convolution(num_filter=3, kernel=(3,), dilate=(2,), cudnn_off=True, name='conv')
    check_consistency_NxM([sym, sym_no_cudnn], ctx_list)
    # 1x1 convolution
    sym = mx.sym.Convolution(layout='NCW', num_filter=3, kernel=(1,), pad=(0,), name='conv')
    sym_no_cudnn = mx.sym.Convolution(num_filter=3, kernel=(1,), pad=(0,), cudnn_off=True, name='conv')
    check_consistency_NxM([sym, sym_no_cudnn], ctx_list)

    # 2D convolution
    ctx_list = [{'ctx': mx.gpu(0), 'conv_data': (2, 2, 7, 7), 'type_dict': {'conv_data': np.float64}},
                {'ctx': mx.gpu(0), 'conv_data': (2, 2, 7, 7), 'type_dict': {'conv_data': np.float32}},
                {'ctx': mx.gpu(0), 'conv_data': (2, 2, 7, 7), 'type_dict': {'conv_data': np.float16}},
                {'ctx': mx.cpu(0), 'conv_data': (2, 2, 7, 7), 'type_dict': {'conv_data': np.float64}},
                {'ctx': mx.cpu(0), 'conv_data': (2, 2, 7, 7), 'type_dict': {'conv_data': np.float32}}]
    # Pad > 0
    sym = mx.sym.Convolution(num_filter=3, kernel=(3,3), pad=(1,1), name='conv')
    sym_no_cudnn = mx.sym.Convolution(num_filter=3, kernel=(3,3), pad=(1,1), cudnn_off=True, name='conv')
    check_consistency_NxM([sym, sym_no_cudnn], ctx_list)
    # Stride > 1
    sym = mx.sym.Convolution(num_filter=3, kernel=(3,3), stride=(2,2), name='conv')
    sym_no_cudnn = mx.sym.Convolution(num_filter=3, kernel=(3,3), stride=(2,2), cudnn_off=True, name='conv')
    check_consistency_NxM([sym, sym_no_cudnn], ctx_list)
    # Dilate > 1
    sym = mx.sym.Convolution(num_filter=3, kernel=(3,3), dilate=(2,2), name='conv')
    sym_no_cudnn = mx.sym.Convolution(num_filter=3, kernel=(3,3), dilate=(2,2), cudnn_off=True, name='conv')
    check_consistency_NxM([sym, sym_no_cudnn], ctx_list)
    # 1x1 convolution
    sym = mx.sym.Convolution(num_filter=3, kernel=(1,1), pad=(0,0), name='conv')
    sym_no_cudnn = mx.sym.Convolution(num_filter=3, kernel=(1,1), pad=(0,0), cudnn_off=True, name='conv')
    check_consistency_NxM([sym, sym_no_cudnn], ctx_list)

    # 3D convolution
    ctx_list = [{'ctx': mx.cpu(0), 'conv_data': (2, 2, 5, 7, 7), 'type_dict': {'conv_data': np.float64}},
                {'ctx': mx.cpu(0), 'conv_data': (2, 2, 5, 7, 7), 'type_dict': {'conv_data': np.float64}},
                {'ctx': mx.gpu(0), 'conv_data': (2, 2, 5, 7, 7), 'type_dict': {'conv_data': np.float64}},
                {'ctx': mx.gpu(0), 'conv_data': (2, 2, 5, 7, 7), 'type_dict': {'conv_data': np.float32}}]
    # Pad > 0
    sym = mx.sym.Convolution(num_filter=3, kernel=(2,3,3), pad=(1,1,1), name='conv')
    sym_no_cudnn = mx.sym.Convolution(num_filter=3, kernel=(2,3,3), pad=(1,1,1), cudnn_off=True, name='conv')
    check_consistency_NxM([sym, sym_no_cudnn], ctx_list)
    # Stride > 1
    sym = mx.sym.Convolution(num_filter=3, kernel=(2,3,3), stride=(2,2,2), name='conv')
    sym_no_cudnn = mx.sym.Convolution(num_filter=3, kernel=(2,3,3), stride=(2,2,2), cudnn_off=True, name='conv')
    check_consistency_NxM([sym, sym_no_cudnn], ctx_list)
    # 1x1 convolution
    sym = mx.sym.Convolution(num_filter=3, kernel=(1,1,1), pad=(0,0,0), name='conv')
    sym_no_cudnn = mx.sym.Convolution(num_filter=3, kernel=(1,1,1), pad=(0,0,0), cudnn_off=True, name='conv')
    check_consistency_NxM([sym, sym_no_cudnn], ctx_list)

# This test is designed to expose an issue with cudnn v7.1.4 algo find() when invoked with large c.
# Algos returned by find() can fail to run with grad_req='add' (wgrad kernel beta parameter == 1.0f).
@with_seed()
def test_convolution_large_c():
    problematic_c = 64 * 1024
    # The convolution accumulates many values, so set large tolerances.
    tol = {np.dtype(np.float32): 1,
           np.dtype(np.float64): 1}
    def test_1D_with_width(width, grad_req):
        ctx_list = [{'ctx': mx.gpu(0), 'conv_data': (1, problematic_c, width), 'type_dict': {'conv_data': np.float32}},
                    {'ctx': mx.gpu(0), 'conv_data': (1, problematic_c, width), 'type_dict': {'conv_data': np.float64}}]
        sym = mx.sym.Convolution(layout='NCW', num_filter=8, kernel=(2,), name='conv')
        check_consistency([sym, sym], ctx_list, tol=tol, grad_req=grad_req)

    def test_2D_with_width(width, grad_req):
        ctx_list = [{'ctx': mx.gpu(0), 'conv_data': (1, problematic_c, 2, width), 'type_dict': {'conv_data': np.float32}},
                    {'ctx': mx.gpu(0), 'conv_data': (1, problematic_c, 2, width), 'type_dict': {'conv_data': np.float64}}]
        sym = mx.sym.Convolution(layout='NCHW', num_filter=4, kernel=(2,2), name='conv')
        check_consistency([sym, sym], ctx_list, tol=tol, grad_req=grad_req)

    # Run with different data tensor shapes to run cudnnFind() multiple times.
    # First, populate algo and op caches with models that always use cudnnFind() (req == 'write').
    # Then run models that must avoid cached cudnnFind() results in some cases (req == 'add').
    widths = [4, 16, 64]
    for req in ['write', 'add']:
        for width in widths:
            test_1D_with_width(width, req)
            test_2D_with_width(width, req)


# This test is designed to expose an issue with cudnn v7.1.4 algo find() when invoked with large c.
# Algos returned by find() can fail to run with grad_req='add' (wgrad kernel beta parameter == 1.0f).
@with_seed()
def test_deconvolution_large_c():
    problematic_c = 64 * 1024
    # The deconvolution accumulates many values, so set large tolerances.
    tol = {np.dtype(np.float32): 1,
           np.dtype(np.float64): 1}
    def test_1D_with_width(width, grad_req):
        ctx_list = [{'ctx': mx.gpu(0), 'deconv_data': (1, 8, width), 'type_dict': {'deconv_data': np.float32}},
                    {'ctx': mx.gpu(0), 'deconv_data': (1, 8, width), 'type_dict': {'deconv_data': np.float64}}]
        sym = mx.sym.Deconvolution(layout='NCW', num_filter=problematic_c, kernel=(2,), name='deconv')
        check_consistency([sym, sym], ctx_list, tol=tol, grad_req=grad_req)

    def test_2D_with_width(width, grad_req):
        ctx_list = [{'ctx': mx.gpu(0), 'deconv_data': (1, 8, 2, width), 'type_dict': {'deconv_data': np.float32}},
                    {'ctx': mx.gpu(0), 'deconv_data': (1, 8, 2, width), 'type_dict': {'deconv_data': np.float64}}]
        sym = mx.sym.Deconvolution(layout='NCHW', num_filter=problematic_c, kernel=(2,2), name='deconv')
        check_consistency([sym, sym], ctx_list, tol=tol, grad_req=grad_req)

    # Run with different data tensor shapes to run cudnnFind() multiple times.
    # First, populate algo and op caches with models that always use cudnnFind() (req == 'write').
    # Then run models that must avoid cached cudnnFind() results in some cases (req == 'add').
    widths = [4, 16, 64]
    for req in ['write', 'add']:
        for width in widths:
            test_1D_with_width(width, req)
            test_2D_with_width(width, req)


@with_seed()
def test_convolution_versions():
    # 2D convolution NCHW
    ctx_list = [{'ctx': mx.cpu(0), 'conv_data': (2, 2, 7, 7), 'type_dict': {'conv_data': np.float32}},
                {'ctx': mx.gpu(0), 'conv_data': (2, 2, 7, 7), 'type_dict': {'conv_data': np.float32}},
                {'ctx': mx.gpu(0), 'conv_data': (2, 2, 7, 7), 'type_dict': {'conv_data': np.float32}},
                {'ctx': mx.cpu(0), 'conv_data': (2, 2, 7, 7), 'type_dict': {'conv_data': np.float32}},
                {'ctx': mx.gpu(0), 'conv_data': (2, 2, 7, 7), 'type_dict': {'conv_data': np.float32}}]
    conv_v1_cpu = mx.sym.Convolution_v1(num_filter=3, kernel=(3,3), pad=(1,1), name='conv')
    conv_v1_gpu = mx.sym.Convolution_v1(num_filter=3, kernel=(3,3), pad=(1,1), cudnn_off=True, name='conv')
    conv_cudnn = mx.sym.Convolution(num_filter=3, kernel=(3,3), pad=(1,1), name='conv')
    conv_cpu = mx.sym.Convolution(num_filter=3, kernel=(3,3), pad=(1,1), name='conv')
    conv_gpu = mx.sym.Convolution(num_filter=3, kernel=(3,3), pad=(1,1), cudnn_off=True, name='conv')
    syms = [conv_v1_cpu, conv_v1_gpu, conv_cudnn, conv_cpu, conv_gpu]
    check_consistency(syms, ctx_list)

    # 3D convolution NCDHW
    ctx_list = [{'ctx': mx.gpu(0), 'conv_data': (2, 2, 5, 7, 7), 'type_dict': {'conv_data': np.float32}},
                {'ctx': mx.cpu(0), 'conv_data': (2, 2, 5, 7, 7), 'type_dict': {'conv_data': np.float32}},
                {'ctx': mx.gpu(0), 'conv_data': (2, 2, 5, 7, 7), 'type_dict': {'conv_data': np.float32}}]
    conv_cudnn = mx.sym.Convolution(num_filter=3, kernel=(2,3,3), pad=(1,1,1), name='conv')
    conv_cpu = mx.sym.Convolution(num_filter=3, kernel=(2,3,3), pad=(1,1,1), name='conv')
    conv_gpu = mx.sym.Convolution(num_filter=3, kernel=(2,3,3), pad=(1,1,1), cudnn_off=True, name='conv')
    syms = [conv_cudnn, conv_cpu, conv_gpu]
    check_consistency(syms, ctx_list)


@with_seed()
def test_pooling_with_type():
    ctx_list = [{'ctx': mx.gpu(0), 'pool_data': (2, 2, 10, 10), 'type_dict': {'pool_data': np.float64}},
                {'ctx': mx.gpu(0), 'pool_data': (2, 2, 10, 10), 'type_dict': {'pool_data': np.float32}},
                {'ctx': mx.gpu(0), 'pool_data': (2, 2, 10, 10), 'type_dict': {'pool_data': np.float16}},
                {'ctx': mx.cpu(0), 'pool_data': (2, 2, 10, 10), 'type_dict': {'pool_data': np.float64}},
                {'ctx': mx.cpu(0), 'pool_data': (2, 2, 10, 10), 'type_dict': {'pool_data': np.float32}}]
    sym = mx.sym.Pooling(kernel=(3,3), pool_type='max', pooling_convention='valid', name='pool')
    check_consistency(sym, ctx_list, rand_type=np.float16)

    sym = mx.sym.Pooling(kernel=(3,3), pool_type='max', pooling_convention='full', name='pool')
    check_consistency(sym, ctx_list, rand_type=np.float16)

    sym = mx.sym.Pooling(kernel=(300,300), pool_type='max', global_pool=True, name='pool')
    check_consistency(sym, ctx_list, rand_type=np.float16)


@with_seed()
def test_deconvolution_with_type():
    # Test basic deconvolution without exercising stride, pad or dilation.
    # 1D deconvolution
    sym = mx.sym.Deconvolution(num_filter=3, kernel=(3,), name='deconv')
    ctx_list = [{'ctx': mx.gpu(0), 'deconv_data': (2, 2, 7), 'type_dict': {'deconv_data': np.float64}},
                {'ctx': mx.gpu(0), 'deconv_data': (2, 2, 7), 'type_dict': {'deconv_data': np.float32}},
                {'ctx': mx.gpu(0), 'deconv_data': (2, 2, 7), 'type_dict': {'deconv_data': np.float16}},
                {'ctx': mx.cpu(0), 'deconv_data': (2, 2, 7), 'type_dict': {'deconv_data': np.float64}},
                {'ctx': mx.cpu(0), 'deconv_data': (2, 2, 7), 'type_dict': {'deconv_data': np.float32}}]
    # wider tolerance needed for true-fp16 test above
    tol = {np.dtype(np.float16): 0.3,
               np.dtype(np.float32): 1e-3,
               np.dtype(np.float64): 1e-5,
               np.dtype(np.uint8): 0,
               np.dtype(np.int32): 0}
    check_consistency(sym, ctx_list, tol=tol)
    check_consistency(sym, ctx_list, tol=tol, grad_req="add")

    # 2D deconvolution
    sym = mx.sym.Deconvolution(num_filter=2, kernel=(3,3), name='deconv')
    ctx_list = [{'ctx': mx.gpu(0), 'deconv_data': (2, 2, 10, 10), 'type_dict': {'deconv_data': np.float64}},
                {'ctx': mx.gpu(0), 'deconv_data': (2, 2, 10, 10), 'type_dict': {'deconv_data': np.float32}},
                {'ctx': mx.gpu(0), 'deconv_data': (2, 2, 10, 10), 'type_dict': {'deconv_data': np.float16}},
                {'ctx': mx.cpu(0), 'deconv_data': (2, 2, 10, 10), 'type_dict': {'deconv_data': np.float64}},
                {'ctx': mx.cpu(0), 'deconv_data': (2, 2, 10, 10), 'type_dict': {'deconv_data': np.float32}}]
    # wider tolerance needed for true-fp16 test above
    tol = {np.dtype(np.float16): 0.3,
               np.dtype(np.float32): 1e-3,
               np.dtype(np.float64): 1e-5,
               np.dtype(np.uint8): 0,
               np.dtype(np.int32): 0}
    check_consistency(sym, ctx_list, tol=tol)
    check_consistency(sym, ctx_list, tol=tol, grad_req="add")


@with_seed()
def test_deconvolution_options():

    # 1D deconvolution
    ctx_list = [{'ctx': mx.gpu(0), 'deconv_data': (2, 2, 7), 'type_dict': {'deconv_data': np.float64}},
                {'ctx': mx.gpu(0), 'deconv_data': (2, 2, 7), 'type_dict': {'deconv_data': np.float32}},
                {'ctx': mx.gpu(0), 'deconv_data': (2, 2, 7), 'type_dict': {'deconv_data': np.float16}},
                {'ctx': mx.cpu(0), 'deconv_data': (2, 2, 7), 'type_dict': {'deconv_data': np.float64}},
                {'ctx': mx.cpu(0), 'deconv_data': (2, 2, 7), 'type_dict': {'deconv_data': np.float32}}]
    # Pad > 0
    sym = mx.sym.Deconvolution(layout='NCW', num_filter=3, kernel=(3,), pad=(1,), name='deconv')
    sym_no_cudnn = mx.sym.Deconvolution(num_filter=3, kernel=(3,), pad=(1,), cudnn_off=True, name='deconv')
    check_consistency_NxM([sym, sym_no_cudnn], ctx_list)
    # Stride > 1
    sym = mx.sym.Deconvolution(layout='NCW', num_filter=3, kernel=(3,), stride=(2,), name='deconv')
    sym_no_cudnn = mx.sym.Deconvolution(num_filter=3, kernel=(3,), stride=(2,), cudnn_off=True, name='deconv')
    check_consistency_NxM([sym, sym_no_cudnn], ctx_list)
    # Dilate > 1
    sym = mx.sym.Deconvolution(layout='NCW', num_filter=3, kernel=(3,), dilate=(2,), name='deconv')
    sym_no_cudnn = mx.sym.Deconvolution(num_filter=3, kernel=(3,), dilate=(2,), cudnn_off=True, name='deconv')
    check_consistency_NxM([sym, sym_no_cudnn], ctx_list)

    # 2D deconvolution
    ctx_list = [{'ctx': mx.gpu(0), 'deconv_data': (2, 8, 10, 10), 'type_dict': {'deconv_data': np.float64}},
                {'ctx': mx.gpu(0), 'deconv_data': (2, 8, 10, 10), 'type_dict': {'deconv_data': np.float32}},
                {'ctx': mx.gpu(0), 'deconv_data': (2, 8, 10, 10), 'type_dict': {'deconv_data': np.float16}},
                {'ctx': mx.cpu(0), 'deconv_data': (2, 8, 10, 10), 'type_dict': {'deconv_data': np.float64}},
                {'ctx': mx.cpu(0), 'deconv_data': (2, 8, 10, 10), 'type_dict': {'deconv_data': np.float32}}]
    # Pad > 0
    sym = mx.sym.Deconvolution(num_filter=2, kernel=(3,3), pad=(1,1), name='deconv')
    sym_no_cudnn = mx.sym.Deconvolution(num_filter=2, kernel=(3,3), pad=(1,1), cudnn_off=True, name='deconv')
    check_consistency_NxM([sym, sym_no_cudnn], ctx_list)
    # Stride > 1
    sym = mx.sym.Deconvolution(num_filter=2, kernel=(3,3), stride=(2,2), name='deconv')
    sym_no_cudnn = mx.sym.Deconvolution(num_filter=2, kernel=(3,3), stride=(2,2), cudnn_off=True, name='deconv')
    check_consistency_NxM([sym, sym_no_cudnn], ctx_list)
    # Dilate > 1
    sym = mx.sym.Deconvolution(num_filter=2, kernel=(3,3), dilate=(2,2), name='deconv')
    sym_no_cudnn = mx.sym.Deconvolution(num_filter=2, kernel=(3,3), dilate=(2,2), cudnn_off=True, name='deconv')
    check_consistency_NxM([sym, sym_no_cudnn], ctx_list)

#    # 3D deconvolution (not yet enabled)
#    ctx_list = [{'ctx': mx.cpu(0), 'conv_data': (2, 2, 5, 7, 7), 'type_dict': {'conv_data': np.float64}},
#                {'ctx': mx.cpu(0), 'conv_data': (2, 2, 5, 7, 7), 'type_dict': {'conv_data': np.float64}},
#                {'ctx': mx.gpu(0), 'conv_data': (2, 2, 5, 7, 7), 'type_dict': {'conv_data': np.float64}},
#                {'ctx': mx.gpu(0), 'conv_data': (2, 2, 5, 7, 7), 'type_dict': {'conv_data': np.float32}}]
#    # Pad > 0
#    sym = mx.sym.Convolution(num_filter=3, kernel=(2,3,3), pad=(1,1,1), name='conv')
#    sym_no_cudnn = mx.sym.Convolution(num_filter=3, kernel=(2,3,3), pad=(1,1,1), cudnn_off=True, name='conv')
#    check_consistency_NxM([sym, sym_no_cudnn], ctx_list)
#    # Stride > 1
#    sym = mx.sym.Convolution(num_filter=3, kernel=(2,3,3), stride=(2,2,2), name='conv')
#    sym_no_cudnn = mx.sym.Convolution(num_filter=3, kernel=(2,3,3), stride=(2,2,2), cudnn_off=True, name='conv')
#    check_consistency_NxM([sym, sym_no_cudnn], ctx_list)


@with_seed(1234)
def test_bilinear_sampler_with_type():
    data = mx.sym.Variable('data')
    grid = mx.sym.Variable('grid')
    sym = mx.sym.BilinearSampler(data=data, grid=grid)
    ctx_list = [{'ctx': mx.gpu(0), 'data': (1, 5, 10, 10), 'grid': (1, 2, 10, 10),
                 'type_dict': {'data': np.float64}},
                {'ctx': mx.gpu(0), 'data': (1, 5, 10, 10), 'grid': (1, 2, 10, 10),
                 'type_dict': {'data': np.float32}},
                {'ctx': mx.gpu(0), 'data': (1, 5, 10, 10), 'grid': (1, 2, 10, 10),
                 'type_dict': {'data': np.float16}},
                {'ctx': mx.cpu(0), 'data': (1, 5, 10, 10), 'grid': (1, 2, 10, 10),
                 'type_dict': {'data': np.float64}},
                {'ctx': mx.cpu(0), 'data': (1, 5, 10, 10), 'grid': (1, 2, 10, 10),
                 'type_dict': {'data': np.float32}}]
    check_consistency(sym, ctx_list)
    check_consistency(sym, ctx_list, grad_req="add")


@with_seed()
def test_grid_generator_with_type():
    data = mx.sym.Variable('data')
    sym = mx.sym.GridGenerator(data=data, transform_type='affine', target_shape=(20, 20))
    ctx_list = [{'ctx': mx.gpu(0), 'data': (3, 6), 'type_dict': {'data': np.float32}},
                {'ctx': mx.cpu(0), 'data': (3, 6), 'type_dict': {'data': np.float32}}]
    check_consistency(sym, ctx_list)
    check_consistency(sym, ctx_list, grad_req="add")
    sym = mx.sym.GridGenerator(data=data, transform_type='warp', target_shape=(20, 20))
    ctx_list = [{'ctx': mx.gpu(0), 'data': (3, 2, 20, 20), 'type_dict': {'data': np.float32}},
                {'ctx': mx.cpu(0), 'data': (3, 2, 20, 20), 'type_dict': {'data': np.float32}}]
    check_consistency(sym, ctx_list)
    check_consistency(sym, ctx_list, grad_req="add")


@with_seed()
def test_spatial_transformer_with_type():
    data = mx.sym.Variable('data')
    loc = mx.sym.Flatten(data)
    loc = mx.sym.FullyConnected(data=loc, num_hidden=10)
    loc = mx.sym.Activation(data=loc, act_type='relu')
    loc = mx.sym.FullyConnected(data=loc, num_hidden=6)
    sym = mx.sym.SpatialTransformer(data=data, loc=loc, target_shape=(10, 10),
                                    transform_type="affine", sampler_type="bilinear", cudnn_off=True)
    ctx_list = [{'ctx': mx.gpu(0), 'data': (1, 5, 10, 10), 'type_dict': {'data': np.float64}},
                {'ctx': mx.cpu(0), 'data': (1, 5, 10, 10), 'type_dict': {'data': np.float64}}]
    check_consistency(sym, ctx_list)
    check_consistency(sym, ctx_list, grad_req="add")
    sym = mx.sym.SpatialTransformer(data=data, loc=loc, target_shape=(10, 10),
                                    transform_type="affine", sampler_type="bilinear", cudnn_off=False)
    check_consistency(sym, ctx_list)
    check_consistency(sym, ctx_list, grad_req="add")


@with_seed()
def test_pooling_with_type2():
    ctx_list = [{'ctx': mx.gpu(0), 'pool_data': (10, 2, 10, 10), 'type_dict': {'pool_data': np.float64}},
                {'ctx': mx.gpu(0), 'pool_data': (10, 2, 10, 10), 'type_dict': {'pool_data': np.float32}},
                {'ctx': mx.gpu(0), 'pool_data': (10, 2, 10, 10), 'type_dict': {'pool_data': np.float16}},
                {'ctx': mx.cpu(0), 'pool_data': (10, 2, 10, 10), 'type_dict': {'pool_data': np.float64}},
                {'ctx': mx.cpu(0), 'pool_data': (10, 2, 10, 10), 'type_dict': {'pool_data': np.float32}}]

    sym = mx.sym.Pooling(name='pool', kernel=(3,3), stride=(2,2), pool_type='max')
    check_consistency(sym, ctx_list, rand_type=np.float16)

    sym = mx.sym.Pooling(name='pool', kernel=(3,3), pad=(1,1), pool_type='avg')
    check_consistency(sym, ctx_list)

    sym = mx.sym.Pooling(name='pool', kernel=(5,5), pad=(2,2), pool_type='max')
    check_consistency(sym, ctx_list, rand_type=np.float16)

    sym = mx.sym.Pooling(name='pool', kernel=(3,3), pad=(1,1), pool_type='sum')
    check_consistency(sym, ctx_list)

@unittest.skip("Flaky test https://github.com/apache/incubator-mxnet/issues/11517")
@with_seed()
def test_pooling_versions():
    def test_pooling_versions_helper(pool_op_list, data, kernel, pool_type, pad, stride, pooling_convention='valid',
                                     global_pool=False, p_value=2, count_include_pad=True, tol=None):
        ctx_list = []
        sym_list = []
        # PoolingV1 cpu
        if 'pool_v1_cpu' in pool_op_list:
            ctx_list.append({'ctx': mx.cpu(0), 'pool_data': data, 'type_dict': {'pool_data': np.float32}})
            if not global_pool:
                sym_list.append(mx.sym.Pooling_v1(kernel=kernel, pad=pad, stride=stride, pool_type=pool_type,
                                                  pooling_convention=pooling_convention, name='pool'))
            else:
                sym_list.append(mx.sym.Pooling_v1(kernel=kernel, pool_type=pool_type, global_pool=True, name='pool'))
        # PoolingV1 gpu
        if 'pool_v1_gpu' in pool_op_list:
            ctx_list.append({'ctx': mx.gpu(0), 'pool_data': data, 'type_dict': {'pool_data': np.float32}})
            if not global_pool:
                sym_list.append(mx.sym.Pooling_v1(kernel=kernel, pad=pad, stride=stride, pool_type=pool_type,
                                                  pooling_convention=pooling_convention, name='pool'))
            else:
                sym_list.append(mx.sym.Pooling_v1(kernel=kernel, pool_type=pool_type, global_pool=True, name='pool'))
        # Pooling cpu
        if 'pool_cpu' in pool_op_list:
            ctx_list.append({'ctx': mx.cpu(0), 'pool_data': data, 'type_dict': {'pool_data': np.float32}})
            if not global_pool:
                sym_list.append(mx.sym.Pooling(kernel=kernel, pad=pad, stride=stride, pool_type=pool_type,
                                               pooling_convention=pooling_convention, name='pool',
                                               p_value=p_value, count_include_pad=count_include_pad))
            else:
                sym_list.append(mx.sym.Pooling(kernel=kernel, pool_type=pool_type, global_pool=True, name='pool',
                                               p_value=p_value, count_include_pad=count_include_pad))
        # Pooling gpu
        if 'pool_gpu' in pool_op_list:
            ctx_list.append({'ctx': mx.gpu(0), 'pool_data': data, 'type_dict': {'pool_data': np.float32}})
            if not global_pool:
                sym_list.append(mx.sym.Pooling(kernel=kernel, pad=pad, stride=stride, pool_type=pool_type,
                                               pooling_convention=pooling_convention, cudnn_off=True, name='pool',
                                               p_value=p_value, count_include_pad=count_include_pad))
            else:
                sym_list.append(mx.sym.Pooling(kernel=kernel, pool_type=pool_type, global_pool=True, cudnn_off=True,
                                               name='pool', p_value=p_value, count_include_pad=count_include_pad))
        # CuDNNPooling
        if 'pool_cudnn' in pool_op_list:
            ctx_list.append({'ctx': mx.gpu(0), 'pool_data': data, 'type_dict': {'pool_data': np.float32}})
            if not global_pool:
                sym_list.append(mx.sym.Pooling(kernel=kernel, pad=pad, stride=stride, pool_type=pool_type,
                                               pooling_convention=pooling_convention, p_value=p_value, cudnn_off=False,
                                               name='pool', count_include_pad=count_include_pad))
            else:
                sym_list.append(mx.sym.Pooling(kernel=kernel, pool_type=pool_type, global_pool=True, p_value=p_value,
                                               cudnn_off=False, name='pool', count_include_pad=count_include_pad))
        check_consistency(sym_list, ctx_list, equal_nan=(not count_include_pad), tol=tol)

    def test_1d_pooling(pool_type, p_value=2, count_include_pad=True):
        data = (2, 3, 20)
        kernel = (4,)
        pad = (0,)
        stride = (1,)
        test_pooling_versions_helper(pool_op_list=['pool_cpu', 'pool_gpu'],
                                     data=data, kernel=kernel, pad=pad, stride=stride, pool_type=pool_type,
                                     pooling_convention='valid', global_pool=False, p_value=p_value,
                                     count_include_pad=count_include_pad)

        pad = (2,)
        stride = (2,)
        test_pooling_versions_helper(pool_op_list=['pool_cpu', 'pool_gpu'],
                                     data=data, kernel=kernel, pad=pad, stride=stride, pool_type=pool_type,
                                     pooling_convention='valid', global_pool=False, p_value=p_value,
                                     count_include_pad=count_include_pad)

        pad = (0,)
        stride = (1,)
        test_pooling_versions_helper(pool_op_list=['pool_cpu', 'pool_gpu'],
                                     data=data, kernel=kernel, pad=pad, stride=stride, pool_type=pool_type,
                                     pooling_convention='full', global_pool=False, p_value=p_value,
                                     count_include_pad=count_include_pad)

        pad = (2,)
        stride = (2,)
        test_pooling_versions_helper(pool_op_list=['pool_cpu', 'pool_gpu'],
                                     data=data, kernel=kernel, pad=pad, stride=stride, pool_type=pool_type,
                                     pooling_convention='full', global_pool=False, p_value=p_value,
                                     count_include_pad=count_include_pad)

        test_pooling_versions_helper(pool_op_list=['pool_cpu', 'pool_gpu'],
                                     data=data, kernel=kernel, pad=pad, stride=stride, pool_type=pool_type,
                                     global_pool=True, p_value=p_value, count_include_pad=count_include_pad)

    def test_2d_pooling(pool_type, p_value=2, count_include_pad=True):
        data = (2, 3, 20, 20)
        kernel = (4, 5)
        pad = (0, 0)
        stride = (1, 1)
        if pool_type == 'lp':
            test_pooling_versions_helper(pool_op_list=['pool_cpu', 'pool_gpu'],
                                         data=data, kernel=kernel, pad=pad, stride=stride, pool_type=pool_type,
                                         pooling_convention='valid', global_pool=False, p_value=p_value)
        else:
            test_pooling_versions_helper(pool_op_list=['pool_v1_cpu', 'pool_v1_gpu', 'pool_cpu', 'pool_gpu', 'pool_cudnn'],
                                         data=data, kernel=kernel, pad=pad, stride=stride, pool_type=pool_type,
                                         pooling_convention='valid', global_pool=False, count_include_pad=count_include_pad)

        # pool_v1 has bugs when pad is not 0, do not test PoolingV1 here
        pad = (2, 3)
        stride = (2, 3)
        test_pooling_versions_helper(pool_op_list=['pool_cpu', 'pool_gpu', 'pool_cudnn'],
                                     data=data, kernel=kernel, pad=pad, stride=stride, pool_type=pool_type,
                                     pooling_convention='valid', global_pool=False, p_value=p_value,
                                     count_include_pad=count_include_pad)

        pad = (0, 0)
        stride = (1, 1)
        if pool_type == 'lp':
            test_pooling_versions_helper(pool_op_list=['pool_cpu', 'pool_gpu', 'pool_cudnn'],
                                         data=data, kernel=kernel, pad=pad, stride=stride, pool_type=pool_type,
                                         pooling_convention='full', global_pool=False, p_value=p_value)
        else:
            if count_include_pad:
                test_pooling_versions_helper(pool_op_list=['pool_v1_cpu', 'pool_v1_gpu', 'pool_cpu', 'pool_gpu', 'pool_cudnn'],
                                             data=data, kernel=kernel, pad=pad, stride=stride, pool_type=pool_type,
                                             pooling_convention='full', global_pool=False,
                                             count_include_pad=count_include_pad)
            else:
                test_pooling_versions_helper(pool_op_list=['pool_cpu', 'pool_gpu', 'pool_cudnn'],
                                             data=data, kernel=kernel, pad=pad, stride=stride, pool_type=pool_type,
                                             pooling_convention='full', global_pool=False,
                                             count_include_pad=count_include_pad)

        # pool_v1 has bugs when pad is not 0, do not test PoolingV1 here
        pad = (2, 3)
        stride = (2, 3)
        test_pooling_versions_helper(pool_op_list=['pool_cpu', 'pool_gpu', 'pool_cudnn'],
                                     data=data, kernel=kernel, pad=pad, stride=stride, pool_type=pool_type,
                                     pooling_convention='full', global_pool=False, p_value=p_value,
                                     count_include_pad=count_include_pad)

        if pool_type == 'lp':
            test_pooling_versions_helper(pool_op_list=['pool_cpu', 'pool_gpu', 'pool_cudnn'],
                                         data=data, kernel=kernel, pad=pad, stride=stride, pool_type=pool_type,
                                         global_pool=True, p_value=p_value)
        else:
            test_pooling_versions_helper(pool_op_list=['pool_v1_cpu', 'pool_v1_gpu', 'pool_cpu', 'pool_gpu', 'pool_cudnn'],
                                         data=data, kernel=kernel, pad=pad, stride=stride, pool_type=pool_type,
                                         global_pool=True, count_include_pad=count_include_pad)

    def test_3d_pooling(pool_type, p_value=2, count_include_pad=True):
        data = (2, 3, 20, 20, 20)
        kernel = (4, 5, 3)
        pad = (0, 0, 0)
        stride = (1, 1, 1)
        test_pooling_versions_helper(pool_op_list=['pool_cpu', 'pool_gpu', 'pool_cudnn'],
                                     data=data, kernel=kernel, pad=pad, stride=stride, pool_type=pool_type,
                                     pooling_convention='valid', global_pool=False, p_value=p_value,
                                     count_include_pad=count_include_pad)

        pad = (2, 3, 3)
        stride = (2, 3, 1)
        test_pooling_versions_helper(pool_op_list=['pool_cpu', 'pool_gpu', 'pool_cudnn'],
                                     data=data, kernel=kernel, pad=pad, stride=stride, pool_type=pool_type,
                                     pooling_convention='valid', global_pool=False, p_value=p_value,
                                     count_include_pad=count_include_pad)

        pad = (0, 0, 0)
        stride = (1, 1, 1)
        test_pooling_versions_helper(pool_op_list=['pool_cpu', 'pool_gpu', 'pool_cudnn'],
                                     data=data, kernel=kernel, pad=pad, stride=stride, pool_type=pool_type,
                                     pooling_convention='full', global_pool=False, p_value=p_value,
                                     count_include_pad=count_include_pad)

        pad = (2, 3, 3)
        stride = (2, 3, 1)
        test_pooling_versions_helper(pool_op_list=['pool_cpu', 'pool_gpu', 'pool_cudnn'],
                                     data=data, kernel=kernel, pad=pad, stride=stride, pool_type=pool_type,
                                     pooling_convention='full', global_pool=False, p_value=p_value,
                                     count_include_pad=count_include_pad)

        test_pooling_versions_helper(pool_op_list=['pool_cpu', 'pool_gpu', 'pool_cudnn'],
                                     data=data, kernel=kernel, pad=pad, stride=stride, pool_type=pool_type,
                                     global_pool=True, p_value=p_value, count_include_pad=count_include_pad)

    test_1d_pooling('max')
    test_1d_pooling('avg', count_include_pad=True)
    test_1d_pooling('avg', count_include_pad=False)
    test_1d_pooling('sum')
    test_1d_pooling('lp', p_value=1)
    test_1d_pooling('lp', p_value=2)
    test_1d_pooling('lp', p_value=3)

    test_2d_pooling('max')
    test_2d_pooling('avg', count_include_pad=True)
    test_2d_pooling('avg', count_include_pad=False)
    test_2d_pooling('sum')
    test_2d_pooling('lp', p_value=1)
    test_2d_pooling('lp', p_value=2)
    test_2d_pooling('lp', p_value=3)

    test_3d_pooling('max')
    test_3d_pooling('avg', count_include_pad=True)
    test_3d_pooling('avg', count_include_pad=False)
    test_3d_pooling('sum')
    test_3d_pooling('lp', p_value=1)
    test_3d_pooling('lp', p_value=2)
    test_3d_pooling('lp', p_value=3)


@with_seed()
def test_pooling_full_2d():
    def test_pooling_full_2d_type(pool_type):
        data = (2, 2, 10, 10)
        kernel = (4, 5)
        pad = (1, 2)
        stride = (3, 4)

        convention = 'full'
        ctx_list = []
        sym_list = []

        # o_h = ceil((10 + 1 + 1 - 4) / 3) + 1 = 4
        # o_w = ceil((10 + 2 + 2 - 5) / 4) + 1 = 4
        ctx_list.append({'ctx': mx.cpu(0), 'pool_data': data, 'type_dict': {'pool_data': np.float32}})
        sym_list.append(mx.sym.Pooling(kernel=kernel, pad=pad, stride=stride, pool_type=pool_type,
                                       pooling_convention=convention, global_pool=False, name='pool'))

        ctx_list.append({'ctx': mx.gpu(0), 'pool_data': data, 'type_dict': {'pool_data': np.float32}})
        sym_list.append(mx.sym.Pooling(kernel=kernel, pad=pad, stride=stride, pool_type=pool_type,
                                       pooling_convention=convention, global_pool=False, name='pool'))

        check_consistency(sym_list, ctx_list)

    test_pooling_full_2d_type('max')
    test_pooling_full_2d_type('avg')
    test_pooling_full_2d_type('sum')


@with_seed()
def test_global_pooling():
    def test_1d_pooling(pool_type, p_value=2):
        data = (2, 3, 20)
        kernel = (4,)
        pad = (2,)
        stride = (2,)

        ctx_list = []
        sym_list = []

        pooling_convention = 'valid'

        ctx_list.append({'ctx': mx.cpu(0), 'pool_data': data, 'type_dict': {'pool_data': np.float32}})
        sym_list.append(mx.sym.Pooling(kernel=kernel, pad=pad, stride=stride, pool_type=pool_type,
                                       pooling_convention=pooling_convention, global_pool=True, name='pool', p_value=p_value))

        ctx_list.append({'ctx': mx.cpu(0), 'pool_data': data, 'type_dict': {'pool_data': np.float32}})
        sym_list.append(mx.sym.Pooling(kernel=kernel, pool_type=pool_type,
                                       pooling_convention=pooling_convention, global_pool=True, name='pool', p_value=p_value))

        ctx_list.append({'ctx': mx.cpu(0), 'pool_data': data, 'type_dict': {'pool_data': np.float32}})
        sym_list.append(mx.sym.Pooling(pool_type=pool_type,
                                       pooling_convention=pooling_convention, global_pool=True, name='pool', p_value=p_value))

        ctx_list.append({'ctx': mx.gpu(0), 'pool_data': data, 'type_dict': {'pool_data': np.float32}})
        sym_list.append(mx.sym.Pooling(kernel=kernel, pad=pad, stride=stride, pool_type=pool_type,
                                       pooling_convention=pooling_convention, global_pool=True, p_value=p_value, cudnn_off=False, name='pool'))

        ctx_list.append({'ctx': mx.gpu(0), 'pool_data': data, 'type_dict': {'pool_data': np.float32}})
        sym_list.append(mx.sym.Pooling(kernel=kernel, pool_type=pool_type,
                                       pooling_convention=pooling_convention, global_pool=True, p_value=p_value, cudnn_off=False, name='pool'))

        ctx_list.append({'ctx': mx.gpu(0), 'pool_data': data, 'type_dict': {'pool_data': np.float32}})
        sym_list.append(mx.sym.Pooling(pool_type=pool_type,
                                       pooling_convention=pooling_convention, global_pool=True, p_value=p_value, cudnn_off=False, name='pool'))

        ctx_list.append({'ctx': mx.gpu(0), 'pool_data': data, 'type_dict': {'pool_data': np.float32}})
        sym_list.append(mx.sym.Pooling(kernel=kernel, pad=pad, stride=stride, pool_type=pool_type,
                                       pooling_convention=pooling_convention, global_pool=True, p_value=p_value, cudnn_off=True, name='pool'))

        ctx_list.append({'ctx': mx.gpu(0), 'pool_data': data, 'type_dict': {'pool_data': np.float32}})
        sym_list.append(mx.sym.Pooling(kernel=kernel, pool_type=pool_type,
                                       pooling_convention=pooling_convention, global_pool=True, p_value=p_value, cudnn_off=True, name='pool'))

        ctx_list.append({'ctx': mx.gpu(0), 'pool_data': data, 'type_dict': {'pool_data': np.float32}})
        sym_list.append(mx.sym.Pooling(pool_type=pool_type,
                                       pooling_convention=pooling_convention, global_pool=True, p_value=p_value, cudnn_off=True, name='pool'))

        check_consistency(sym_list, ctx_list)

    def test_2d_pooling(pool_type, p_value=2):
        data = (2, 3, 20, 20)
        kernel = (4, 4)
        pad = (2, 2)
        stride = (2, 2)

        ctx_list = []
        sym_list = []

        pooling_convention = 'valid'

        if pool_type != 'lp':
            ctx_list.append({'ctx': mx.cpu(0), 'pool_data': data, 'type_dict': {'pool_data': np.float32}})
            sym_list.append(mx.sym.Pooling_v1(kernel=kernel, pad=pad, stride=stride, pool_type=pool_type,
                                              pooling_convention=pooling_convention, global_pool=True, name='pool'))

            ctx_list.append({'ctx': mx.cpu(0), 'pool_data': data, 'type_dict': {'pool_data': np.float32}})
            sym_list.append(mx.sym.Pooling_v1(kernel=kernel, pool_type=pool_type,
                                              pooling_convention=pooling_convention, global_pool=True, name='pool'))

            ctx_list.append({'ctx': mx.cpu(0), 'pool_data': data, 'type_dict': {'pool_data': np.float32}})
            sym_list.append(mx.sym.Pooling_v1(pool_type=pool_type,
                                              pooling_convention=pooling_convention, global_pool=True, name='pool'))

        ctx_list.append({'ctx': mx.cpu(0), 'pool_data': data, 'type_dict': {'pool_data': np.float32}})
        sym_list.append(mx.sym.Pooling(kernel=kernel, pad=pad, stride=stride, pool_type=pool_type,
                                       pooling_convention=pooling_convention, global_pool=True, p_value=p_value, name='pool'))

        ctx_list.append({'ctx': mx.cpu(0), 'pool_data': data, 'type_dict': {'pool_data': np.float32}})
        sym_list.append(mx.sym.Pooling(kernel=kernel, pool_type=pool_type,
                                       pooling_convention=pooling_convention, global_pool=True, p_value=p_value, name='pool'))

        ctx_list.append({'ctx': mx.cpu(0), 'pool_data': data, 'type_dict': {'pool_data': np.float32}})
        sym_list.append(mx.sym.Pooling(pool_type=pool_type,
                                       pooling_convention=pooling_convention, global_pool=True, p_value=p_value, name='pool'))

        ctx_list.append({'ctx': mx.gpu(0), 'pool_data': data, 'type_dict': {'pool_data': np.float32}})
        sym_list.append(mx.sym.Pooling(kernel=kernel, pad=pad, stride=stride, pool_type=pool_type,
                                       pooling_convention=pooling_convention, global_pool=True, p_value=p_value, cudnn_off=False, name='pool'))

        ctx_list.append({'ctx': mx.gpu(0), 'pool_data': data, 'type_dict': {'pool_data': np.float32}})
        sym_list.append(mx.sym.Pooling(kernel=kernel, pool_type=pool_type,
                                       pooling_convention=pooling_convention, global_pool=True, p_value=p_value, cudnn_off=False, name='pool'))

        ctx_list.append({'ctx': mx.gpu(0), 'pool_data': data, 'type_dict': {'pool_data': np.float32}})
        sym_list.append(mx.sym.Pooling(pool_type=pool_type,
                                       pooling_convention=pooling_convention, global_pool=True, p_value=p_value, cudnn_off=False, name='pool'))

        ctx_list.append({'ctx': mx.gpu(0), 'pool_data': data, 'type_dict': {'pool_data': np.float32}})
        sym_list.append(mx.sym.Pooling(kernel=kernel, pad=pad, stride=stride, pool_type=pool_type,
                                       pooling_convention=pooling_convention, global_pool=True, p_value=p_value, cudnn_off=True, name='pool'))

        ctx_list.append({'ctx': mx.gpu(0), 'pool_data': data, 'type_dict': {'pool_data': np.float32}})
        sym_list.append(mx.sym.Pooling(kernel=kernel, pool_type=pool_type,
                                       pooling_convention=pooling_convention, global_pool=True, p_value=p_value, cudnn_off=True, name='pool'))

        ctx_list.append({'ctx': mx.gpu(0), 'pool_data': data, 'type_dict': {'pool_data': np.float32}})
        sym_list.append(mx.sym.Pooling(pool_type=pool_type,
                                       pooling_convention=pooling_convention, global_pool=True, p_value=p_value, cudnn_off=True, name='pool'))


        check_consistency(sym_list, ctx_list)

    test_1d_pooling('max')
    test_1d_pooling('avg')
    test_1d_pooling('sum')
    test_1d_pooling('lp', p_value=1)
    test_1d_pooling('lp', p_value=2)
    test_1d_pooling('lp', p_value=3)

    test_2d_pooling('max')
    test_2d_pooling('avg')
    test_2d_pooling('sum')
    test_2d_pooling('lp', p_value=1)
    test_2d_pooling('lp', p_value=2)
    test_2d_pooling('lp', p_value=3)


@with_seed()
def test_upsampling_with_type():
    sym = mx.sym.UpSampling(scale=2, num_filter=2, name='up', sample_type='nearest', num_args=1)
    ctx_list = [{'ctx': mx.gpu(0), 'up_arg0': (2, 2, 2, 10), 'type_dict': {'up_arg0': np.float64}},
                {'ctx': mx.gpu(0), 'up_arg0': (2, 2, 2, 10), 'type_dict': {'up_arg0': np.float32}},
                {'ctx': mx.gpu(0), 'up_arg0': (2, 2, 2, 10), 'type_dict': {'up_arg0': np.float16}},
                {'ctx': mx.cpu(0), 'up_arg0': (2, 2, 2, 10), 'type_dict': {'up_arg0': np.float64}},
                {'ctx': mx.cpu(0), 'up_arg0': (2, 2, 2, 10), 'type_dict': {'up_arg0': np.float32}}]
    check_consistency(sym, ctx_list)


@with_seed()
def test_upsampling_bilinear_with_type():
    sym = mx.sym.UpSampling(scale=2, num_filter=2, name='up', sample_type='bilinear', num_args=1)
    ctx_list = [{'ctx': mx.gpu(0), 'up_data': (2, 2, 2, 10), 'type_dict': {'up_data': np.float64}},
                {'ctx': mx.gpu(0), 'up_data': (2, 2, 2, 10), 'type_dict': {'up_data': np.float32}},
                {'ctx': mx.gpu(0), 'up_data': (2, 2, 2, 10), 'type_dict': {'up_data': np.float16}},
                {'ctx': mx.cpu(0), 'up_data': (2, 2, 2, 10), 'type_dict': {'up_data': np.float64}},
                {'ctx': mx.cpu(0), 'up_data': (2, 2, 2, 10), 'type_dict': {'up_data': np.float32}}]
    check_consistency(sym, ctx_list)


@with_seed()
def test_concat_with_type():
    sym = mx.sym.Concat(name='concat', num_args=2)
    ctx_list = [{'ctx': mx.gpu(0), 'concat_arg1': (2, 10), 'concat_arg0': (2, 10),
                 'type_dict': {'concat_arg0': np.float64, 'concat_arg1': np.float64}},
                {'ctx': mx.gpu(0), 'concat_arg1': (2, 10), 'concat_arg0': (2, 10),
                 'type_dict': {'concat_arg0': np.float32, 'concat_arg1': np.float32}},
                {'ctx': mx.gpu(0), 'concat_arg1': (2, 10), 'concat_arg0': (2, 10),
                 'type_dict': {'concat_arg0': np.float16, 'concat_arg1': np.float16}},
                {'ctx': mx.cpu(0), 'concat_arg1': (2, 10), 'concat_arg0': (2, 10),
                 'type_dict': {'concat_arg0': np.float64, 'concat_arg1': np.float64}},
                {'ctx': mx.cpu(0), 'concat_arg1': (2, 10), 'concat_arg0': (2, 10),
                 'type_dict': {'concat_arg0': np.float32, 'concat_arg1': np.float32}}]
    check_consistency(sym, ctx_list)


@with_seed()
def test_elementwisesum_with_type():
    dev_types = [[mx.gpu(0), [np.float64, np.float32, np.float16]],
                 [mx.cpu(0), [np.float64, np.float32]] ]
    for num_args in range(1, 6):
        ews_arg_shape = {}
        for i in range(num_args):
            ews_arg_shape['ews_arg'+str(i)] = (2, 10)
        sym = mx.sym.ElementWiseSum(name='ews', num_args=num_args)
        ctx_list = []
        for dev, types in dev_types:
            for dtype in types:
                ews_arg_dtype = {'type_dict':{}}
                for i in range(num_args):
                    ews_arg_dtype['type_dict']['ews_arg'+str(i)] = dtype
                ctx_elem = {'ctx': dev}
                ctx_elem.update(ews_arg_shape)
                ctx_elem.update(ews_arg_dtype)
                ctx_list.append(ctx_elem)
    check_consistency(sym, ctx_list)


@with_seed()
def test_reshape_with_type():
    sym = mx.sym.Reshape(name='reshape', shape=(-1,1,1,0))
    ctx_list = [{'ctx': mx.gpu(0), 'reshape_data': (2, 2, 2, 10), 'type_dict': {'reshape_data': np.float64}},
                {'ctx': mx.gpu(0), 'reshape_data': (2, 2, 2, 10), 'type_dict': {'reshape_data': np.float32}},
                {'ctx': mx.gpu(0), 'reshape_data': (2, 2, 2, 10), 'type_dict': {'reshape_data': np.float16}},
                {'ctx': mx.cpu(0), 'reshape_data': (2, 2, 2, 10), 'type_dict': {'reshape_data': np.float64}},
                {'ctx': mx.cpu(0), 'reshape_data': (2, 2, 2, 10), 'type_dict': {'reshape_data': np.float32}}]
    check_consistency(sym, ctx_list)


@with_seed()
def test_blockgrad_with_type():
    sym = mx.sym.BlockGrad(name='bg')
    ctx_list = [{'ctx': mx.gpu(0), 'bg_data': (2, 2, 2, 10), 'type_dict': {'bg_data': np.float64}},
                {'ctx': mx.gpu(0), 'bg_data': (2, 2, 2, 10), 'type_dict': {'bg_data': np.float32}},
                {'ctx': mx.gpu(0), 'bg_data': (2, 2, 2, 10), 'type_dict': {'bg_data': np.float16}},
                {'ctx': mx.cpu(0), 'bg_data': (2, 2, 2, 10), 'type_dict': {'bg_data': np.float64}},
                {'ctx': mx.cpu(0), 'bg_data': (2, 2, 2, 10), 'type_dict': {'bg_data': np.float32}}]
    check_consistency(sym, ctx_list)


@with_seed()
def test_swapaxis_with_type():
    sym = mx.sym.SwapAxis(name='swap', dim1=1)
    ctx_list = [{'ctx': mx.gpu(0), 'swap_data': (2, 2, 2, 10), 'type_dict': {'swap_data': np.float64}},
                {'ctx': mx.gpu(0), 'swap_data': (2, 2, 2, 10), 'type_dict': {'swap_data': np.float32}},
                {'ctx': mx.gpu(0), 'swap_data': (2, 2, 2, 10), 'type_dict': {'swap_data': np.float16}},
                {'ctx': mx.cpu(0), 'swap_data': (2, 2, 2, 10), 'type_dict': {'swap_data': np.float64}},
                {'ctx': mx.cpu(0), 'swap_data': (2, 2, 2, 10), 'type_dict': {'swap_data': np.float32}}]
    check_consistency(sym, ctx_list)


@with_seed()
def test_fullyconnected_with_type():
    sym = mx.sym.FullyConnected(num_hidden=3, name='inner')
    ctx_list = [{'ctx': mx.gpu(0), 'inner_data': (2, 10), 'type_dict': {'inner_data': np.float64}},
                {'ctx': mx.gpu(0), 'inner_data': (2, 10), 'type_dict': {'inner_data': np.float32}},
                {'ctx': mx.gpu(0), 'inner_data': (2, 10), 'type_dict': {'inner_data': np.float16}},
                {'ctx': mx.cpu(0), 'inner_data': (2, 10), 'type_dict': {'inner_data': np.float64}},
                {'ctx': mx.cpu(0), 'inner_data': (2, 10), 'type_dict': {'inner_data': np.float32}}]
    check_consistency(sym, ctx_list)
    # Sizes are divisible by 8 to test TensorCore on Volta GPU.
    sym = mx.sym.FullyConnected(num_hidden=8, name='inner')
    ctx_list = [{'ctx': mx.gpu(0), 'inner_data': (16, 24), 'type_dict': {'inner_data': np.float16}},
                {'ctx': mx.cpu(0), 'inner_data': (16, 24), 'type_dict': {'inner_data': np.float32}}]
    check_consistency(sym, ctx_list)


@with_seed()
def test_activation_with_type():
    act_types = ['relu', 'sigmoid', 'tanh', 'softrelu', 'softsign']
    shape = (2, 2, 10, 10)
    for act_type in act_types:
        sym = mx.sym.Activation(name='act', act_type=act_type)
        ctx_list = [{'ctx': mx.gpu(0), 'act_data': shape, 'type_dict': {'act_data': np.float64}},
                    {'ctx': mx.gpu(0), 'act_data': shape, 'type_dict': {'act_data': np.float32}},
                    {'ctx': mx.gpu(0), 'act_data': shape, 'type_dict': {'act_data': np.float16}},
                    {'ctx': mx.cpu(0), 'act_data': shape, 'type_dict': {'act_data': np.float64}},
                    {'ctx': mx.cpu(0), 'act_data': shape, 'type_dict': {'act_data': np.float32}},
                    {'ctx': mx.cpu(0), 'act_data': shape, 'type_dict': {'act_data': np.float16}}]
        check_consistency(sym, ctx_list)


@with_seed()
def test_lrn():
    sym = mx.sym.LRN(alpha=0.0001, beta=0.75, knorm=2, nsize=5, name='lrn')
    ctx_list = [{'ctx': mx.gpu(0), 'lrn_data': (2, 6, 10, 10), 'type_dict': {'lrn_data': np.float32}},
                {'ctx': mx.cpu(0), 'lrn_data': (2, 6, 10, 10), 'type_dict': {'lrn_data': np.float32}}]
    check_consistency(sym, ctx_list)


@with_seed()
def test_embedding_with_type():
    def test_embedding_helper(data_types, weight_types, low_pad, high_pad):
        NVD = [[20, 10, 20], [200, 10, 300]]
        for N, V, D in NVD:
            sym = mx.sym.Embedding(name='embedding', input_dim=V, output_dim=D)
            ctx_list = []
            for data_type in data_types:
                for weight_type in weight_types:
                    ctx_list.append({'ctx': mx.gpu(0), 'embedding_data': (N,),
                        'type_dict': {'embedding_data': data_type, 'embedding_weight': weight_type}})
                    ctx_list.append({'ctx': mx.cpu(0), 'embedding_data': (N,),
                        'type_dict': {'embedding_data': data_type, 'embedding_weight': weight_type}})
            arg_params = {'embedding_data': np.random.randint(low=-low_pad, high=V+high_pad, size=(N,))}
            check_consistency(sym, ctx_list, grad_req={'embedding_data': 'null','embedding_weight': 'write'},
                              arg_params=arg_params)

    data_types = [np.float16, np.float32, np.float64, np.int32]
    weight_types = [np.float16, np.float32, np.float64]
    test_embedding_helper(data_types, weight_types, 5, 5)
    data_types = [np.uint8]
    weight_types = [np.float16, np.float32, np.float64]
    test_embedding_helper(data_types, weight_types, 0, 5)


@with_seed()
def test_svmoutput_with_type():
    sym = mx.sym.SVMOutput(name='svmoutput', use_linear=True)
    ctx_list = [{'ctx': mx.gpu(0), 'svmoutput_data': (20, 10), 'type_dict': {'svmoutput_data': np.float64}},
                {'ctx': mx.gpu(0), 'svmoutput_data': (20, 10), 'type_dict': {'svmoutput_data': np.float32}},
                {'ctx': mx.gpu(0), 'svmoutput_data': (20, 10), 'type_dict': {'svmoutput_data': np.float16}},
                {'ctx': mx.cpu(0), 'svmoutput_data': (20, 10), 'type_dict': {'svmoutput_data': np.float64}},
                {'ctx': mx.cpu(0), 'svmoutput_data': (20, 10), 'type_dict': {'svmoutput_data': np.float32}},
                {'ctx': mx.cpu(0), 'svmoutput_data': (20, 10), 'type_dict': {'svmoutput_data': np.float16}}]
    check_consistency(sym, ctx_list, use_uniform=True)


@with_seed()
def test_take_with_type():
    sym = mx.sym.take(name='take')
    for data_ndim in range(2, 5):
        for idx_ndim in range(1, 4):
            data_shape = ()
            for _ in range(data_ndim):
                data_shape += (np.random.randint(low=3, high=6), )
            idx_shape = ()
            for _ in range(idx_ndim):
                idx_shape += (np.random.randint(low=3, high=5), )
            ctx_list = [{'ctx': mx.gpu(0), 'take_indices': idx_shape,
                         'take_a': data_shape,
                         'type_dict': {'take_indices': np.float64,
                                       'take_a': np.float64}},
                        {'ctx': mx.gpu(0), 'take_indices': idx_shape,
                         'take_a': data_shape,
                         'type_dict': {'take_indices': np.float32,
                                       'take_a': np.float32}},
                        {'ctx': mx.gpu(0), 'take_indices': idx_shape,
                         'take_a': data_shape,
                         'type_dict': {'take_indices': np.float16,
                                       'take_a': np.float16}},
                        {'ctx': mx.cpu(0), 'take_indices': idx_shape,
                         'take_a': data_shape,
                         'type_dict': {'take_indices': np.float64,
                                       'take_a': np.float64}},
                        {'ctx': mx.cpu(0), 'take_indices': idx_shape,
                         'take_a': data_shape,
                         'type_dict': {'take_indices': np.float32,
                                       'take_a': np.float32}},
                        {'ctx': mx.cpu(0), 'take_indices': idx_shape,
                         'take_a': data_shape,
                         'type_dict': {'take_indices': np.float16,
                                       'take_a': np.float16}}]
            arg_params = {'take_indices': np.random.randint(low=0,
                                                            high=data_shape[0],
                                                            size=idx_shape),
                          'take_a': np.random.normal(size=data_shape)}
            check_consistency(sym, ctx_list,
                              grad_req={'take_indices': 'null',
                                        'take_a': 'write'},
                              arg_params=arg_params)


def check_rnn_consistency(cell1, cell2):
    dshape = (32, 5, 200)
    data = mx.sym.Variable('data')

    sym1, _ = cell1.unroll(5, data, merge_outputs=True)
    mod1 = mx.mod.Module(sym1, label_names=None, context=mx.gpu(0))
    mod1.bind(data_shapes=[('data', dshape)], label_shapes=None)

    sym2, _ = cell2.unroll(5, data, merge_outputs=True)
    mod2 = mx.mod.Module(sym2, label_names=None, context=mx.gpu(0))
    mod2.bind(data_shapes=[('data', dshape)], label_shapes=None)

    mod1.init_params()
    args, auxs = mod1.get_params()
    args = cell1.unpack_weights(args)
    args = cell2.pack_weights(args)
    mod2.set_params(args, auxs)

    batch=mx.io.DataBatch(data=[mx.random.uniform(shape=dshape)], label=[])
    mod1.forward(batch, is_train=False)
    mod2.forward(batch, is_train=False)

    assert_allclose(mod1.get_outputs()[0].asnumpy(), mod2.get_outputs()[0].asnumpy(), rtol=1e-2, atol=1e-4)

@with_seed()
@assert_raises_cudnn_not_satisfied(min_version='5.1.10')
def test_rnn():
    fused = mx.rnn.FusedRNNCell(100, num_layers=2, mode='rnn_relu', prefix='')

    stack = mx.rnn.SequentialRNNCell()
    stack.add(mx.rnn.RNNCell(100, activation='relu', prefix='l0_'))
    stack.add(mx.rnn.RNNCell(100, activation='relu', prefix='l1_'))

    check_rnn_consistency(fused, stack)
    check_rnn_consistency(stack, fused)

@with_seed()
@assert_raises_cudnn_not_satisfied(min_version='5.1.10')
def test_lstm_forget_bias():
    forget_bias = 2.0
    fused = mx.rnn.FusedRNNCell(10, forget_bias=forget_bias, num_layers=2, mode='lstm', prefix='')

    dshape = (32, 1, 20)
    data = mx.sym.Variable('data')

    sym, _ = fused.unroll(1, data, merge_outputs=True)
    mod = mx.mod.Module(sym, label_names=None, context=mx.gpu(0))
    mod.bind(data_shapes=[('data', dshape)], label_shapes=None)

    mod.init_params()

    args, auxs = mod.get_params()
    args = fused.unpack_weights(args)

    bias_name = next(x for x in args if x.endswith('f_bias'))
    expected_bias = forget_bias * np.ones(10, )
    assert_allclose(args[bias_name].asnumpy(), expected_bias)

@with_seed()
@assert_raises_cudnn_not_satisfied(min_version='5.1.10')
def test_gru():
    fused = mx.rnn.FusedRNNCell(100, num_layers=2, mode='gru', prefix='')

    stack = mx.rnn.SequentialRNNCell()
    stack.add(mx.rnn.GRUCell(100, prefix='l0_'))
    stack.add(mx.rnn.GRUCell(100, prefix='l1_'))

    check_rnn_consistency(fused, stack)
    check_rnn_consistency(stack, fused)

@with_seed()
@assert_raises_cudnn_not_satisfied(min_version='5.1.10')
def test_bidirectional():
    fused = mx.rnn.FusedRNNCell(100, num_layers=2, mode='gru', prefix='',
            bidirectional=True)

    stack = mx.rnn.SequentialRNNCell()
    stack.add(mx.rnn.BidirectionalCell(
                mx.rnn.GRUCell(100, prefix='l0_'),
                mx.rnn.GRUCell(100, prefix='r0_'),
                output_prefix='bi_gru_0_'))
    stack.add(mx.rnn.BidirectionalCell(
                mx.rnn.GRUCell(100, prefix='l1_'),
                mx.rnn.GRUCell(100, prefix='r1_'),
                output_prefix='bi_gru_1_'))

    check_rnn_consistency(fused, stack)
    check_rnn_consistency(stack, fused)

@with_seed()
@assert_raises_cudnn_not_satisfied(min_version='5.1.10')
def test_unfuse():
    for mode in ['rnn_tanh', 'rnn_relu', 'lstm', 'gru']:
        fused = mx.rnn.FusedRNNCell(
            100, num_layers=2, mode=mode,
            prefix='test_%s'%mode,
            bidirectional=True,
            dropout=0.5)

        stack = fused.unfuse()

        check_rnn_consistency(fused, stack)
        check_rnn_consistency(stack, fused)


@with_seed()
def test_psroipooling_with_type():
    arg_params = {
        'psroipool_rois': np.array([[0, 10, 22, 161, 173], [0, 20, 15, 154, 160]])}

    # plain psroipooling
    sym = mx.sym.contrib.PSROIPooling(spatial_scale=0.0625, output_dim=2, pooled_size=3, name='psroipool')
    ctx_list = [{'ctx': mx.gpu(0),
                 'psroipool_data': (1, 18, 14, 14),
                 'psroipool_rois': (2, 5),
                 'type_dict': {'psroipool_data': np.float64, 'psroipool_rois': np.float64}},
                {'ctx': mx.gpu(0),
                 'psroipool_data': (1, 18, 14, 14),
                 'psroipool_rois': (2, 5),
                 'type_dict': {'psroipool_data': np.float32, 'psroipool_rois': np.float32}},
                {'ctx': mx.gpu(0),
                 'psroipool_data': (1, 18, 14, 14),
                 'psroipool_rois': (2, 5),
                 'type_dict': {'psroipool_data': np.float16, 'psroipool_rois': np.float16}},
                ]

    check_consistency(sym, ctx_list, grad_req={'psroipool_data': 'write',
                                               'psroipool_rois': 'null'}, arg_params=arg_params)


@with_seed()
def test_deformable_psroipooling_with_type():
    tol = {np.dtype(np.float32): 1e-1,
           np.dtype(np.float64): 1e-3,
           np.dtype(np.float16): 1e-2}

    arg_params = {
        'deformable_psroipool_rois': np.array([[0, 10, 22, 161, 173], [0, 20, 15, 154, 160]])}

    # deformable psroipooling
    sym = mx.sym.contrib.DeformablePSROIPooling(spatial_scale=0.0625, sample_per_part=4, group_size=3, pooled_size=3,
                                                output_dim=2, trans_std=0.1, no_trans=False, name='deformable_psroipool')

    ctx_list = [{'ctx': mx.gpu(0),
                 'deformable_psroipool_data': (1, 18, 14, 14),
                 'deformable_psroipool_rois': (2, 5),
                 'deformable_psroipool_trans': (2, 4, 3, 3),
                 'type_dict': {'deformable_psroipool_data': np.float64, 'deformable_psroipool_rois': np.float64,
                               'deformable_psroipool_trans': np.float64}},
                {'ctx': mx.gpu(0),
                 'deformable_psroipool_data': (1, 18, 14, 14),
                 'deformable_psroipool_rois': (2, 5),
                 'deformable_psroipool_trans': (2, 4, 3, 3),
                 'type_dict': {'deformable_psroipool_data': np.float32, 'deformable_psroipool_rois': np.float32,
                               'deformable_psroipool_trans': np.float32}},
                {'ctx': mx.gpu(0),
                 'deformable_psroipool_data': (1, 18, 14, 14),
                 'deformable_psroipool_rois': (2, 5),
                 'deformable_psroipool_trans': (2, 4, 3, 3),
                 'type_dict': {'deformable_psroipool_data': np.float16, 'deformable_psroipool_rois': np.float16,
                               'deformable_psroipool_trans': np.float16}},
                ]

    check_consistency(sym, ctx_list, scale=0.1, tol=tol,
                      grad_req={'deformable_psroipool_data': 'write',
                                'deformable_psroipool_rois': 'null',
                                'deformable_psroipool_trans': 'write'}, arg_params=arg_params)


@with_seed()
def test_deformable_convolution_with_type():
    tol = {np.dtype(np.float32): 1e-1,
           np.dtype(np.float64): 1e-3}

    sym = mx.sym.contrib.DeformableConvolution(num_filter=3, kernel=(3,3), name='deformable_conv')
    # since atomicAdd does not support fp16 (which deformable conv uses in backward), we do not test fp16 here
    ctx_list = [{'ctx': mx.gpu(0),
                 'deformable_conv_data': (2, 2, 10, 10),
                 'deformable_conv_offset': (2, 18, 8, 8),
                 'type_dict': {'deformable_conv_data': np.float64, 'deformable_conv_offset': np.float64}},
                {'ctx': mx.gpu(0),
                 'deformable_conv_data': (2, 2, 10, 10),
                 'deformable_conv_offset': (2, 18, 8, 8),
                 'type_dict': {'deformable_conv_data': np.float32, 'deformable_conv_offset': np.float32}},
                # {'ctx': mx.gpu(0),
                #  'deformable_conv_data': (2, 2, 10, 10),
                #  'deformable_conv_offset': (2, 18, 8, 8),
                #  'type_dict': {'deformable_conv_data': np.float16, 'deformable_conv_offset': np.float16}},
                ]

    check_consistency(sym, ctx_list, scale=0.1, tol=tol)
    # test ability to turn off training on bias
    check_consistency(sym, ctx_list, scale=0.1, tol=tol,
                      grad_req={'deformable_conv_data': 'write',
                                'deformable_conv_offset': 'write',
                                'deformable_conv_weight': 'write',
                                'deformable_conv_bias': 'null'})


@with_seed()
def test_deformable_convolution_options():
    tol = {np.dtype(np.float32): 1e-1,
           np.dtype(np.float64): 1e-3}
    # 2D convolution

    # Pad > 0
    # since atomicAdd does not support fp16 (which deformable conv uses in backward), we do not test fp16 here
    ctx_list = [{'ctx': mx.gpu(0),
                 'deformable_conv_data': (2, 2, 7, 7),
                 'deformable_conv_offset': (2, 18, 7, 7),
                 'type_dict': {'deformable_conv_data': np.float64, 'deformable_conv_offset': np.float64}},
                {'ctx': mx.gpu(0),
                 'deformable_conv_data': (2, 2, 7, 7),
                 'deformable_conv_offset': (2, 18, 7, 7),
                 'type_dict': {'deformable_conv_data': np.float32, 'deformable_conv_offset': np.float32}},
                ]
    sym = mx.sym.contrib.DeformableConvolution(num_filter=3, kernel=(3,3), pad=(1,1), name='deformable_conv')
    check_consistency(sym, ctx_list, scale=0.1, tol=tol)

    # Stride > 1
    # since atomicAdd does not support fp16 (which deformable conv uses in backward), we do not test fp16 here
    ctx_list = [{'ctx': mx.gpu(0),
                 'deformable_conv_data': (2, 2, 7, 7),
                 'deformable_conv_offset': (2, 18, 3, 3),
                 'type_dict': {'deformable_conv_data': np.float64, 'deformable_conv_offset': np.float64}},
                {'ctx': mx.gpu(0),
                 'deformable_conv_data': (2, 2, 7, 7),
                 'deformable_conv_offset': (2, 18, 3, 3),
                 'type_dict': {'deformable_conv_data': np.float32, 'deformable_conv_offset': np.float32}},
                ]
    sym = mx.sym.contrib.DeformableConvolution(num_filter=3, kernel=(3,3), stride=(2,2), name='deformable_conv')
    check_consistency(sym, ctx_list, scale=0.1, tol=tol)

    # Dilate > 1
    # since atomicAdd does not support fp16 (which deformable conv uses in backward), we do not test fp16 here
    ctx_list = [{'ctx': mx.gpu(0),
                 'deformable_conv_data': (2, 2, 7, 7),
                 'deformable_conv_offset': (2, 18, 3, 3),
                 'type_dict': {'deformable_conv_data': np.float64, 'deformable_conv_offset': np.float64}},
                {'ctx': mx.gpu(0),
                 'deformable_conv_data': (2, 2, 7, 7),
                 'deformable_conv_offset': (2, 18, 3, 3),
                 'type_dict': {'deformable_conv_data': np.float32, 'deformable_conv_offset': np.float32}},
                ]
    sym = mx.sym.contrib.DeformableConvolution(num_filter=3, kernel=(3,3), dilate=(2,2), name='deformable_conv')
    check_consistency(sym, ctx_list, scale=0.1, tol=tol)

    # Deformable group > 1
    # since atomicAdd does not support fp16 (which deformable conv uses in backward), we do not test fp16 here
    ctx_list = [{'ctx': mx.gpu(0),
                 'deformable_conv_data': (2, 2, 7, 7),
                 'deformable_conv_offset': (2, 36, 5, 5),
                 'type_dict': {'deformable_conv_data': np.float64, 'deformable_conv_offset': np.float64}},
                {'ctx': mx.gpu(0),
                 'deformable_conv_data': (2, 2, 7, 7),
                 'deformable_conv_offset': (2, 36, 5, 5),
                 'type_dict': {'deformable_conv_data': np.float32, 'deformable_conv_offset': np.float32}},
                # {'ctx': mx.gpu(0),
                #  'deformable_conv_data': (2, 2, 7, 7),
                #  'deformable_conv_offset': (2, 36, 5, 5),
                #  'type_dict': {'deformable_conv_data': np.float16, 'deformable_offset': np.float16}},
                ]
    sym = mx.sym.contrib.DeformableConvolution(num_filter=4, kernel=(3,3), num_deformable_group=2,
                                               name='deformable_conv')

@with_seed()
@assert_raises_cudnn_not_satisfied(min_version='5.1.10')
def test_residual_fused():
    cell = mx.rnn.ResidualCell(
            mx.rnn.FusedRNNCell(50, num_layers=3, mode='lstm',
                               prefix='rnn_', dropout=0.5))

    inputs = [mx.sym.Variable('rnn_t%d_data'%i) for i in range(2)]
    outputs, _ = cell.unroll(2, inputs, merge_outputs=None)
    assert sorted(cell.params._params.keys()) == \
           ['rnn_parameters']

    args, outs, auxs = outputs.infer_shape(rnn_t0_data=(10, 50), rnn_t1_data=(10, 50))
    assert outs == [(10, 2, 50)]
    outputs = outputs.eval(ctx=mx.gpu(0),
                           rnn_t0_data=mx.nd.ones((10, 50), ctx=mx.gpu(0))+5,
                           rnn_t1_data=mx.nd.ones((10, 50), ctx=mx.gpu(0))+5,
                           rnn_parameters=mx.nd.zeros((61200,), ctx=mx.gpu(0)))
    expected_outputs = np.ones((10, 2, 50))+5
    assert np.array_equal(outputs[0].asnumpy(), expected_outputs)


def check_rnn_layer(layer):
    layer.collect_params().initialize(ctx=[mx.cpu(0), mx.gpu(0)])
    with mx.gpu(0):
        x = mx.nd.ones((10, 16, 30))
        states = layer.begin_state(16)
        go, gs = layer(x, states)

    with mx.cpu(0):
        x = mx.nd.ones((10, 16, 30))
        states = layer.begin_state(16)
        co, cs = layer(x, states)

    # atol of 1e-6 required, as exposed by seed 2124685726
    assert_almost_equal(go.asnumpy(), co.asnumpy(), rtol=1e-2, atol=1e-6)
    for g, c in zip(gs, cs):
        assert_almost_equal(g.asnumpy(), c.asnumpy(), rtol=1e-2, atol=1e-6)

def check_rnn_layer_w_rand_inputs(layer):
    layer.collect_params().initialize(ctx=[mx.cpu(0), mx.gpu(0)])
    x = mx.nd.uniform(shape=(10, 16, 30))
    with mx.gpu(0):
        x = x.copyto(mx.gpu(0))
        states = layer.begin_state(16)
        go, gs = layer(x, states)

    with mx.cpu(0):
        x = x.copyto(mx.cpu(0))
        states = layer.begin_state(16)
        co, cs = layer(x, states)

    assert_almost_equal(go.asnumpy(), co.asnumpy(), rtol=1e-2, atol=1e-6)
    for g, c in zip(gs, cs):
        assert_almost_equal(g.asnumpy(), c.asnumpy(), rtol=1e-2, atol=1e-6)

@with_seed()
def test_sequence_reverse():
    check_sequence_reverse(mx.gpu(0))


@with_seed()
def test_autograd_save_memory():
    x = mx.nd.zeros((128, 512, 512), ctx=mx.gpu(0))
    x.attach_grad()

    with mx.autograd.record():
        for i in range(200):
            x = x + 1
            x.wait_to_read()
    x.backward()


@with_seed()
def test_cuda_rtc():
    source = r'''
    extern "C" __global__ void axpy(const float *x, float *y, float alpha) {
        int i = threadIdx.x + blockIdx.x * blockDim.x;
        y[i] += alpha * x[i];
    }

    extern "C" __global__ void saxpy(const float *x, float *y, float alpha) {
        extern __shared__ float smem[];
        int i = threadIdx.x + blockIdx.x * blockDim.x;
        smem[threadIdx.x] = x[i];
        y[i] += alpha * smem[threadIdx.x];
    }
    '''
    module = mx.rtc.CudaModule(source)
    axpy = module.get_kernel("axpy", "const float *x, float *y, float alpha")
    x = mx.nd.ones((10,), ctx=mx.gpu(0))
    y = mx.nd.zeros((10,), ctx=mx.gpu(0))
    axpy.launch([x, y, 3.0], mx.gpu(0), (1, 1, 1), (10, 1, 1))
    assert (y.asnumpy() == 3).all()

    saxpy = module.get_kernel("saxpy", "const float *x, float *y, float alpha")
    saxpy.launch([x, y, 4.0], mx.gpu(0), (1, 1, 1), (10, 1, 1), 10)
    assert (y.asnumpy() == 7).all()

    saxpy.launch([x, y, 5.0], mx.gpu(0), (2, 1, 1), (5, 1, 1), 5)
    assert (y.asnumpy() == 12).all()


@with_seed()
def test_cross_device_autograd():
    x = mx.nd.random.uniform(shape=(10,))
    x.attach_grad()

    with mx.autograd.record():
        y = mx.nd.tanh(x)
        y = y.copyto(mx.gpu(0))
        y = mx.nd.tanh(y)
        y = y.copyto(mx.cpu(0))
        y = mx.nd.tanh(y)
        y = y.copyto(mx.gpu(0))
        y = y.copyto(mx.gpu(0))

        y.backward()

    dx = x.grad.asnumpy()
    x.grad[:] = 0

    with mx.autograd.record():
        y = x
        for i in range(3):
            y = mx.nd.tanh(y)
        y.backward()

    assert_almost_equal(dx, x.grad.asnumpy())

@with_seed()
def test_multi_proposal_op():
    # paramters
    feature_stride = 16
    scales = (8, 16, 32)
    ratios = (0.5, 1, 2)
    rpn_pre_nms_top_n = 12000
    rpn_post_nms_top_n = 2000
    rpn_min_size = feature_stride

    feat_len = (1000 + 15) // 16
    H, W = feat_len, feat_len
    num_anchors = len(scales) * len(ratios)
    count_anchors = H * W * num_anchors

    def get_new_data(batch_size, ctx):
        '''
        cls_prob: (batch_size, 2 * num_anchors, H, W)
        bbox_pred: (batch_size, 4 * num_anchors, H, W)
        im_info: (batch_size, 3)
        '''

        dtype = np.float32
        cls_prob = mx.nd.empty((batch_size, 2 * num_anchors, H, W), dtype = dtype, ctx = ctx)
        bbox_pred = mx.nd.empty((batch_size, 4 * num_anchors, H, W), dtype = dtype, ctx = ctx)
        im_info = mx.nd.empty((batch_size, 3), dtype = dtype, ctx = ctx)

        cls = [1.0 * (i + 1) / cls_prob.size for i in range(cls_prob.size)]
        np.random.shuffle(cls)
        cls_prob = mx.nd.reshape(mx.nd.array(cls, dtype = dtype, ctx = ctx), shape = cls_prob.shape)
        bbox_pred = mx.nd.array(np.random.randint(-2, 3, size = bbox_pred.shape), dtype = dtype, ctx = ctx)

        for i in range(batch_size):
            im_size = np.random.randint(600, feat_len * feature_stride, size = (2,))
            im_scale = np.random.randint(80, 100) / 100.0
            im_info[i, :] = [im_size[0], im_size[1], im_scale]
        return cls_prob, bbox_pred, im_info

    def check_proposal_consistency(op, batch_size, with_nms=False):
        '''
        op is mx.nd.contrib.Proposal or mx.nd.contrib.MultiProposal
        '''
        cls_prob, bbox_pred, im_info = get_new_data(batch_size, mx.cpu(0))
        rois_cpu, score_cpu = op(
                cls_prob = cls_prob,
                bbox_pred = bbox_pred,
                im_info = im_info,
                feature_stride = feature_stride,
                scales = scales,
                ratios = ratios,
                rpn_pre_nms_top_n = rpn_pre_nms_top_n,
                rpn_post_nms_top_n = rpn_post_nms_top_n,
                threshold = 0.7 if with_nms else 1.0,
                rpn_min_size = rpn_min_size, output_score = True)

        gpu_ctx = mx.gpu(0)

        # copy data to gpu from cpu
        cls_prob_gpu = cls_prob.as_in_context(gpu_ctx)
        bbox_pred_gpu = bbox_pred.as_in_context(gpu_ctx)
        im_info_gpu = im_info.as_in_context(gpu_ctx)

        rois_gpu, score_gpu = op(
                cls_prob = cls_prob_gpu,
                bbox_pred = bbox_pred_gpu,
                im_info = im_info_gpu,
                feature_stride = feature_stride,
                scales = scales,
                ratios = ratios,
                rpn_pre_nms_top_n = rpn_pre_nms_top_n,
                rpn_post_nms_top_n = rpn_post_nms_top_n,
                threshold = 0.7 if with_nms else 1.0,
                rpn_min_size = rpn_min_size, output_score = True)

        rois_cpu_np = rois_cpu.asnumpy()
        rois_gpu_np = rois_gpu.asnumpy()

        score_cpu_np = score_cpu.asnumpy()
        score_gpu_np = score_gpu.asnumpy()

        if not with_nms:
            assert_almost_equal(score_cpu_np, score_gpu_np, atol = 1e-3, rtol = 1e-3)
            assert_almost_equal(rois_cpu_np, rois_gpu_np, atol = 1e-3, rtol = 1e-3)
        else:
            # no 100% gurantee with nms
            assert(np.sum(np.abs(score_cpu_np - score_gpu_np) < 1e-3) >= 10)
            assert(np.sum(np.abs(rois_cpu_np - rois_gpu_np) < 1e-3) >= 40)

    check_proposal_consistency(mx.nd.contrib.Proposal, 1)
    check_proposal_consistency(mx.nd.contrib.MultiProposal, 5)
    check_proposal_consistency(mx.nd.contrib.Proposal, 1, with_nms=True)
    check_proposal_consistency(mx.nd.contrib.MultiProposal, 5, with_nms=True)


# The following 2 functions launch 0-thread kernels, an error that should be caught and signaled.
def kernel_error_check_imperative():
    os.environ['MXNET_ENGINE_TYPE'] = 'NaiveEngine'
    a = mx.nd.array([1,2,3],ctx=mx.gpu(0))
    b = mx.nd.array([],ctx=mx.gpu(0))
    c = (a / b).asnumpy()

def kernel_error_check_symbolic():
    os.environ['MXNET_ENGINE_TYPE'] = 'NaiveEngine'
    a = mx.sym.Variable('a')
    b = mx.sym.Variable('b')
    c = a / b
    f = c.bind(mx.gpu(0), { 'a':mx.nd.array([1,2,3],ctx=mx.gpu(0)),
                            'b':mx.nd.array([],ctx=mx.gpu(0))})
    f.forward()
    g = f.outputs[0].asnumpy()

def test_kernel_error_checking():
    # Running tests that may throw exceptions out of worker threads will stop CI testing
    # if not run in a separate process (with its own address space for CUDA compatibility).
    try:
        mpctx = mp.get_context('spawn')
    except:
        print('SKIP: python%s.%s lacks the required process fork-exec support ... ' %
              sys.version_info[0:2], file=sys.stderr, end='')
    else:
        with discard_stderr():
            for f in [kernel_error_check_imperative, kernel_error_check_symbolic]:
                p = mpctx.Process(target=f)
                p.start()
                p.join()
                assert p.exitcode != 0,\
                    "Expected a synchronous kernel error from %s(), none seen." % f.__name__

def test_incorrect_gpu():
    # Try setting dev_id to a really big number
    assert_raises(MXNetError, mx.nd.ones, (2,2), ctx=mx.gpu(100001))

@with_seed()
def test_batchnorm_backwards_notrain():
    for ctx in [mx.cpu(0), mx.gpu(0)]:
        for cudnn_o in [False, True]:
            B,C,H,W = 4,3,2,2
            x = mx.nd.random.poisson(1,shape=(B,C,H,W)).as_in_context(ctx)
            gamma = mx.nd.random.normal(shape=(C)).as_in_context(ctx)
            beta = mx.nd.random.normal(shape=(C)).as_in_context(ctx)
            mean = mx.nd.random.normal(shape=(C)).as_in_context(ctx)
            std = mx.nd.random.normal(shape=(C)).as_in_context(ctx)
            x.attach_grad()

            with autograd.record(False):
                y = mx.ndarray.BatchNorm(x, gamma, beta, mean, std.square(),
                                         fix_gamma=False, cudnn_off=cudnn_o)
                loss=y.square().sum()
            loss.backward(train_mode=False)

@with_seed()
def test_create_sparse_ndarray_gpu_to_cpu():
    dim0 = 10
    dim1 = 5
    densities = [0, 0.5, 1]
    for density in densities:
        shape = rand_shape_2d(dim0, dim1)
        matrix = rand_ndarray(shape, 'row_sparse', density)
        data = matrix.data
        indices = matrix.indices
        rsp_created = mx.nd.sparse.row_sparse_array((data, indices), shape=shape, ctx=mx.cpu())
        assert rsp_created.stype == 'row_sparse'
        assert same(rsp_created.data.asnumpy(), data.asnumpy())
        assert same(rsp_created.indices.asnumpy(), indices.asnumpy())
        rsp_copy = mx.nd.array(rsp_created)
        assert(same(rsp_copy.asnumpy(), rsp_created.asnumpy()))


@with_seed()
def test_softmax_activation():
    gpu_a = mx.nd.array([[3., 0.5, -0.5, 2., 7.],
        [2., -.4, 7.,   3., 0.2]], ctx=mx.gpu(0))
    cpu_a = mx.nd.array([[3., 0.5, -0.5, 2., 7.],
        [2., -.4, 7.,   3., 0.2]], ctx=mx.cpu())

    cpu_a.attach_grad()
    gpu_a.attach_grad()
    with mx.autograd.record():
        gpu_y = mx.nd.SoftmaxActivation(data = gpu_a)
        cpu_y = mx.nd.SoftmaxActivation(data = cpu_a)
        assert_almost_equal(cpu_y.asnumpy(), gpu_y.asnumpy(), atol = 1e-3, rtol = 1e-3)

        gpu_y.backward()
        cpu_y.backward()
        assert_almost_equal(cpu_a.grad.asnumpy(), gpu_a.grad.asnumpy(),
                atol = 1e-3, rtol = 1e-3)


@with_seed()
def test_bilinear_sampler_versions():
    data = mx.sym.Variable('data')
    grid = mx.sym.Variable('grid')
    sym1 = mx.sym.BilinearSampler(data=data, grid=grid)
    sym2 = mx.sym.BilinearSampler(data=data, grid=grid, cudnn_off=True)
    sym3 = mx.sym.BilinearSampler(data=data, grid=grid)

    test_cases = [[(1,3,15,16),(1,2,10,10)],
                 [(1,6,7,16),(1,2,10,4)],
                 [(1,7,3,16),(1,2,8,11)],
                 [(1,9,50,50),(1,2,50,50)]]

    for item in test_cases:
        data_shape, grid_shape = item
        # kWriteTo
        exe_cpu = sym1.simple_bind(data=data_shape, grid=grid_shape, ctx=mx.cpu(), grad_req='write')
        exe_gpu = sym2.simple_bind(data=data_shape, grid=grid_shape, ctx=default_context(), grad_req='write')
        exe_cudnn = sym3.simple_bind(data=data_shape, grid=grid_shape, ctx=default_context(), grad_req='write')
        exe_list = [exe_cpu, exe_gpu, exe_cudnn]
        ref_idx = 0
        test_data = np.random.uniform(low=-0.1, high=0.1,size=data_shape).astype(np.float32)
        test_grid = np.random.uniform(low=-2, high=2, size=grid_shape).astype(np.float32)
        for exe in exe_list:
            exe.arg_dict['data'][:] = test_data
            exe.arg_dict['grid'][:] = test_grid
            exe.forward(is_train=True)
            assert_almost_equal(exe_list[ref_idx].outputs[0].asnumpy(), exe.outputs[0].asnumpy(), rtol=1e-3, atol=1e-5)

        out_grad = np.random.uniform(low=-0.01, high=0.01,size=data_shape[:2] + grid_shape[2:]).astype(np.float32)
        for exe in exe_list:
            exe.backward(mx.nd.array(out_grad))
            assert_almost_equal(exe.grad_dict['data'].asnumpy(), exe_list[ref_idx].grad_dict['data'].asnumpy(), rtol=1e-3, atol=1e-5)
            assert_almost_equal(exe.grad_dict['grid'].asnumpy(), exe_list[ref_idx].grad_dict['grid'].asnumpy(), rtol=1e-3, atol=1e-5)

        data_grad = exe_list[ref_idx].grad_dict['data'].asnumpy()
        grid_grad = exe_list[ref_idx].grad_dict['grid'].asnumpy()

        # kAddTo
        exe_cpu_addto = sym1.simple_bind(data=data_shape, grid=grid_shape, ctx=mx.cpu(), grad_req='add')
        exe_gpu_addto = sym2.simple_bind(data=data_shape, grid=grid_shape, ctx=default_context(), grad_req='add')
        exe_cudnn_addto = sym3.simple_bind(data=data_shape, grid=grid_shape, ctx=default_context(), grad_req='add')
        exe_list = [exe_cpu_addto, exe_gpu_addto, exe_cudnn_addto]
        data_initial_grad = np.random.normal(size=exe_list[ref_idx].grad_dict['data'].shape).astype(np.float32)
        grid_initial_grad = np.random.normal(size=exe_list[ref_idx].grad_dict['grid'].shape).astype(np.float32)
        for exe in exe_list:
            exe.arg_dict['data'][:] = test_data
            exe.arg_dict['grid'][:] = test_grid
            exe.grad_dict['data'][:] = data_initial_grad
            exe.grad_dict['grid'][:] = grid_initial_grad
            exe.forward(is_train=True)
            exe.backward(mx.nd.array(out_grad))
            assert_almost_equal(exe.grad_dict['data'].asnumpy(), exe_list[ref_idx].grad_dict['data'].asnumpy(), rtol=1e-3, atol=1e-5)
            assert_almost_equal(exe.grad_dict['grid'].asnumpy(), exe_list[ref_idx].grad_dict['grid'].asnumpy(), rtol=1e-3, atol=1e-5)
        assert_almost_equal(exe_list[ref_idx].grad_dict['data'].asnumpy(), data_grad + data_initial_grad, rtol=1e-3, atol=1e-5)
        assert_almost_equal(exe_list[ref_idx].grad_dict['grid'].asnumpy(), grid_grad + grid_initial_grad, rtol=1e-3, atol=1e-5)

        for req_dict in [{'data' : 'null', 'grid' : 'write'}, {'data' : 'write', 'grid' : 'null'}]:
            # Mixture of kWriteTo and kNullOp
            exe_cpu_mix = sym1.simple_bind(data=data_shape, grid=grid_shape, ctx=mx.cpu(), grad_req=req_dict)
            exe_gpu_mix = sym2.simple_bind(data=data_shape, grid=grid_shape, ctx=default_context(), grad_req=req_dict)
            exe_cudnn_mix = sym3.simple_bind(data=data_shape, grid=grid_shape, ctx=default_context(), grad_req=req_dict)
            exe_list = [exe_cpu_mix, exe_gpu_mix, exe_cudnn_mix]
            for exe in exe_list:
                exe.arg_dict['data'][:] = test_data
                exe.arg_dict['grid'][:] = test_grid
                exe.forward(is_train=True)
                exe.backward(mx.nd.array(out_grad))
                if req_dict['data'] is 'write':
                    assert_almost_equal(exe.grad_dict['data'].asnumpy(), exe_list[ref_idx].grad_dict['data'].asnumpy(), rtol=1e-3, atol=1e-5)
                if req_dict['grid'] is 'write':
                    assert_almost_equal(exe.grad_dict['grid'].asnumpy(), exe_list[ref_idx].grad_dict['grid'].asnumpy(), rtol=1e-3, atol=1e-5)


def test_context_num_gpus():
    # Test that num_gpus reports at least one GPU, as the test is run on a GPU host.
    assert mx.context.num_gpus() > 0

if __name__ == '__main__':
    import nose
    nose.runmodule()
