#!/bin/bash

EXAMPLE_PYTHON_SCRIPT='../../examples/cnn_bsc.py'

# run the following nodes in a central party:
# global scheduler, global server, master worker, and scheduler
# run global scheduler in deamon process
DMLC_ROLE_GLOBAL=global_scheduler \
DMLC_PS_GLOBAL_ROOT_URI=127.0.0.1 \
DMLC_PS_GLOBAL_ROOT_PORT=9092 \
DMLC_NUM_GLOBAL_SERVER=1 \
DMLC_NUM_GLOBAL_WORKER=2 \
PS_VERBOSE=1 \
DMLC_INTERFACE=eth0 \
nohup python -c "import mxnet" > /dev/null &

# run global server in deamon process
DMLC_ROLE_GLOBAL=global_server \
DMLC_PS_GLOBAL_ROOT_URI=127.0.0.1 \
DMLC_PS_GLOBAL_ROOT_PORT=9092 \
DMLC_NUM_GLOBAL_SERVER=1 \
DMLC_NUM_GLOBAL_WORKER=2 \
DMLC_ROLE=server \
DMLC_PS_ROOT_URI=127.0.0.1 \
DMLC_PS_ROOT_PORT=9093 \
DMLC_NUM_SERVER=1 \
DMLC_NUM_WORKER=1 \
DMLC_ENABLE_CENTRAL_WORKER=0 \
DMLC_NUM_ALL_WORKER=4 \
PS_VERBOSE=1 \
DMLC_INTERFACE=eth0 \
MXNET_KVSTORE_SIZE_LOWER_BOUND=1000 \
nohup python -c "import mxnet" > /dev/null &

# run master worker in deamon process
DMLC_ROLE=worker \
DMLC_ROLE_MASTER_WORKER=1 \
DMLC_PS_ROOT_URI=127.0.0.1 \
DMLC_PS_ROOT_PORT=9093 \
DMLC_NUM_SERVER=1 \
DMLC_NUM_WORKER=1 \
DMLC_NUM_ALL_WORKER=4 \
PS_VERBOSE=1 \
DMLC_INTERFACE=eth0 \
nohup python ${EXAMPLE_PYTHON_SCRIPT} --cpu > /dev/null &

# run scheduler in deamon process
DMLC_ROLE=scheduler \
DMLC_PS_ROOT_URI=127.0.0.1 \
DMLC_PS_ROOT_PORT=9093 \
DMLC_NUM_SERVER=1 \
DMLC_NUM_WORKER=1 \
PS_VERBOSE=1 \
DMLC_INTERFACE=eth0 \
nohup python -c "import mxnet" > /dev/null &

# run the following nodes in party A:
# scheduler, server, and two workers
# run scheduler in deamon process
DMLC_ROLE=scheduler \
DMLC_PS_ROOT_URI=127.0.0.1 \
DMLC_PS_ROOT_PORT=9094 \
DMLC_NUM_SERVER=1 \
DMLC_NUM_WORKER=2 \
PS_VERBOSE=1 \
DMLC_INTERFACE=eth0 \
nohup python -c "import mxnet" > /dev/null &

# run server in deamon process
DMLC_PS_GLOBAL_ROOT_URI=127.0.0.1 \
DMLC_PS_GLOBAL_ROOT_PORT=9092 \
DMLC_NUM_GLOBAL_SERVER=1 \
DMLC_NUM_GLOBAL_WORKER=2 \
DMLC_ROLE=server \
DMLC_PS_ROOT_URI=127.0.0.1 \
DMLC_PS_ROOT_PORT=9094 \
DMLC_NUM_SERVER=1 \
DMLC_NUM_WORKER=2 \
PS_VERBOSE=1 \
DMLC_INTERFACE=eth0 \
MXNET_KVSTORE_SIZE_LOWER_BOUND=1000 \
nohup python -c "import mxnet" > /dev/null &

# run two workers in deamon process
DMLC_ROLE=worker \
DMLC_PS_ROOT_URI=127.0.0.1 \
DMLC_PS_ROOT_PORT=9094 \
DMLC_NUM_SERVER=1 \
DMLC_NUM_WORKER=2 \
DMLC_NUM_ALL_WORKER=4 \
PS_VERBOSE=1 \
DMLC_INTERFACE=eth0 \
nohup python ${EXAMPLE_PYTHON_SCRIPT} --data-slice-idx 0 --cpu > /dev/null &

DMLC_ROLE=worker \
DMLC_PS_ROOT_URI=127.0.0.1 \
DMLC_PS_ROOT_PORT=9094 \
DMLC_NUM_SERVER=1 \
DMLC_NUM_WORKER=2 \
DMLC_NUM_ALL_WORKER=4 \
PS_VERBOSE=1 \
DMLC_INTERFACE=eth0 \
nohup python ${EXAMPLE_PYTHON_SCRIPT} --data-slice-idx 1 --cpu > /dev/null &

# run the following nodes in party B:
# scheduler, server, and two workers
# run scheduler in deamon process
DMLC_ROLE=scheduler \
DMLC_PS_ROOT_URI=127.0.0.1 \
DMLC_PS_ROOT_PORT=9095 \
DMLC_NUM_SERVER=1 \
DMLC_NUM_WORKER=2 \
PS_VERBOSE=1 \
DMLC_INTERFACE=eth0 \
nohup python -c "import mxnet" > /dev/null &

# run server in deamon process
DMLC_PS_GLOBAL_ROOT_URI=127.0.0.1 \
DMLC_PS_GLOBAL_ROOT_PORT=9092 \
DMLC_NUM_GLOBAL_SERVER=1 \
DMLC_NUM_GLOBAL_WORKER=2 \
DMLC_ROLE=server \
DMLC_PS_ROOT_URI=127.0.0.1 \
DMLC_PS_ROOT_PORT=9095 \
DMLC_NUM_SERVER=1 \
DMLC_NUM_WORKER=2 \
PS_VERBOSE=1 \
DMLC_INTERFACE=eth0 \
MXNET_KVSTORE_SIZE_LOWER_BOUND=1000 \
nohup python -c "import mxnet" > /dev/null &

# run one worker in deamon process and another one not
DMLC_ROLE=worker \
DMLC_PS_ROOT_URI=127.0.0.1 \
DMLC_PS_ROOT_PORT=9095 \
DMLC_NUM_SERVER=1 \
DMLC_NUM_WORKER=2 \
DMLC_NUM_ALL_WORKER=4 \
PS_VERBOSE=1 \
DMLC_INTERFACE=eth0 \
nohup python ${EXAMPLE_PYTHON_SCRIPT} --data-slice-idx 2 --cpu > /dev/null &

DMLC_ROLE=worker \
DMLC_PS_ROOT_URI=127.0.0.1 \
DMLC_PS_ROOT_PORT=9095 \
DMLC_NUM_SERVER=1 \
DMLC_NUM_WORKER=2 \
DMLC_NUM_ALL_WORKER=4 \
PS_VERBOSE=1 \
DMLC_INTERFACE=eth0 \
python -u ${EXAMPLE_PYTHON_SCRIPT} --data-slice-idx 3 --cpu
