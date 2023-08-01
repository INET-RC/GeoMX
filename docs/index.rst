.. GeoMX documentation master file, created by
   sphinx-quickstart on Tue Aug  1 09:46:34 2023.
   You can adapt this file completely to your liking, but it should at least
   contain the root `toctree` directive.

Welcome to GeoMX Docs! 😁
=================================

---------------------------------

.. toctree::
   :maxdepth: 3
   :caption:

   source/quick-start
   source/installation
   source/pseudo-distributed-deployment
   source/multi-host-deployment
   source/klonet-deployment
   source/synchronization
   source/accelerator
   source/env-var-summary

---------------------------------

**GeoMX** is a fast and unified distributed system for training ML algorithms over geographical data centers. Built upon the `MXNET <https://github.com/apache/mxnet>`_ framework, GeoMX integrates several sophisticated optimization techniques to enhance its training efficiency. These strategic enhancements result in a significant performance boost compared to the original MXNET system, offering 20x acceleration under identical network bandwidth conditions. This superior efficiency propels GeoMX to the forefront of training systems used for geographically dispersed data centers, showcasing satisfying performance and effectiveness.

GeoMX employs the `Hierarchical Parameter Server (HiPS) <https://www.zte.com.cn/content/dam/zte-site/res-www-zte-com-cn/mediares/magazine/publication/com_cn/article/202005/cn202005004.pdf>`_ framework as its fundamental training architecture, designed to segregate the network environment within and beyond the data center. This unique architecture includes an intra-domain parameter server system within each data center while concurrently establishing an inter-parameter server connection between different data centers. In this configuration, model data traffic undergoes two stages of aggregation: an initial local aggregation within each data center, followed by a global aggregation at the central data center. This approach effectively minimizes cross-WAN traffic and, consequently, reduces communication overhead.

But it's far from enough, given the often limited and varied network conditions in WANs, distributed training across data centers can potentially create communication bottlenecks. To mitigate these issues, GeoMX employs a variety of optimization techniques. These include gradient sparsification, low-precision quantization (e.g., fp16), mixed-precision quantization, advanced transmission protocols, synchronization algorithms, flow scheduling, and priority scheduling, among others (e.g., overlay scheduling, currently in development). These techniques comprehensively tackle communication issues, further enhancing the efficiency and robustness of distributed machine learning training in GeoMX.

.. rubric:: Optimization Techniques:

#. **Bidirectional Gradient Sparsification (Bi-Sparse)**: Traditional approaches such as `Deep Gradient Compression <https://arxiv.org/pdf/1712.01887.pdf>`_ sparsify the pushed gradient tensors. For further compression, we also sparsify the pulled (aggregated) gradient tensors rather than pulling full parameters. This technique is enabled between the global parameter server and the intra-domain parameter servers of different data centers. Refer to `this paper <https://www.zte.com.cn/content/dam/zte-site/res-www-zte-com-cn/mediares/magazine/publication/com_cn/article/202005/cn202005004.pdf>`_ for more details.

#. **Low-Precision Quantization (FP16)**: GeoMX also supports quantifying model data at lower precision for transmission, such as in FP16 format. In this scheme, GeoMX computes the model using FP32, but during transmission, it converts the model data tensor into FP16. Once the pulling data is received, GeoMX reverts it back into FP32 and continues model computing. This effectively halves the data traffic volume over both LANs and WANs.

#. **Mixed-Precision Quantization (MPQ)**: The technology of MPQ leverages both Bi-Sparse and FP16. In this scheme, tiny tensors are quantified into FP16 format for transmission, while large tensors persist in the FP32 format. Moreover, these large sensors will undergo a sparsification process before transmission. This precaution is taken to minimize the loss of crucial information and avoid significant degradation to model performance.

#. **Differential Gradient Transmission (DGT)**: This advanced transmission protocol is optimized for distributed machine learning tasks. Leveraging the tolerance of gradient descent algorithms towards partial parameter loss, this protocol transfers gradients across multiple channels, each with distinct levels of reliability and priority, contingent on their respective contributions to model convergence. Through these prioritized channels, critical gradients receive precedence in transmission, while other non-important gradients are transmitted with lower priority and reliability. This helps to reduce tail latency and thus reduce the end-to-end transmission delay of parameter synchronization. Refer to `this paper <https://drive.google.com/file/d/1IbmpFybX_qXZM2g_8BrcD9IF080qci94/view>`_ for more details and `this repo <https://github.com/zhouhuaman/dgt>`_ for individual use.

#. **TSEngine**: To solve the communication in-cast issue typically associated with centralized parameter servers, GeoMX incorporates TSEngine, an adaptive communication scheduler designed for efficient communication overlay in WANs. TSEngine dynamically optimizes the topology overlay and communication logic among the training nodes in response to real-time network conditions. This adaptive scheduler shows significant advantages over existing communication patterns in terms of system efficiency, communication, as well as scalability. Refer to `this paper <https://drive.google.com/file/d/1ELfApVoCA8WCdOe3iBe-VreLJCSD7r8r/view>`_ for more details and `this repo <https://github.com/zhouhuaman/TSEngine>`_ for individual use.

#. **Priority-based Parameter Propagation (P3)**: In conventional implementations, the gradient synchronization at round :math:`r` does not overlap with the forward propagation at round :math:`r+1`, because the forward propagation relies on the completion of gradient synchronization. To improve system efficiency, GeoMX integrates the P3 scheduler, which prioritizes the transmission of shallow-layer gradients. This setup enables overlapping between forward propagation and gradient synchronization, allowing earlier execution of forward propagation for the next round, thereby accelerating distributed training. See `this paper <https://arxiv.org/pdf/1905.03960.pdf>`_ for more details and `this repo <https://github.com/anandj91/p3>`_ for individual use.

#. **Multi-Server Load Balancing (MultiGPS)**: GeoMX supports a balanced distribution of workload, including traffic, storage, and computation, across multiple global parameter servers. By preventing any single server from becoming a bottleneck, MultiGPS significantly enhances efficiency, scalability, and overall performance of our GeoMX system.

.. rubric:: Synchronization Algorithms:

GeoMX supports two fundamental synchronization algorithms: a fully-synchronous algorithm and a mixed-synchronous algorithm.

1. **Fully-Synchronous Algorithm (FSA)**: In this synchronous algorithm, training nodes synchronize their model data (can be parameters or gradients) each round, and both parameter server systems within and between data centers run in a synchronous parallel mode. This means all training nodes are synchronized to ensure a consistent model. (Default)

.. note::
   NOTE: FSA is highly effective in maintaining model accuracy and consistency, but at the expense of training speed, as it necessitates waiting for all computations and communications to complete at every iteration.

2. **Mixed-Synchronous Algorithm (MixedSync)**: This algorithm is an asynchronous version of FSA, where the difference is that the parameter server system between data centers runs in an asynchronous parallel mode. This setup is particularly suitable for scenarios where intra-data center training nodes display high homogeneity, yet there is significant resource heterogeneity between different data centers. This asynchronous method resolves the problem of straggling data centers, thereby accelerating distributed training across WANs.

.. note::
   NOTE: To alleviate the issue of stale gradients in asynchronous parallel operations, the global parameter server can be configured to use the `DCASGD <http://proceedings.mlr.press/v70/zheng17b/zheng17b.pdf>`_ optimizer. This adjustment aids in improving training convergence while preserving model accuracy.

Building upon the two aforementioned fundamental algorithms, GeoMX also offers two advanced synchronization algorithms. These two advanced algorithms are specifically designed to address the challenges of bandwidth scarcity and resource heterogeneity in WANs.

3. **Hierarchical Frequency Aggregation (HFA)**: Inspired by `this paper <https://ieeexplore.ieee.org/abstract/document/9148862>`_, our HFA algorithm first performs :math:`K_1` steps of local updates at the training nodes, followed by :math:`K_2` steps of synchronizations at the local parameter server. Finally, a global synchronization is performed at the global parameter server. This approach effectively reduces the frequency of model synchronization across data centers, thereby boosting distributed training.

4. **ESync**: Applying asynchronous algorithms to strongly heterogeneous clusters can lead to severe stale gradient issues. To address this, we can adopt an optimized algorithm known as ESync. ESync is a synchronous parallel algorithm designed to save stragglers under conditions of strong resource heterogeneity. It introduces a state server to orchestrate the local iteration steps of each training node, in order to balance their reach-server time (including computational and transmission time). To be integrated, refer to `this paper <https://drive.google.com/file/d/1bvK0EeO5vjkXveU_ccBp4Uxl-qmbmcfn/view>`_ for more details and `this repo <https://github.com/Lizonghang/ESync>`_ for individual use.

.. note::
   For more details on the GeoMX system, please refer to our book:

   #. **虞红芳, 李宗航, 孙罡, 罗龙. 《跨数据中心机器学习：赋能多云智能数算融合》. 电子工业出版社, 人工智能前沿理论与技术应用丛书, (2023).**

   .. image:: source/_static/geomx-book.png
      :alt: Cover for GeoMX Book
      :width: 400px
      :align: center