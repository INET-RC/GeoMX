#!/usr/bin/env python
# -*- coding: utf-8 -*-

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

"""
CNN_HFA.py contains code that trains a simple network with HierFAVG-styled synchronous algorithm.
"""

import time
import os
import mxnet as mx
import argparse
import logging
from utils import load_data, get_batch, eval_acc, try_gpu
from mxnet.gluon import Trainer


def main():
    logging.basicConfig(level=logging.DEBUG)
    parser = argparse.ArgumentParser()
    parser.add_argument("-lr", "--learning-rate", type=float, default=0.01)
    parser.add_argument("-bs", "--batch-size", type=int, default=32)
    parser.add_argument("-ds", "--data-slice-idx", type=int, default=0)
    parser.add_argument("-ep", "--epoch", type=int, default=5)
    parser.add_argument("-sc", "--split-by-class", action="store_true")
    parser.add_argument("-c", "--cpu", action="store_true")
    args = parser.parse_args()

    learning_rate = args.learning_rate
    batch_size = args.batch_size
    data_slice_idx = args.data_slice_idx
    epochs = args.epoch
    split_by_class = args.split_by_class
    ctx = mx.cpu() if args.cpu else try_gpu()
    use_hfa = int(os.getenv('MXNET_KVSTORE_USE_HFA'))
    period_k1 = int(os.getenv('MXNET_KVSTORE_HFA_K1'))
    period_k2 = int(os.getenv('MXNET_KVSTORE_HFA_K2'))
    assert use_hfa == 1, 'MXNET_KVSTORE_USE_HFA is not properly set'
    assert period_k1 >= 1, 'MXNET_KVSTORE_HFA_K1 is not properly set'
    assert period_k2 >= 1, 'MXNET_KVSTORE_HFA_K2 is not properly set'

    data_type = "mnist"
    data_dir = "/root/data"
    shape = (batch_size, 1, 28, 28)

    net = mx.gluon.nn.Sequential()
    net.add(mx.gluon.nn.Conv2D(channels=6, kernel_size=5, activation='sigmoid'),
            mx.gluon.nn.MaxPool2D(pool_size=2, strides=2),
            mx.gluon.nn.Conv2D(channels=16, kernel_size=5, activation='sigmoid'),
            mx.gluon.nn.MaxPool2D(pool_size=2, strides=2),
            mx.gluon.nn.Dense(120, activation='sigmoid'),
            mx.gluon.nn.Dense(84, activation='sigmoid'),
            mx.gluon.nn.Dense(10))

    loss = mx.gluon.loss.SoftmaxCrossEntropyLoss()
    net.initialize(force_reinit=True, ctx=ctx, init=mx.init.Xavier())
    net(mx.nd.random.uniform(shape=shape, ctx=ctx))

    kvstore_dist = mx.kv.create("dist_sync")
    is_master_worker = kvstore_dist.is_master_worker
    adam_optimizer = mx.optimizer.Adam(learning_rate=learning_rate)
    trainer = Trainer(net.collect_params(),
                      optimizer=adam_optimizer,
                      kvstore=None,
                      update_on_kvstore=False)

    num_all_workers = kvstore_dist.num_all_workers
    num_local_workers = kvstore_dist.num_workers
    my_rank = kvstore_dist.rank
    # waiting for configurations to complete
    time.sleep(1)

    params = list(net.collect_params().values())
    for idx, param in enumerate(params):
        if param.grad_req == "null":
            continue
        kvstore_dist.init(idx, param.data())
        if is_master_worker:
            continue
        kvstore_dist.pull(idx, param.data(), priority=-idx)
    mx.nd.waitall()
    
    if is_master_worker:
        return

    train_iter, test_iter, _, _ = load_data(
        batch_size,
        num_all_workers,
        data_slice_idx,
        data_type=data_type,
        split_by_class=split_by_class,
        resize=shape[-2:],
        root=data_dir
    )
    
    begin_time = time.time()
    eval_time = 0
    global_iters = 1
    local_iters = 1

    print(f"Start training on {num_all_workers} workers, my rank is {my_rank}.")
    for epoch in range(epochs):
        for _, batch in enumerate(train_iter):
            Xs, ys, num_samples = get_batch(batch, ctx)
            with mx.autograd.record():
                y_hats = [net(X) for X in Xs]
                ls = [loss(y_hat, y) for y_hat, y in zip(y_hats, ys)]
            for l in ls:
                l.backward()

            trainer.step(num_samples)
            if local_iters % period_k1 == 0:
                for idx, param in enumerate(params):
                    if param.grad_req == "null":
                        continue
                    kvstore_dist.push(idx, param.data() / num_local_workers, priority=-idx)
                    kvstore_dist.pull(idx, param.data(), priority=-idx)
            mx.nd.waitall()

            global_iters += 1

            # run evaluation
            test_acc = eval_acc(test_iter, net, ctx)
            print("[Time %.3f][Epoch %d][Iteration %d] Test Acc %.4f"
                  % (time.time() - begin_time, epoch, global_iters, test_acc))


if __name__ == "__main__":
    main()
