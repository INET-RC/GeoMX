## Introduction

GeoMX is a MXNet-based two-layer parameter server framework, aiming at integrating data knowledge that owned by multiple independent parties in a privacy-preservating way (i.e. no need to transfer raw data), by training a shared deep learning model collaboratively in a decentralized and distributed manner.

Unlike other distributed deep learning software framworks and the emerging Federated Learning technologies which are based on single-layer parameter server architecture, GeoMX applies two-layer architecture to reduce communication cost between parties. 

GeoMX allows parties to train the deep learning model on their own data and clusters in a distributed mannar locally. Parties only need to upload the locally aggregated model gradients (or model updates) to the central party to perform global aggregation, model updating and model synchronization.

To mitigate the communication bottleneck between the central party and participating parties, GeoMX implements multiple communication-efficient strategies, such as BSC, DGT, TSEngine and P3, boosting the model training.

**BSC**, fully named as *Bilateral Sparse Compression*, reduces the size of gradient tensors during Local Server's push and pull progress. **DGT**, fully named as *Differential Gradient Transmission*, transfers gradients in multi-channels with different  reliability and priority according to their contribution to model convergence. **TSEngine** dynamically schedules the communication logic over the parameter server and workers based on the active network perception. **P3** overlaps parameter synchronization with computation in order to improve the training performance.

Furthermore, GeoMX supports:

1. **4 communication algorithms**, including fully-synchronous algorithm, mix-synchronous algorithm, HierFAVG-styled synchronous algorithm and DC-ASGD asynchronous algorithm. 

2. (6 categories) **25 machine learning algorithms**, including 9 gradient descent algorithms, 7 ensemble learning algorithms, 3 support vector algorithms, 2 MapReduce algorithms, 2 online learning algorithms, 2 incremental learning algorithms.

## Installation

### Build from source

#### clone the GeoMX project

```shell
git clone https://github.com/INET-RC/GeoMX.git
cd GeoMX
```

#### Install a Math Library and OpenCV

```shell
# e.g. OpenBLAS
sudo apt-get install -y libopenblas-dev
sudo apt-get install -y libopencv-dev
```

#### Build core shared library

There is a configuration file for make, `make/config.mk` compilation options. 

If building on CPU and using OpenBLAS:

```makefile
USE_OPENCV=1
USE_BLAS=openblas
```

If building on GPU and you want OpenCV and OpenBLAS:

```makefile
USE_OPENCV=1
USE_BLAS=openblas
USE_CUDA=1
USE_CUDA_PATH=/usr/local/cuda
```

Visit [usage-example](https://mxnet.apache.org/versions/1.4.1/install/build_from_source.html#usage-examples) for other compilation options. You can edit it and then run `make -j$(nproc)`

Building from source creates a library called `libmxnet.so` in the `lib` folder in your project root.

You may also want to add the shared library to your `LD_LIBRARY_PATH`:

```shell
export LD_LIBRARY_PATH=$PWD/lib
```

#### Install Python bindings

Navigate to the root of the GeoMX folder then run the following:

```shell
$ cd python
$ pip install -e .
```

Note that the `-e` flag is optional. It is equivalent to `--editable` and means that if you edit the source files, these changes will be reflected in the package installed.

### Docker

It's highly recommended to run GeoMX in a Docker container. To build such a Docker image, use the provided `Dockerfile`.

```shell
docker build -t GeoMX -f Dockerfile .
```

#### GPU Backend

Please make sure `NVIDIA Container Toolkit` is installed. Here are some install for `Ubuntu` users:

```shell
distribution=$(. /etc/os-release;echo $ID$VERSION_ID) \
   && curl -s -L https://nvidia.github.io/nvidia-docker/gpgkey | sudo apt-key add - \
   && curl -s -L https://nvidia.github.io/nvidia-docker/$distribution/nvidia-docker.list | sudo tee /etc/apt/sources.list.d/nvidia-docker.list
```

restart the Docker daemon to complete the installation:

```shell
sudo systemctl restart docker
```

Then edit  `daemon.json` to set the default runtime:

```shell
$ vim /etc/docker/daemon.json
{
    "runtimes": {
        "nvidia": {
            "path": "nvidia-container-runtime",
            "runtimeArgs": []
        }
    }
}
```

Finally, restart the Docker daemon:

```shell
sudo systemctl restart docker
```

## Documentation

- [Deployment](./docs/Deployment.md): How to run GeoMX on multiple machines with Docker.
- [Configurations](./docs/Configurations.md): How to use GeoMX's communication-efficient strategies.
- [System Design](./docs/System%20Design.md): How and why GeoMX and its modifications make better.

## Communication

- [Github Issues](https://github.com/INET-RC/GeoMX/issues) is welcomed.

## Copyright and License

GeoMX is provided under the [Apache-2.0 license](https://github.com/INET-RC/GeoMX/blob/main/LICENSE).