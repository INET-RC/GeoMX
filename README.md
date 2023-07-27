## Introduction

GeoMX is a MXNet-based two-layer parameter server framework, aiming at integrating data knowledge that owned by multiple independent parties in a privacy-preserving way (i.e. no need to transfer raw data), by training a shared deep learning model collaboratively in a decentralized and distributed manner.

Unlike other distributed deep learning software framworks and the emerging Federated Learning technologies which are based on single-layer parameter server architecture, GeoMX applies two-layer architecture to reduce communication cost between parties. 

GeoMX allows parties to train the deep learning model on their own data and clusters in a distributed mannar locally. Parties only need to upload the locally aggregated model gradients (or model updates) to the central party to perform global aggregation, model updating and model synchronization.

To mitigate the communication bottleneck between the central party and participating parties, GeoMX implements multiple communication-efficient strategies, such as BSC, DGT, TSEngine and P3, boosting the model training.

**BSC**, fully named as *Bilateral Sparse Compression*, reduces the size of gradient tensors during Local Server's push and pull progress. **DGT**, a contribution-aware differential gradient transmission protocol, fully named as *Differential Gradient Transmission* , transfers gradients in multi-channels with different  reliability and priority according to their contribution to model convergence. **TSEngine**, an adaptive communication scheduler for efficient communication overlay of the parameter server system in DML-WANs, dynamically schedules the communication logic over the parameter server and workers based on the active network perception. **P3** overlaps parameter synchronization with computation in order to improve the training performance.

Furthermore, GeoMX supports:

1. **4 communication algorithms**, including fully-synchronous algorithm, mix-synchronous algorithm, HierFAVG-styled synchronous algorithm and DC-ASGD asynchronous algorithm. 

2. (6 categories) **25 machine learning algorithms**, including 9 gradient descent algorithms, 7 ensemble learning algorithms, 3 support vector algorithms, 2 MapReduce algorithms, 2 online learning algorithms, 2 incremental learning algorithms.

## Installation

You can install GeoMX by building it from the source code, or building a Docker image using the provided dockerfile, or downloading the pre-built Docker image directly from DockerHub.

### Build from Source Code

Download the source code of GeoMX from this repo:

```shell
git clone https://github.com/INET-RC/GeoMX.git
```

Install dependencies:

```shell
sudo apt install -y build-essential cmake libopencv-dev libopenblas-dev libsnappy-dev autogen autoconf automake libtool
```

Copy the configuration file to ``GeoMX/config.mk`` and edit its content as you wish. If you prefer to run GeoMX on CPUs, using:

```shell
cp make/cpu_config.mk ./config.mk
```

If you prefer to run GeoMX on GPUs, copy ``make/gpu_config.mk`` instead of ``make/cpu_config.mk``. Please refer to [cpu_config.mk](https://github.com/INET-RC/GeoMX/blob/main/make/cpu_config.mk) and [gpu_config.mk](https://github.com/INET-RC/GeoMX/blob/main/make/gpu_config.mk) for more detailed configurations.

Then, we make the source code using:

```shell
make -j$(nproc)
```

Please note that this could fail due to network problems. If that occurs, please try to make it again, or at a later time. 

Once it is done, we will see a folder named ``lib`` with a library file `libmxnet.so` in it. 

Finally, we bind GeoMX to Python, by running:

```shell
$ cd python && pip install -e .
```

Note that the `-e` flag is optional. It is equivalent to `--editable` and means that if you edit the source files, these changes will be reflected in the package installed.

### Build from Dockerfile

We highly recommend running GeoMX within a Docker container for effortless deployment and streamlined setup. To achieve that, please make sure you have Docker (and Nvidia-Docker) installed. Then, you can use the docker files provided in ``docker`` to build our required Docker image.

For example, here we use ``docker/build_on_cpu.dockerfile`` to build a Docker image that runs on CPUs.

```shell
cd docker && sudo docker build -f build_on_cpu.dockerfile -t geomx:cpu-only .
```

This will automatically install dependent third-party libraries, download the source code of GeoMX, compile and install GeoMX. However, it may fail due to possible network failure. If that happens, try a few more times, or compile GeoMX inside the Docker container.

### Download Pre-built Docker Image from DockerHub
Alternatively, if you prefer not to build the Docker image yourself, you can download a [pre-built Docker image for GeoMX from DockerHub](https://hub.docker.com/repository/docker/lizonghango00o1/geomx/general).

The command to pull the pre-built Docker image from DockerHub is as follows:
```shell
sudo docker pull lizonghango00o1/geomx:cpu-only
```

> Please note that a demo dataset is already included in [this Docker image](https://hub.docker.com/repository/docker/lizonghango00o1/geomx/general). If you use the other two ways to install GeoMX, please manually download the demo dataset.

## How to Use GeoMX?
If we were using a pre-built Docker image, run it as a container:

```shell
sudo docker run -it --rm --name geomx lizonghango00o1/geomx:cpu-only bash
```

The script files in the ``scripts`` folder are provided to help users quickly run GeoMX on a demo task. Here's a general guide on how to use GeoMX with the provided scripts:

```shell
cd GeoMX/scripts/cpu && bash run_vanilla_hips.sh
```

Errors can happen because the dataset file might be missing. However, it is not a problem since some worker nodes have already automatically downloaded the dataset files, and they are stored in the default location ``/root/data``. Just terminate the existing process by ``killall python`` and rerun the training script.

> Initializing GeoMX for the first time might take a few minutes if we were running on a GPU, but after that, everything should work fine. This problem occurs from time to time on both PyTorch and MXNET due to incompatible CUDA versions.

## Documentation

- [Deployment](./docs/Deployment.md): How to run GeoMX on multiple machines with Docker.
- [Configurations](./docs/Configurations.md): How to use GeoMX's communication-efficient strategies.
- [System Design](./docs/System%20Design.md): How and why GeoMX and its modifications make it better.

[Github Issues](https://github.com/INET-RC/GeoMX/issues) is welcomed.
