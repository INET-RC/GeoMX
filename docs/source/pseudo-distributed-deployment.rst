Deploy GeoMX in Pseudo-distributed Mode
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The pseudo-distributed deployment is designed for quick trial and
debugging purposes. In this setup, all nodes are launched within a
single Docker container and their IP addresses are all set to
``127.0.0.1``. This removes the need for additional network
configuration. While this method is handy for getting a quick
understanding of how the system operates, it is not meant for deployment
in a production environment.

A basic shell script for pseudo-distributed deployment can be found
`here <https://github.com/INET-RC/GeoMX/blob/main/scripts/cpu/run_vanilla_hips.sh>`__.
In this script, we launched a total of 12 nodes, each command
corresponds to running a different node, with roles specified by
``DMLC_ROLE`` and ``DMLC_ROLE_GLOBAL``.

.. warning::

   Please ensure the global scheduler and local schedulers are initiated
   prior to the other nodes within each party.

Launch Nodes in the Central Party
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The central party consists of 4 nodes: a global scheduler, a local
scheduler, a global server, and a master worker.

The global scheduler is used to manage the global server and local
servers (in other parties). Use the following commands to launch it:

.. code:: shell

   DMLC_ROLE_GLOBAL=global_scheduler \
   DMLC_PS_GLOBAL_ROOT_URI=127.0.0.1 \
   DMLC_PS_GLOBAL_ROOT_PORT=9092 \
   DMLC_NUM_GLOBAL_SERVER=1 \
   DMLC_NUM_GLOBAL_WORKER=2 \
   PS_VERBOSE=1 \
   DMLC_INTERFACE=eth0 \
   nohup python -c "import mxnet" > /dev/null &

These environment variables are defined as follows:

#. ``DMLC_ROLE_GLOBAL``: The role of the current process. In this case, it is a ``global_scheduler`` node. It could also be set to ``global_server``.

#. ``DMLC_PS_GLOBAL_ROOT_URI``: The IP address of the global scheduler. In this case, it is set to ``127.0.0.1``, meaning the process is running on the local machine.

#. ``DMLC_PS_GLOBAL_ROOT_PORT``: The port that the global scheduler binds to. In this case, the port is set to 9092.

#. ``DMLC_NUM_GLOBAL_SERVER``: The number of global servers. In this case, it is set to 1, meaning there is only one global server.

#. ``DMLC_NUM_GLOBAL_WORKER``: The number of local servers, i.e., the number of participating data centers. Here, it is set to 2, representing 2 participating data centers (Party A and Party B).

#. ``PS_VERBOSE``: The level of detail in the logs. Setting it to 0 disables log outputs, 1 outputs necessary log information, and 2 outputs log details.

#. ``DMLC_INTERFACE``: This specifies the network interface used for inter-process communication. In this case, it is set to ``eth0``. This should be replaced with the actual network interface name used by your system or container.

Then, we launch a local scheduler, used to manage the global server and the master worker.

.. code:: shell

   DMLC_ROLE=scheduler \
   DMLC_PS_ROOT_URI=127.0.0.1 \
   DMLC_PS_ROOT_PORT=9093 \
   DMLC_NUM_SERVER=1 \
   DMLC_NUM_WORKER=1 \
   PS_VERBOSE=1 \
   DMLC_INTERFACE=eth0 \
   nohup python -c "import mxnet" > /dev/null &

Some new environment variables introduced here control intra-party
behaviors:

#. ``DMLC_ROLE``: The role of the current process. In this case, it is a ``scheduler`` node. It could also be set to ``server`` and ``worker``.

#. ``DMLC_PS_ROOT_URI``: The IP address of the local scheduler. Here, it is set to ``127.0.0.1``, meaning the local scheduler runs on the local machine.

#. ``DMLC_PS_ROOT_PORT``: The port that the local scheduler binds to. It should differ from other schedulers (and the global scheduler) if they’re launched on the same machine. Here, the port number is set to 9093.

#. ``DMLC_NUM_SERVER``: In the central party, this indicates the number of global server nodes. Here, it is set to 1.

#. ``DMLC_NUM_WORKER``: In the central party, this indicates the number of worker nodes (and the master worker). Here, we have only one master worker, so this value is set to 1.

To launch the global server, run the following commands:

.. code:: shell

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
   nohup python -c "import mxnet" > /dev/null &

In this case, ``DMLC_PS_GLOBAL_ROOT_URI`` and
``DMLC_PS_GLOBAL_ROOT_PORT`` refer to the setup of the global scheduler,
while ``DMLC_PS_ROOT_URI`` and ``DMLC_PS_ROOT_PORT`` refer to the setup
of the local scheduler.

Other environment variables are as follows:

#. ``DMLC_ENABLE_CENTRAL_WORKER``: This option enables or disables the central party to participate in model training. If set to 0, the central party only provides a master worker to initialize the global server. If set to 1, the central party can provide a worker cluster to participate in model training, with the master worker attached to a worker node.

#. ``DMLC_NUM_ALL_WORKER``: The total number of worker nodes worldwide participating in model training. Here, with 2 workers in Party A and 2 workers in Party B, it’s set to 4. Note that although the master worker is also a worker node, in this case it does not participate in model training, so it is not counted.

Lastly, we launch the master worker.

.. code:: shell

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

The master worker sets ``DMLC_ROLE_MASTER_WORKER=1`` to announce itself
as a master worker node. It establishes a socket connection with the
local scheduler, thus ``DMLC_PS_ROOT_URI=127.0.0.1`` and
``DMLC_PS_ROOT_PORT=9093`` are set to ensure that the master worker can
find the local scheduler.

Launch Nodes in Other Parties
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Next, we will be launching a scheduler, a parameter server, and two
workers in the other parties. Let’s take one of them as an example.

First, we’ll start with launching the local scheduler:

.. code:: shell

   DMLC_ROLE=scheduler \
   DMLC_PS_ROOT_URI=127.0.0.1 \
   DMLC_PS_ROOT_PORT=9094 \
   DMLC_NUM_SERVER=1 \
   DMLC_NUM_WORKER=2 \
   PS_VERBOSE=1 \
   DMLC_INTERFACE=eth0 \
   nohup python -c "import mxnet" > /dev/null &

This setup is similar to that of the local scheduler in the central
party, but in this context, ``DMLC_NUM_SERVER`` specifies the number of
local parameter servers within the current party, which typically sets
to 1. Furthermore, ``DMLC_NUM_WORKER`` specifies the number of worker
nodes within the current party. As we’re planning to launch two worker
nodes in this party, here we set this value to 2.

Next, we launch the local parameter server:

.. code:: shell

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
   nohup python -c "import mxnet" > /dev/null &

As we mentioned above, a parameter server is required to establish
socket connections with both the local and global schedulers. Thus, it
needs to know the IP and port address of both the local scheduler and
the global scheduler.

Finally, we’ll launch two worker nodes:

.. code:: shell

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
   python ${EXAMPLE_PYTHON_SCRIPT} --data-slice-idx 1 --cpu

The worker nodes are launched in a similar manner as before, but they
connect to their own local scheduler within their party.

The training data is divided among worker nodes. Each worker gets a
slice of data to process, which is specified by the ``--data-slice-idx``
option. For example, the first worker gets the 0th slice of the data,
and the second worker gets the 1st slice of the data.

.. warning::

   This demo task might encounter errors due to a missing dataset.
   If this occurs, there's no need for concern as the script is implemented
   to automatically download the required dataset. You just need to restart
   this demo task.

   If you're using our pre-built images, the demo dataset is already placed
   within the image, thus eliminating this issue.