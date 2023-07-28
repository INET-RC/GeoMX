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
sudo apt install -y build-essential cmake libopencv-dev libopenblas-dev libsnappy-dev autogen autoconf automake libtool
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

### Download Pre-built Docker Image from DockerHub
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

## Documentation

- [Deployment](./docs/Deployment.md): How to run GeoMX on multiple machines with Docker.
- [Configurations](./docs/Configurations.md): How to use GeoMX's communication-efficient strategies.
- [System Design](./docs/System%20Design.md): How and why GeoMX and its modifications make it better.

[Github Issues](https://github.com/INET-RC/GeoMX/issues) is welcomed.
