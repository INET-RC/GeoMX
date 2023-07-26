import os
import time
import json
import shutil
import mxnet as mx
from mxnet.gluon import data as gdata
from mxnet.gluon import utils as gutils


class SplitSampler(gdata.sampler.Sampler):
    def __init__(self, length, num_parts=1, part_index=0):
        self.part_len = length // num_parts
        self.start = self.part_len * part_index
        self.end = self.start + self.part_len

    def __iter__(self):
        indices = list(range(self.start, self.end))
        return iter(indices)

    def __len__(self):
        return self.part_len


class ClassSplitSampler(gdata.sampler.Sampler):
    def __init__(self, class_list, length, num_parts=1, part_index=0):
        self.class_list = class_list
        self.part_len = length // num_parts
        self.start = self.part_len * part_index
        self.end = self.start + self.part_len

    def __iter__(self):
        indices = self.class_list[self.start:self.end]
        return iter(indices)

    def __len__(self):
        return self.part_len


def load_data(batch_size,
              num_workers=1,
              data_slice_idx=0,
              data_type="mnist",
              split_by_class=False,
              resize=None,
              root="/root/data"):
    assert data_slice_idx < num_workers, \
        "Invalid slice id (%s), a slice id smaller than num_workers (%s) is required." \
        % (data_slice_idx, num_workers)

    if not os.path.exists(root): os.mkdir(root)
    root = os.path.join(root, data_type)
    root = os.path.expanduser(root)
    if data_type == "fashion-mnist":
        train = gdata.vision.FashionMNIST(root=root, train=True)
        test = gdata.vision.FashionMNIST(root=root, train=False)
    elif data_type == "mnist":
        train = gdata.vision.MNIST(root=root, train=True)
        test = gdata.vision.MNIST(root=root, train=False)
    elif data_type == "cifar10":
        train = gdata.vision.CIFAR10(root=root, train=True)
        test = gdata.vision.CIFAR10(root=root, train=False)
    else:
        raise NotImplementedError("Dataset %s not support" % data_type)

    if num_workers > 1:
        if split_by_class:
            num_classes = 10
            class_list = [[] for _ in range(num_classes)]
            for idx, sample in enumerate(train):
                class_list[sample[1]].append(idx)
            flat_class_list = []
            for class_ in class_list:
                flat_class_list += class_
            sampler = ClassSplitSampler(flat_class_list, len(train), num_workers, data_slice_idx)
        else:
            sampler = SplitSampler(len(train), num_workers, data_slice_idx)
    else:
        sampler = None

    transformer = []
    if resize:
        transformer += [gdata.vision.transforms.Resize(resize)]
    transformer += [gdata.vision.transforms.ToTensor()]
    transformer = gdata.vision.transforms.Compose(transformer)

    train_iter = gdata.DataLoader(train.transform_first(transformer), batch_size, sampler=sampler, num_workers=0)
    test_iter = gdata.DataLoader(test.transform_first(transformer), batch_size, num_workers=0)
    return train_iter, test_iter, len(train), len(test)


def get_batch(batch, ctx):
    features, labels = batch
    if type(ctx) == mx.Context:
        ctx = [ctx]
    if labels.dtype != features.dtype:
        labels = labels.astype(features.dtype)
    return (gutils.split_and_load(features, ctx),
            gutils.split_and_load(labels, ctx), features.shape[0])


def eval_acc(test_iter, net, ctx):
    test_acc = 0.0
    for X, y in test_iter:
        X, y = X.as_in_context(ctx), y.as_in_context(ctx)
        pred_class = net(X).argmax(axis=1)
        batch_acc = (pred_class == y.astype('float32')).mean().asscalar().astype('float64')
        test_acc += batch_acc
    return test_acc / len(test_iter)


def try_gpu():
    try:
        ctx = mx.gpu(0)
        _ = mx.nd.zeros((1,), ctx=ctx)
    except mx.base.MXNetError:
        ctx = mx.cpu()
    return ctx


class Measure:
    def __init__(self, log_dir, sub_dir):
        self.num_iters = -1
        self.self_iter = -1
        self.begin = time.time()
        self.total_time = -1
        self.start_time = 0.
        self.time_map = {}
        self.accuracy = 0.
        self.num_sampels = 0
        self.log_path = os.path.join(log_dir, sub_dir)
        if not os.path.exists(log_dir):
            os.mkdir(log_dir)
        if os.path.exists(self.log_path):
            shutil.rmtree(self.log_path)
        os.mkdir(self.log_path)

    def set_begin_time(self, ts):
        self.begin = ts

    def get_begin_time(self):
        return self.begin

    def set_num_iters(self, num_iters):
        assert num_iters >= 0
        self.num_iters = num_iters

    def next_iter(self):
        self.self_iter += 1
        self.time_map[self.self_iter] = {}

    def start(self, name):
        self.time_map[self.self_iter][name] = 0
        self.start_time = time.time()

    def stop(self, name):
        if self.time_map.get(self.self_iter, -1) != -1 and \
                self.time_map[self.self_iter].get(name, -1) == 0:
            self.time_map[self.self_iter][name] = round(time.time() - self.start_time, 4)

    def set_accuracy(self, accuracy):
        self.accuracy = accuracy

    def add_samples(self, num_samples):
        self.num_sampels += num_samples

    def reset(self, num_iters=-1):
        self.start_time = 0.
        self.time_map = {}
        self.self_iter = -1
        self.accuracy = 0.
        self.num_sampels = 0
        if num_iters != -1:
            self.num_iters = num_iters

    def save_report(self):
        if self.num_iters == -1:
            print("[Error] Incorrect iteration number %d." % self.num_iters)
            return -1

        log = {
            "num_iters": self.num_iters,
            "time": self.time_map,
            "num_samples": self.num_sampels,
            "total_time": time.time() - self.begin
        }

        if self.accuracy:
            log.update({"accuracy": self.accuracy})

        log_file = os.path.join(self.log_path, "iter-%d.txt" % self.num_iters)
        with open(log_file, "w") as fp:
            json.dump(log, fp)
