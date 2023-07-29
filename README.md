# GeoMX: Fast and unified distributed system for training ML algorithms over geographical data centers

[![GitHub stars](https://img.shields.io/github/stars/INET-RC/GeoMX)](https://img.shields.io/github/stars/INET-RC/GeoMX) 
[![GitHub license](https://img.shields.io/github/license/INET-RC/GeoMX)](https://github.com/INET-RC/GeoMX/blob/main/LICENSE) 
[![Docker Stars](https://img.shields.io/docker/stars/lizonghango00o1/geomx.svg)](https://hub.docker.com/r/lizonghango00o1/geomx)
[![Docker Pulls](https://img.shields.io/docker/pulls/lizonghango00o1/geomx.svg)](https://hub.docker.com/r/lizonghango00o1/geomx)

## Table of Contents
- [Introduction](#introduction)
- [Installation Guidance](#installation-guidance)
  - [1. Build from Source Code](#1-build-from-source-code)
  - [2. Build from Dockerfile](#2-build-from-dockerfile)
  - [3. Download Pre-built Docker Image from DockerHub](#3-download-pre-built-docker-image-from-dockerhub)
- [How to Use GeoMX?](#how-to-use-geomx)
- [Contributors](#contributors)
- [Cite Us](#cite-us)
- [References](#references)

## Introduction
GeoMX is an optimized distributed machine learning system that operates across multiple geographically dispersed data centers. Built upon the [MXNET](https://github.com/apache/mxnet) framework, GeoMX integrates several sophisticated optimization techniques to enhance its training efficiency. These strategic enhancements result in a significant performance boost compared to the original MXNET system, offering 20x acceleration under identical network bandwidth conditions. This superior efficiency propels GeoMX to the forefront of training systems used for geographically dispersed data centers, showcasing satisfying performance and effectiveness.

GeoMX employs the [Hierarchical Parameter Server (HiPS)](https://www.zte.com.cn/content/dam/zte-site/res-www-zte-com-cn/mediares/magazine/publication/com_cn/article/202005/cn202005004.pdf)<sup>1</sup> framework as its fundamental training architecture, designed to segregate the network environment within and beyond the data center. This unique architecture includes an intra-domain parameter server system within each data center while concurrently establishing an inter-parameter server connection between different data centers. In this configuration, model data traffic undergoes two stages of aggregation: an initial local aggregation within each data center, followed by a global aggregation at the central data center. This approach effectively minimizes cross-WAN traffic and, consequently, reduces communication overhead.

### Communication Optimization Techniques

But it's far from enough, given the often limited and varied network conditions in WANs, distributed training across data centers can potentially create communication bottlenecks. To mitigate these issues, GeoMX employs a variety of optimization techniques. These include gradient sparsification, mixed-precision quantization, advanced transmission protocols, synchronization algorithms, flow scheduling, and priority scheduling, among others (e.g., overlay scheduling, currently in development). These techniques comprehensively tackle communication issues, further enhancing the efficiency and robustness of distributed machine learning training in GeoMX.

Next, let's delve into each optimization technique in brief:

1. **Bidirectional Gradient Sparsification (Bi-Sparse)<sup>1</sup>:** Traditional approaches such as [Deep Gradient Compression](https://arxiv.org/pdf/1712.01887.pdf) sparsify the pushed gradient tensors. For further compression, we also sparsify the pulled (aggregated) gradient tensors rather than pulling full parameters. This technique is enabled between the global parameter server and the intra-domain parameter servers of different data centers. (Refer to [this paper](https://www.zte.com.cn/content/dam/zte-site/res-www-zte-com-cn/mediares/magazine/publication/com_cn/article/202005/cn202005004.pdf) for more details.)
2. **Mixed-Precision Quantization:** This technique quantifies the parameter and gradient tensors set for transmission into FP16 format, which effectively halves the data traffic volume over both LANs and WANs. However, if bidirectional gradient sparsification is enabled, the communication between the intra-domain parameter servers and the global parameter server remains in FP32 format. This precaution is taken to minimize the loss of crucial information and avoid significant degradation to model performance.
3. **Differential Gradient Transmission (DGT)<sup>2</sup>:** This advanced transmission protocol is optimized for distributed machine learning tasks. Leveraging the tolerance of gradient descent algorithms towards partial parameter loss, this protocol transfers gradients across multiple channels, each with distinct levels of reliability and priority, contingent on their respective contributions to model convergence. Through these prioritized channels, critical gradients receive precedence in transmission, while other non-important gradients are transmitted with lower priority and reliability. This helps to reduce tail latency and thus reduce the end-to-end transmission delay of parameter synchronization. (Refer to [this paper](https://drive.google.com/file/d/1IbmpFybX_qXZM2g_8BrcD9IF080qci94/view) for more details and [this repo](https://github.com/zhouhuaman/dgt) for individual use.)
4. **TSEngine<sup>3</sup>**: To solve the communication in-cast issue typically associated with centralized parameter servers, GeoMX incorporates TSEngine, an adaptive communication scheduler designed for efficient communication overlay in WANs. TSEngine dynamically optimizes the topology overlay and communication logic among the training nodes in response to real-time network conditions. This adaptive scheduler shows significant advantages over existing communication patterns in terms of system efficiency, communication, as well as scalability. (Refer to [this paper](https://drive.google.com/file/d/1ELfApVoCA8WCdOe3iBe-VreLJCSD7r8r/view) for more details and [this repo](https://github.com/zhouhuaman/TSEngine) for individual use.)
5. **Priority-based Parameter Propagation (P3):** In conventional implementations, the gradient synchronization at round $r$ does not overlap with the forward propagation at round $r+1$, because the forward propagation relies on the completion of gradient synchronization. To improve system efficiency, GeoMX integrates the P3 scheduler, which prioritizes the transmission of shallow-layer gradients. This setup enables overlapping between forward propagation and gradient synchronization, allowing earlier execution of forward propagation for the next round, thereby accelerating distributed training. (See [this paper](https://arxiv.org/pdf/1905.03960.pdf) for more details and [this repo](https://github.com/anandj91/p3) for individual use.)

### Synchronization Algorithms

GeoMX supports two fundamental synchronization algorithms: a fully-synchronous algorithm and a mixed-synchronous algorithm.
1. **Fully-Synchronous Algorithm (FSA):** In this synchronous algorithm, training nodes synchronize their model data (can be parameters or gradients) each round, and both parameter server systems within and between data centers run in a synchronous parallel mode. This means all training nodes are synchronized to ensure a consistent model.

> FSA is highly effective in maintaining model accuracy and consistency, but at the expense of training speed, as it necessitates waiting for all computations and communications to complete at every iteration.

2. **Mixed-Synchronous Algorithm (MixedSync):** This algorithm is an asynchronous version of FSA, where the difference is that the parameter server system between data centers runs in an asynchronous parallel mode. This setup is particularly suitable for scenarios where intra-data center training nodes display high homogeneity, yet there is significant resource heterogeneity between different data centers. This asynchronous method resolves the problem of straggling data centers, thereby accelerating distributed training across WANs.

> To alleviate the issue of stale gradients in asynchronous parallel operations, the global parameter server can be configured to use the [DCASGD](http://proceedings.mlr.press/v70/zheng17b/zheng17b.pdf) optimizer. This adjustment aids in improving training convergence while preserving model accuracy.

Building upon the two aforementioned fundamental algorithms, GeoMX also offers two advanced synchronization algorithms. These two advanced algorithms are specifically designed to address the challenges of bandwidth scarcity and resource heterogeneity in WANs.

3. **Hierarchical Frequency Aggregation (HFA):** Inspired by [this paper](https://ieeexplore.ieee.org/abstract/document/9148862), our HFA algorithm first performs $K_1$ steps of local updates at the training nodes, followed by $K_2$ steps of synchronizations at the local parameter server. Finally, a global synchronization is performed at the global parameter server. This approach effectively reduces the frequency of model synchronization across data centers, thereby boosting distributed training.

4. **ESync<sup>4</sup>:** Applying asynchronous algorithms to strongly heterogeneous clusters can lead to severe stale gradient issues. To address this, we can adopt an optimized algorithm known as ESync. ESync is a synchronous parallel algorithm designed to save stragglers under conditions of strong resource heterogeneity. It introduces a state server to orchestrate the local iteration steps of each training node, in order to balance their reach-server time (including computational and transmission time). (To be integrated, refer to [this paper](https://drive.google.com/file/d/1bvK0EeO5vjkXveU_ccBp4Uxl-qmbmcfn/view) for more details and [this repo](https://github.com/Lizonghang/ESync) for individual use.)

## Installation Guidance

GeoMX provides three methods for installation:
1. Build it from the source code.
2. Build a Docker image using the provided Dockerfile.
3. Download the pre-built Docker image directly from DockerHub.

### 1. Build from Source Code 

To install GeoMX from the source code, follow the steps below:
* Step 1: Download the source code. Git clone the GeoMX repository using the following command:

```shell
git clone https://github.com/INET-RC/GeoMX.git
```

* Step 2: Install third-party dependencies. Ensure you have the necessary dependencies installed. Use the following command to do so:

```shell
sudo apt update
sudo apt install -y build-essential cmake libopencv-dev libopenblas-dev libsnappy-dev autogen autoconf automake libtool
pip install --upgrade pip
pip install numpy==1.17.3 pandas opencv-python
```

* Step 3: Copy the configuration file. Based on your preference for running GeoMX on CPUs or GPUs, copy the corresponding configuration file to `GeoMX/config.mk`. For CPU, use:

```shell
cp make/cpu_config.mk ./config.mk
```

And for GPU, copy `make/gpu_config.mk` instead. Refer to [cpu_config.mk](https://github.com/INET-RC/GeoMX/blob/main/make/cpu_config.mk) and [gpu_config.mk](https://github.com/INET-RC/GeoMX/blob/main/make/gpu_config.mk) for detailed configuration instructions.

* Step 4: Build the source code. Use the following command to build the source code:

```shell
# Here all CPU cores are used to build GeoMX faster. You can decrease its value to avoid CPU overload.
make -j$(nproc)
```

> This step may fail due to network issues. If that happens, retry or wait to run the command again at a later time. Once the build is successful, you will see a new folder `lib` containing the library file `libmxnet.so`.

* Step 5: Bind GeoMX to Python, using the following command:

```shell
$ cd python && pip install -e .
```

> The `-e` flag is optional. It is equivalent to `--editable` and means that if you edit the source files, these changes will be reflected in the package installed.

### 2. Build from Dockerfile

For an effortless deployment and streamlined setup, we highly recommend running GeoMX within a Docker container. Please ensure [Docker](https://docs.docker.com/get-docker/) and [Nvidia-Docker](https://github.com/NVIDIA/nvidia-docker) are correctly installed on your machine before proceeding. Then use the Dockerfiles provided in the `docker` folder to build your Docker image.

For a CPU-based Docker image, run:

```shell
cd docker && sudo docker build -f build_on_cpu.dockerfile -t geomx:cpu-only .
```

And for a GPU-based Docker image, run:

```shell
cd docker && sudo docker build -f build_on_gpu.dockerfile -t geomx:cu101 .
```

> This step may also fail due to network issues. If this happens, retry or opt to compile GeoMX inside the Docker container.

### 3. Download Pre-built Docker Image from DockerHub
If you prefer not to build the Docker image yourself, you can download a [pre-built Docker image](https://hub.docker.com/repository/docker/lizonghango00o1/geomx/general) for GeoMX from DockerHub using the following command:

```shell
# If run on CPUs
sudo docker pull lizonghango00o1/geomx:cpu-only

# If run on GPUs with CUDA 8.0
sudo docker pull lizonghango00o1/geomx:cu80

# If run on GPUs with CUDA 10.1
sudo docker pull lizonghango00o1/geomx:cu101
```

> These Docker images include a demo dataset, so we can run demo scripts without much trouble. However, if we install GeoMX using other methods, please download the demo dataset manually.

## How to Use GeoMX?
If we are using a pre-built Docker image, run it as a container using the following command:

```shell
sudo docker run -it --rm --name geomx-cpu lizonghango00o1/geomx:cpu-only bash
```

Then we can use the script files provided in the `scripts` folder to execute demo tasks. For example:

```shell
cd GeoMX/scripts/cpu && bash run_vanilla_hips.sh
```

> We may encounter errors if the dataset file is missing. However, don't worry as some worker nodes will automatically download the dataset files and store them in the default location `/root/data`. If you face this issue, terminate the existing process using `killall python` and rerun the training script.

To run GeoMX on GPUs, use a pre-built Docker image, for example, `geomx:cu101`, as follows:

```shell
sudo docker run -it --rm --name geomx-gpu lizonghango00o1/geomx:cu101 bash
cd GeoMX/scripts/gpu && bash run_vanilla_hips.sh
```

> The first-time initialization of GeoMX on GPUs may take a few minutes. However, subsequent runs should proceed without delay. This issue is common and can occur with other frameworks like PyTorch and MXNET as well.

## Contributors
* **Li, Zonghang** - *Initial work with equal contribution* - [Homepage](https://github.com/Lizonghang)
* **Zhang, Zhaofeng** - *Initial work with equal contribution* - [Homepage](https://github.com/vcfgv)
* **Zhou, Huaman** - *Initial work with equal contribution* - [Homepage](https://github.com/zhouhuaman)
* **Cai, Weibo** - *Initial work with equal contribution* - [Homepage](https://github.com/CaiWeibo)
* **Yu, Hongfang** - *Project instructor* - [Homepage](https://scholar.google.com/citations?user=GmEdMqwAAAAJ&hl=en&oi=ao)
* Other contributors: Cai, Qingqing; Wang, Jigang; Zhang, Zhihao; Xu, Zenglin; Shao, Junming; AND YOU.

## Cite Us
If this repo is helpful to you, please kindly cite us:

```bibtex
@misc{li2020geomx,
  author = {Li, Zonghang and Zhang, Zhaofeng and Zhou, Huaman and Cai, Weibo and Yu, Hongfang},
  title = {GeoMX: Fast and unified distributed system for training machine learning algorithms over geographical data centers.},
  year = {2020},
  howpublished = {GitHub},
  url = {https://github.com/INET-RC/GeoMX}
}
```

## References
1. Li, Zonghang, Hongfang Yu, and Yi Wang. "[Geo-Distributed Machine Learning: Framework and Technology Exceeding LAN Speed.](https://www.zte.com.cn/content/dam/zte-site/res-www-zte-com-cn/mediares/magazine/publication/com_cn/article/202005/cn202005004.pdf)" _ZTE Technology Journal_ 26, no. 5 (2020): 16-22.
2. Zhou, Huaman, Zonghang Li, Qingqing Cai, Hongfang Yu, Shouxi Luo, Long Luo, and Gang Sun. "[DGT: A contribution-aware differential gradient transmission mechanism for distributed machine learning.](https://drive.google.com/file/d/1IbmpFybX_qXZM2g_8BrcD9IF080qci94/view)" _Future Generation Computer Systems_ 121 (2021): 35-47.
3. Zhou, Huaman, Weibo Cai, Zonghang Li, Hongfang Yu, Ling Liu, Long Luo, and Gang Sun. "[TSEngine: Enable Efficient Communication Overlay in Distributed Machine Learning in WANs.](https://drive.google.com/file/d/1ELfApVoCA8WCdOe3iBe-VreLJCSD7r8r/view)" _IEEE Transactions on Network and Service Management_ 18, no. 4 (2021): 4846-4859.
4. Li, Zonghang, Huaman Zhou, Tianyao Zhou, Hongfang Yu, Zenglin Xu, and Gang Sun. "Esync: Accelerating intra-domain federated learning in heterogeneous data centers." _IEEE Transactions on Services Computing_ 15, no. 4 (2020): 2261-2274.

For more details on the GeoMX system, please refer to our book:

<p align="center">
  <img src="geomx-book.png" alt="Cover for GeoMX Book" width="400">
</p>

**虞红芳, 李宗航, 孙罡, 罗龙. 《跨数据中心机器学习：赋能多云智能数算融合》. 电子工业出版社, 人工智能前沿理论与技术应用丛书, (2023).**
