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

# Modifications Copyright (c) 2021 by Contributors at INET-RC

# coding: utf-8
"""A server node for the key value store."""
from __future__ import absolute_import
import ctypes
import sys
import pickle
import logging
from .base import _LIB, check_call
from .kvstore import create

class KVStoreServer(object):
    """The key-value store server."""
    def __init__(self, kvstore):
        """Initialize a new KVStoreServer.

        Parameters
        ----------
        kvstore : KVStore
        """
        self.kvstore = kvstore
        self.handle = kvstore.handle
        self.init_logginig = False

    def _controller(self):
        """Return the server controller."""
        def server_controller(cmd_id, cmd_body, _):
            """Server controler."""
            if not self.init_logginig:
                # the reason put the codes here is because we cannot get
                # kvstore.rank earlier
                head = '%(asctime)-15s Server[' + str(
                    self.kvstore.rank) + '] %(message)s'
                logging.basicConfig(level=logging.DEBUG, format=head)
                self.init_logginig = True

            if cmd_id == 0:
                try:
                    optimizer = pickle.loads(cmd_body)
                except:
                    raise
                self.kvstore.set_optimizer(optimizer)
            else:
                print("server %d, unknown command (%d, %s)" % (
                    self.kvstore.rank, cmd_id, cmd_body))
        return server_controller

    def run(self):
        """Run the server, whose behavior is like.


        >>> while receive(x):
        ...     if is_command x: controller(x)
        ...     else if is_key_value x: updater(x)
        """
        _ctrl_proto = ctypes.CFUNCTYPE(None, ctypes.c_int, ctypes.c_char_p, ctypes.c_void_p)
        check_call(_LIB.MXKVStoreRunServer(self.handle, _ctrl_proto(self._controller()), None))

def _init_kvstore_server_module():
    """Start server/scheduler."""
    is_worker = ctypes.c_int()
    is_global_scheduler = ctypes.c_int()
    check_call(_LIB.MXKVStoreIsWorkerNode(ctypes.byref(is_worker)))
    check_call(_LIB.MXKVStoreIsGlobalSchedulerNode(ctypes.byref(is_global_scheduler)))
    if is_worker.value == 0 or is_global_scheduler.value == 1:
        kvstore = create('dist')
        server = KVStoreServer(kvstore)
        server.run()
        sys.exit()

_init_kvstore_server_module()
