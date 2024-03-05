# GeoMX: Fast and unified distributed system for training ML algorithms over geographical data centers

[![GitHub stars](https://img.shields.io/github/stars/INET-RC/GeoMX)](https://img.shields.io/github/stars/INET-RC/GeoMX) 
[![GitHub forks](https://img.shields.io/github/forks/INET-RC/GeoMX)](https://github.com/INET-RC/GeoMX/network)
[![Docker Stars](https://img.shields.io/docker/stars/lizonghango00o1/geomx.svg)](https://hub.docker.com/r/lizonghango00o1/geomx)
[![Docker Pulls](https://img.shields.io/docker/pulls/lizonghango00o1/geomx.svg)](https://hub.docker.com/r/lizonghango00o1/geomx)
[![Documentation Status](https://readthedocs.org/projects/geomx/badge/?version=latest)](https://geomx.readthedocs.io/en/latest/?badge=latest)
[![Read the Docs](https://img.shields.io/readthedocs/geomx-docs-zh-cn?label=%E4%B8%AD%E6%96%87%E6%96%87%E6%A1%A3)](https://geomx.readthedocs.io/zh_CN/latest/?badge=latest)
[![GitHub license](https://img.shields.io/github/license/INET-RC/GeoMX)](https://github.com/INET-RC/GeoMX/blob/main/LICENSE)

## Introduction
GeoMX is an optimized distributed machine learning system that operates across multiple geographically dispersed data centers. Built upon the [MXNET](https://github.com/apache/mxnet) framework, GeoMX integrates several sophisticated optimization techniques to enhance its training efficiency. These strategic enhancements result in a significant performance boost compared to the original MXNET system, offering 20x acceleration under identical network bandwidth conditions. This superior efficiency propels GeoMX to the forefront of training systems used for geographically dispersed data centers, showcasing satisfying performance and effectiveness.

GeoMX employs the [Hierarchical Parameter Server (HiPS)](https://www.zte.com.cn/content/dam/zte-site/res-www-zte-com-cn/mediares/magazine/publication/com_cn/article/202005/cn202005004.pdf)<sup>1</sup> framework as its fundamental training architecture, designed to segregate the network environment within and beyond the data center. This unique architecture includes an intra-domain parameter server system within each data center while concurrently establishing an inter-parameter server connection between different data centers. In this configuration, model data traffic undergoes two stages of aggregation: an initial local aggregation within each data center, followed by a global aggregation at the central data center. This approach effectively minimizes cross-WAN traffic and, consequently, reduces communication overhead.

### Communication Optimization Techniques

But it's far from enough, given the often limited and varied network conditions in WANs, distributed training across data centers can potentially create communication bottlenecks. To mitigate these issues, GeoMX employs a variety of optimization techniques. These include gradient sparsification, low-precision quantization (e.g., fp16), mixed-precision quantization, advanced transmission protocols, synchronization algorithms, flow scheduling, priority scheduling, load balancing, among others (e.g., overlay scheduling, currently in development). These techniques comprehensively tackle communication issues, further enhancing the efficiency and robustness of distributed machine learning training in GeoMX.

Next, let's delve into each optimization technique in brief:

1. **Bidirectional Gradient Sparsification (Bi-Sparse)<sup>1</sup>:** Traditional approaches such as [Deep Gradient Compression](https://arxiv.org/pdf/1712.01887.pdf) sparsify the pushed gradient tensors. For further compression, we also sparsify the pulled (aggregated) gradient tensors rather than pulling full parameters. This technique is enabled between the global parameter server and the intra-domain parameter servers of different data centers. (Refer to [this paper](https://www.zte.com.cn/content/dam/zte-site/res-www-zte-com-cn/mediares/magazine/publication/com_cn/article/202005/cn202005004.pdf) for more details.)
2. **Low-Precision Quantization (FP16):** GeoMX also supports quantifying model data at lower precision for transmission, such as in FP16 format. In this scheme, GeoMX computes the model using FP32, but during transmission, it converts the model data tensor into FP16. Once the pulling data is received, GeoMX reverts it back into FP32 and continues model computing. This effectively halves the data traffic volume over both LANs and WANs.
3. **Mixed-Precision Quantization (MPQ):** The technology of MPQ leverages both Bi-Sparse and FP16. In this scheme, tiny tensors are quantified into FP16 format for transmission, while large tensors persist in the FP32 format. Moreover, these large sensors will undergo a sparsification process before transmission. This precaution is taken to minimize the loss of crucial information and avoid significant degradation to model performance.
4. **Differential Gradient Transmission (DGT)<sup>2</sup>:** This advanced transmission protocol is optimized for distributed machine learning tasks. Leveraging the tolerance of gradient descent algorithms towards partial parameter loss, this protocol transfers gradients across multiple channels, each with distinct levels of reliability and priority, contingent on their respective contributions to model convergence. Through these prioritized channels, critical gradients receive precedence in transmission, while other non-important gradients are transmitted with lower priority and reliability. This helps to reduce tail latency and thus reduce the end-to-end transmission delay of parameter synchronization. (Refer to [this paper](https://drive.google.com/file/d/1IbmpFybX_qXZM2g_8BrcD9IF080qci94/view) for more details and [this repo](https://github.com/zhouhuaman/dgt) for individual use.)
5. **TSEngine<sup>3</sup>**: To solve the communication in-cast issue typically associated with centralized parameter servers, GeoMX incorporates TSEngine, an adaptive communication scheduler designed for efficient communication overlay in WANs. TSEngine dynamically optimizes the topology overlay and communication logic among the training nodes in response to real-time network conditions. This adaptive scheduler shows significant advantages over existing communication patterns in terms of system efficiency, communication, as well as scalability. (Refer to [this paper](https://drive.google.com/file/d/1ELfApVoCA8WCdOe3iBe-VreLJCSD7r8r/view) for more details and [this repo](https://github.com/zhouhuaman/TSEngine) for individual use.)
6. **Priority-based Parameter Propagation (P3):** In conventional implementations, the gradient synchronization at round $r$ does not overlap with the forward propagation at round $r+1$, because the forward propagation relies on the completion of gradient synchronization. To improve system efficiency, GeoMX integrates the P3 scheduler, which prioritizes the transmission of shallow-layer gradients. This setup enables overlapping between forward propagation and gradient synchronization, allowing earlier execution of forward propagation for the next round, thereby accelerating distributed training. (See [this paper](https://arxiv.org/pdf/1905.03960.pdf) for more details and [this repo](https://github.com/anandj91/p3) for individual use.)
7. **Multi-Server Load Balancing (MultiGPS):** GeoMX supports a balanced distribution of workload, including traffic, storage, and computation, across multiple global parameter servers. By preventing any single server from becoming a bottleneck, MultiGPS significantly enhances efficiency, scalability, and overall performance of our GeoMX system.

### Synchronization Algorithms

GeoMX supports two fundamental synchronization algorithms: a fully-synchronous algorithm and a mixed-synchronous algorithm.
1. **Fully-Synchronous Algorithm (FSA):** In this synchronous algorithm, training nodes synchronize their model data (can be parameters or gradients) each round, and both parameter server systems within and between data centers run in a synchronous parallel mode. This means all training nodes are synchronized to ensure a consistent model. (Default)

> FSA is highly effective in maintaining model accuracy and consistency, but at the expense of training speed, as it necessitates waiting for all computations and communications to complete at every iteration.

2. **Mixed-Synchronous Algorithm (MixedSync):** This algorithm is an asynchronous version of FSA, where the difference is that the parameter server system between data centers runs in an asynchronous parallel mode. This setup is particularly suitable for scenarios where intra-data center training nodes display high homogeneity, yet there is significant resource heterogeneity between different data centers. This asynchronous method resolves the problem of straggling data centers, thereby accelerating distributed training across WANs.

> To alleviate the issue of stale gradients in asynchronous parallel operations, the global parameter server can be configured to use the [DCASGD](http://proceedings.mlr.press/v70/zheng17b/zheng17b.pdf) optimizer. This adjustment aids in improving training convergence while preserving model accuracy.

Building upon the two aforementioned fundamental algorithms, GeoMX also offers two advanced synchronization algorithms. These two advanced algorithms are specifically designed to address the challenges of bandwidth scarcity and resource heterogeneity in WANs.

3. **Hierarchical Frequency Aggregation (HFA):** Inspired by [this paper](https://ieeexplore.ieee.org/abstract/document/9148862), our HFA algorithm first performs $K_1$ steps of local updates at the training nodes, followed by $K_2$ steps of synchronizations at the local parameter server. Finally, a global synchronization is performed at the global parameter server. This approach effectively reduces the frequency of model synchronization across data centers, thereby boosting distributed training.

4. **ESync<sup>4</sup>:** Applying asynchronous algorithms to strongly heterogeneous clusters can lead to severe stale gradient issues. To address this, we can adopt an optimized algorithm known as ESync. ESync is a synchronous parallel algorithm designed to save stragglers under conditions of strong resource heterogeneity. It introduces a state server to orchestrate the local iteration steps of each training node, in order to balance their reach-server time (including computational and transmission time). (To be integrated, refer to [this paper](https://drive.google.com/file/d/1bvK0EeO5vjkXveU_ccBp4Uxl-qmbmcfn/view) for more details and [this repo](https://github.com/Lizonghang/ESync) for individual use.)

## Quick Start
This guide will help you get started with GeoMX in just two steps.

> **Prerequisites:** We provide a pre-built Docker image for a quick trial of GeoMX. For this, Docker and Nvidia-Docker need to be installed following: [Docker Guide](https://docs.docker.com/engine/install/ubuntu/) and [Nvidia-Docker Guide](https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/latest/install-guide.html#install-guide).

* Step 1: Pull the Docker image and run a container.

```commandline
# To run on CPUs, use:
sudo docker run -it --rm --name geomx lizonghango00o1/geomx:cpu-only bash

# To run on GPUs with CUDA 8.0, use:
sudo docker run -it --rm --name geomx --gpus all lizonghango00o1/geomx:cu80 bash

# To run on GPUs with CUDA 10.1, use:
sudo docker run -it --rm --name geomx --gpus all lizonghango00o1/geomx:cu101 bash
```


* Step 2: Use the scripts in the `scripts` folder to launch demo tasks. For example:

```commandline
# To run on CPUs, use:
cd GeoMX/scripts/cpu && bash run_vanilla_hips.sh

# To run on GPUs, use:
cd GeoMX/scripts/gpu && bash run_vanilla_hips.sh
```

> If you are using the images with tags `cu80` and `cu101`, the first-time initialization of GeoMX may take a few minutes. However, subsequent runs should proceed without delay. This issue is common and can occur with other frameworks like PyTorch and MXNET as well.

For more detailed instructions, please refer to our [GeoMX Docs](https://geomx.readthedocs.io/en/latest/) (中文文档请看[这里](https://geomx.readthedocs.io/zh_CN/latest/)).

## Contributors
* **Li, Zonghang**<sup>1</sup> - *Initial work with equal contribution* - [Homepage](https://github.com/Lizonghang)
* **Zhang, Zhaofeng**<sup>1</sup> - *Initial work with equal contribution* - [Homepage](https://github.com/vcfgv)
* **Zhou, Huaman**<sup>1</sup> - *Initial work with equal contribution* - [Homepage](https://github.com/zhouhuaman)
* **Cai, Weibo**<sup>1</sup> - *Initial work with equal contribution* - [Homepage](https://github.com/CaiWeibo)
* **Yu, Hongfang**<sup>1</sup> - *Project instructor* - [Homepage](https://scholar.google.com/citations?user=GmEdMqwAAAAJ&hl=en&oi=ao)
* Other contributors: Cai, Qingqing<sup>1</sup>; Wang, Jigang<sup>1</sup>; Zhang, Zhihao<sup>1</sup>; Shao, Junming<sup>2</sup>; Xu, Zenglin<sup>3</sup>; AND YOU.

The contributors are from the following institutions:

1. **University of Electronic Science and Technology of China** - Intelligent Network and Application Research Centre (INET-RC)
2. **University of Electronic Science and Technology of China** - Data Mining Lab
3. **Harbin Institute of Technology, Shenzhen** - Statistical Machine Intelligence and Learning Lab (SMILE Lab)

## Cite Us
If this repo is helpful to you, please kindly cite us:

```bibtex
@misc{li2020geomx,
  author = {Li, Zonghang and Zhang, Zhaofeng and Zhou, Huaman and Cai, Weibo and Yu, Hongfang},
  title = {GeoMX: A fast and unified system for distributed machine learning over geo-distributed data centers.},
  year = {2020},
  howpublished = {GitHub},
  url = {https://github.com/INET-RC/GeoMX}
}
```

## References
1. Li, Zonghang, Hongfang Yu, and Yi Wang. "[Geo-distributed machine learning: Framework and technology exceeding lan speed.](https://www.zte.com.cn/content/dam/zte-site/res-www-zte-com-cn/mediares/magazine/publication/com_cn/article/202005/cn202005004.pdf)" _ZTE Technology Journal_ 26, no. 5 (2020): 16-22.
2. Zhou, Huaman, Zonghang Li, Qingqing Cai, Hongfang Yu, Shouxi Luo, Long Luo, and Gang Sun. "[DGT: A contribution-aware differential gradient transmission mechanism for distributed machine learning.](https://drive.google.com/file/d/1IbmpFybX_qXZM2g_8BrcD9IF080qci94/view)" _Future Generation Computer Systems_ 121 (2021): 35-47.
3. Zhou, Huaman, Weibo Cai, Zonghang Li, Hongfang Yu, Ling Liu, Long Luo, and Gang Sun. "[TSEngine: Enable efficient communication overlay in distributed machine learning in wans.](https://drive.google.com/file/d/1ELfApVoCA8WCdOe3iBe-VreLJCSD7r8r/view)" _IEEE Transactions on Network and Service Management_ 18, no. 4 (2021): 4846-4859.
4. Li, Zonghang, Huaman Zhou, Tianyao Zhou, Hongfang Yu, Zenglin Xu, and Gang Sun. "[ESync: Accelerating intra-domain federated learning in heterogeneous data centers.](https://drive.google.com/file/d/1bvK0EeO5vjkXveU_ccBp4Uxl-qmbmcfn/view)" _IEEE Transactions on Services Computing_ 15, no. 4 (2020): 2261-2274.

For more details on the GeoMX system, please refer to our book:

<p align="center">
  <img src="docs/source/_static/geomx-book.png" alt="Cover for GeoMX Book" width="400">
</p>

**虞红芳, 李宗航, 孙罡, 罗龙. 《跨数据中心机器学习：赋能多云智能数算融合》. 电子工业出版社, 人工智能前沿理论与技术应用丛书, (2023).**
