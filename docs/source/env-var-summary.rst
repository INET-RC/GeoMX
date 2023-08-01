Summary of Env Variables
------------------------

.. list-table:: Summary of Basic Environment Variables Using by GeoMX.
   :header-rows: 1

   * - Environment Variable
     - Options
     - Node Used On
     - Description
   * - DMLC_ROLE
     - scheduler, server, worker
     - local scheduler, local server, master worker, worker
     - The role of the node within the party.
   * - DMLC_PS_ROOT_URI
     - IPv4 address
     - global server, master worker, local scheduler, local server, worker
     - IPv4 address of the local scheduler node.
   * - DMLC_PS_ROOT_PORT
     - Integer
     - same as above
     - Port number of the local scheduler node.
   * - DMLC_NUM_SERVER
     - Integer
     - same as above
     - Number of local servers in the participating party, or number of global servers in the central party.
   * - DMLC_NUM_WORKER
     - Integer
     - same as above
     - Number of workers in the current party, including the master worker.
   * - DMLC_ROLE_GLOBAL
     - global_scheduler, global_server
     - global scheduler, global server
     - The role of the node across different parties.
   * - DMLC_PS_GLOBAL_ROOT_URI
     - IPv4 address
     - global scheduler, global server, local server
     - IPv4 address of the global scheduler node.
   * - DMLC_PS_GLOBAL_ROOT_PORT
     - Integer
     - same as above
     - Port number of the global scheduler node.
   * - DMLC_NUM_GLOBAL_SERVER
     - Number
     - same as above
     - Number of global servers in the central party.
   * - DMLC_NUM_GLOBAL_WORKER
     - Number
     - same as above
     - Number of local servers worldwide.
   * - DMLC_ROLE_MASTER_WORKER
     - 0, 1
     - master worker
     - Specify if the current node is the master worker.
   * - DMLC_ENABLE_CENTRAL_WORKER
     - 0, 1
     - global server
     - Specify if the central party joins in model training.
   * - DMLC_NUM_ALL_WORKER
     - Number
     - global server, master worker, worker
     - Total number of workers actually participating in model training.
   * - DMLC_INTERFACE
     - String
     - all
     - Name of the network interface used by the node.
   * - PS_VERBOSE
     - 0, 1, 2
     - all
     - Verbosity level of the system logs.


.. list-table:: Summary of Environment Variable for Each Optimization Technology.
   :widths: 30 30 40
   :header-rows: 1

   * - Technology
      - Environment Variable
      - Description

   * - :ref:`HFA <hierarchical-frequency-aggregation>`
     - MXNET_KVSTORE_USE_HFA
     - Enable or disable HFA synchronization.

   * -
     - MXNET_KVSTORE_HFA_K1
     - Number of loops before a local synchronization.

   * -
     - MXNET_KVSTORE_HFA_K2
     - Number of loops before a global synchronization.

   * - :ref:`Bi-Sparse <bidirectional-gradient-sparsification>`, :ref:`MPQ <mixed-precision-quantization>`
     - MXNET_KVSTORE_SIZE_LOWER_BOUND
     - Size lower bound for classifying large and tiny tensors.

   * - :ref:`DGT <differential-gradient-transmission>`
     - ENABLE_DGT
     - Enable or disable DGT protocol, set to 2 for enable.

   * -
     - DMLC_UDP_CHANNEL_NUM
     - Number of channels used for transmission.

   * -
     - DMLC_K
     - Compression ratio.

   * -
     - ADAPTIVE_K_FLAG
     - Enable or disable adaptive K value adjustment.

   * - :ref:`TSEngine <tsengine>`
     - ENABLE_INTER_TS
     - Enable or disable TSEngine within the data center.

   * -
     - ENABLE_INTRA_TS
     - Enable or disable TSEngine between data centers.

   * -
     - MAX_GREED_RATE_TS
     - Probability set for random exploration.

   * - :ref:`P3 <priority-based-parameter-propagation>`
     - ENABLE_P3
     - Enable or disable P3 scheduler.
