#!/usr/bin/env python

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

# coding: utf-8
"""MXNet: a concise, fast and flexible framework for deep learning."""
from __future__ import absolute_import

from .context import Context, current_context, cpu, gpu, cpu_pinned
from . import engine
from .base import MXNetError
from . import base
from . import contrib
from . import ndarray
from . import ndarray as nd
from . import name
# use mx.sym as short for symbol
from . import symbol as sym
from . import symbol
from . import symbol_doc
from . import io
from . import recordio
from . import operator
# use mx.rnd as short for mx.random
from . import random as rnd
from . import random
from . import optimizer
from . import model
from . import metric
from . import notebook
from . import initializer
# use mx.init as short for mx.initializer
from . import initializer as init
from . import visualization
# use viz as short for mx.ndarray
from . import visualization as viz
from . import callback
# from . import misc
from . import lr_scheduler
# use mx.kv as short for kvstore
from . import kvstore as kv
from . import kvstore_server
# Runtime compile module
from . import rtc
# Attribute scope to add attributes to symbolic graphs
from .attribute import AttrScope

from . import monitor
from . import monitor as mon

from . import torch
from . import torch as th

from . import profiler
from . import log

from . import module
from . import module as mod

from . import image
from . import image as img

from . import test_utils

from . import rnn

from . import gluon

__version__ = base.__version__
