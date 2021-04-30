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
MKL-DNN related test cases
"""

import sys
import os
import logging


def test_mkldnn_install():
    """
    This test will verify that MXNet is built/installed correctly when
    compiled with Intel MKL-DNN library. The method will try to import
    the mxnet module and see if the mkldnn library is mapped to this
    process's address space.
    """
    logging.basicConfig(level=logging.INFO)

    if not sys.platform.startswith('linux'):
        logging.info("Bypass mkldnn install test for non-Linux OS")
        return

    try:
        #pylint: disable=unused-variable
        import mxnet as mx
    except (ImportError, OSError) as e:
        assert 0, "Import mxnet error: %s. Please double check your build/" \
            "install steps or environment variable settings" % str(e)

    pid = os.getpid()
    rc = os.system("cat /proc/" + str(pid) +
                   "/maps | grep libmkldnn > /dev/null")

    if rc == 0:
        logging.info("MXNet is built/installed correctly with MKL-DNN")
    else:
        assert 0, "MXNet is built/installed incorrectly with MKL-DNN, please " \
            "double check your build/install steps or environment " \
            "variable settings"
