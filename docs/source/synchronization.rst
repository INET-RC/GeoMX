How to Use GeoMX Synchronization?
=================================

GeoMX currently supports two fundamental synchronization algorithms,
i.e., the fully-synchronous algorithm, the mixed-synchronous algorithm,
and an advanced algorithm, i.e., hierarchical frequency aggregation.

Fully-Synchronous Algorithm
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Fully-Synchronous Algorithm (FSA) is the default strategy for model
synchronization. In this synchronous algorithm, training nodes
synchronize their model data (can be parameters or gradients) each
round, and both parameter server systems within and between data centers
run in a synchronous parallel mode. All training nodes are synchronized
to ensure a consistent model. However, this comes at the expense of
training speed, as it requires waiting for all computations and
communications to complete at every iteration.

To use FSA, all that’s required is to set ``dist_sync`` as a
hyperparameter during the initialization of ``kvstore``. For example:

.. code:: python

   import mxnet as mx

   # Initialize distributed kvstore in synchronous mode.
   kvstore_dist = mx.kv.create("dist_sync")

   # Master worker sets the optimizer of the global parameter server to Adam.
   if kvstore_dist.is_master_worker:
       kvstore_dist.set_optimizer(mx.optimizer.Adam(learning_rate=lr))

   for epoch in range(num_epochs):
       for _, batch in enumerate(train_iter):
           # Perform forward and backward propagation to calculate gradients.
           ...
           # Synchronize gradients to obtain updated parameters.
           for idx, param in enumerate(net_params):
               if param.grad_req == "null": continue
               kvstore_dist.push(idx, param.grad(), priority=-idx)
               kvstore_dist.pull(idx, param.data(), priority=-idx)

The demo code can be found in
`examples/cnn.py <https://github.com/INET-RC/GeoMX/blob/main/examples/cnn.py>`_.
You can run this demo by simply
``bash scripts/xpu/run_vanilla_hips.sh``, where ``xpu`` should be
``cpu`` or ``gpu``.

Mixed-Synchronous Algorithm
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Mixed-Synchronous Algorithm (MixedSync) is an asynchronous version of
FSA, where the difference is that the parameter server system between
data centers runs in an asynchronous parallel mode. This setup is
particularly suitable for scenarios where intra-data center training
nodes display high homogeneity, yet there is significant resource
heterogeneity between different data centers. This asynchronous method
resolves the problem of straggling data centers, thereby accelerating
distributed training across WANs.

To use MixedSync, all that’s required is to set ``dist_async`` instead
of ``dist_sync`` when initializing ``kvstore``. The rest of the setup
remains the same:

.. code:: python

   import mxnet as mx

   kvstore_dist = mx.kv.create("dist_async")

Alternatively, you can enable MixedSync by using the ``--mixed-sync``
option in our provided python script:

.. code:: bash

   python examples/cnn.py --mixed-sync

You can also run this demo by executing
``bash scripts/xpu/run_mixed_sync.sh``, where ``xpu`` should be ``cpu``
or ``gpu``.

Use DCASGD Optimizer in MixedSync
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To alleviate the issue of stale gradients in asynchronous parallel
operations, the global parameter server can be configured to use the
`DCASGD <http://proceedings.mlr.press/v70/zheng17b/zheng17b.pdf>`__
optimizer. This adjustment aids in improving training convergence while
preserving model accuracy.

The way to enable DCASGD in MixedSync is the same as in MXNET: simply
replace the ``Adam`` optimizer with the ``DCASGD`` optimizer:

.. code:: python

   import mxnet as mx

   kvstore_dist = mx.kv.create("dist_async")
   if kvstore_dist.is_master_worker:
       kvstore_dist.set_optimizer(mx.optimizer.DCASGD(learning_rate=lr))

We can use the following command to enable the DCASGD optimizer in
MixedSync:

.. code:: bash

   python examples/cnn.py --mixed-sync --dcasgd

Just modify ``scripts/xpu/run_mixed_sync.sh`` and try it!

.. _hierarchical-frequency-aggregation:

Hierarchical Frequency Aggregation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Inspired by `this
paper <https://ieeexplore.ieee.org/abstract/document/9148862>`__, our
Hierarchical Frequency Aggregation (HFA) algorithm first performs
:math:`K_1` steps of local updates at the training nodes, followed by
:math:`K_2` steps of synchronizations at the local parameter server.
Finally, a global synchronization is performed at the global parameter
server. This approach effectively reduces the frequency of model
synchronization across data centers, thereby boosting distributed
training.

To enable HFA, we initialize ``kvstore`` in ``dist_sync`` mode and make
a simple modification to the training loop:

.. code:: python

   import mxnet as mx

   # Initialize distributed kvstore in synchronous mode.
   kvstore_dist = mx.kv.create("dist_sync")

   # Obtain K1 from environmental variables.
   period_k1 = int(os.getenv('MXNET_KVSTORE_HFA_K1'))

   # Obtain the number of training nodes in each data center.
   num_local_workers = kvstore_dist.num_workers

   # Define local trainer to use Adam optimizer.
   optimizer = mx.optimizer.Adam(learning_rate=lr)
   trainer = Trainer(net.collect_params(), optimizer=optimizer)

   global_iters = 1
   for epoch in range(num_epochs):
       for _, batch in enumerate(train_iter):
           # Perform forward and backward propagation to calculate gradients.
           ...
           # Update local model parameters.
           trainer.step(num_samples)
           # Synchronize model parameters every K1 round.
           if global_iters % period_k1 == 0:
               for idx, param in enumerate(net_params):
                   kvstore_dist.push(idx, param.data() / num_local_workers, priority=-idx)
                   kvstore_dist.pull(idx, param.data(), priority=-idx)
           # Update the iteration counter
           global_iters += 1

Then, let’s set three environmental variables:

.. code:: shell

   MXNET_KVSTORE_USE_HFA = 1  # whether HFA is enabled
   MXNET_KVSTORE_HFA_K1 = 20  # number of loops before a local synchronization
   MXNET_KVSTORE_HFA_K2 = 10  # number of loops before a global synchronization

The demo code can be found in
`examples/cnn_hfa.py <https://github.com/INET-RC/GeoMX/blob/main/examples/cnn_hfa.py>`_.
You can run this demo by simply ``bash scripts/xpu/run_hfa_sync.sh``,
where ``xpu`` should be ``cpu`` or ``gpu``.