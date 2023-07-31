# Deployment Guide
This guide will walk you through the process of installing and deploying GeoMX. It also explains the meaning of the environment variables used in the accompanying shell scripts.

## Installation
### Use Pre-built Docker Image
To simplify installation, we strongly recommend using our pre-built Docker images, which are available on [DockerHub](https://hub.docker.com/repository/docker/lizonghango00o1/geomx/general):

```commandline
# If run on CPUs
sudo docker run -it --rm --name geomx-cpu lizonghango00o1/geomx:cpu-only bash

# If run on GPUs with CUDA 8.0
sudo docker run -it --rm --name geomx-gpu lizonghango00o1/geomx:cu80 bash

# If run on GPUs with CUDA 10.1
sudo docker run -it --rm --name geomx-gpu lizonghango00o1/geomx:cu101 bash
```

These commands will automatically pull the required Docker image from DockerHub and initiate a Docker container. You can directly interact with GeoMX within this container.

> For more in-depth information on using Docker / Nvidia-Docker, please refer to the [Docker Docs](https://docs.docker.com/get-started/) and the [NVIDIA Container Toolkit](https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/latest/index.html).

Then we can use the script files provided in the `scripts` folder to execute demo tasks. For example:

```commandline
# If run on CPUs
cd GeoMX/scripts/cpu && bash run_vanilla_hips.sh

# If run on GPUs
cd GeoMX/scripts/gpu && bash run_vanilla_hips.sh
```

> The first-time initialization of GeoMX on GPUs may take a few minutes. However, subsequent runs should proceed without delay. This issue is common and can occur with other frameworks like PyTorch and MXNET as well.

### Build from Dockerfile
Alternatively, we can also generate a customized Docker image using a Dockerfile. Some templates can be found in the `docker` folder.

To create a CPU-based Docker image, run:

```commandline
cd docker && sudo docker build -f build_on_cpu.dockerfile -t geomx:cpu-only .
```

And for a GPU-based Docker image, run:

```commandline
cd docker && sudo docker build -f build_on_gpu.dockerfile -t geomx:cu101 .
```

> This step may fail due to network issues. If this happens, retry or opt to compile GeoMX inside the Docker container.

### Build from Source Code
Though it is feasible to perform a direct installation on the host machine, we generally advise against this approach. This is due to potential issues that might arise from environmental incompatibilities. For a detailed walkthrough of the installation process on a host machine, please consult this [README](https://github.com/INET-RC/GeoMX/blob/main/README.md) file.

## Deployment
Now, let's delve into the deployment of our GeoMX system. We will describe two methods: pseudo-distributed deployment, and multi-host deployment. We will also introduce a visual virtualized cluster management platform named [Klonet](https://caod.oriprobe.com/articles/62233507/Klonet__a_network_emulation_platform_for_the_techn.htm), and explain how to use it to deploy our GeoMX system.

### Pseudo-distributed Deployment
The pseudo-distributed deployment is designed for quick trial and debugging purposes. In this setup, all nodes are launched within a single Docker container and their IP addresses are all set to `127.0.0.1`. This removes the need for additional network configuration. While this method is handy for getting a quick understanding of how the system operates, it is not meant for deployment in a production environment.

A basic shell script for pseudo-distributed deployment can be found [here](https://github.com/INET-RC/GeoMX/blob/main/scripts/cpu/run_vanilla_hips.sh). In this script, we launched a total of 12 nodes, each command corresponds to running a different node, with roles specified by `DMLC_ROLE` and `DMLC_ROLE_GLOBAL`.

#### Launch nodes in the central party 
The central party consists of 4 nodes: a global scheduler, a local scheduler, a global server, and a master worker.

The global scheduler is used to manage the global server and local servers (in other parties). Use the following commands to launch it:

```shell
DMLC_ROLE_GLOBAL=global_scheduler \
DMLC_PS_GLOBAL_ROOT_URI=127.0.0.1 \
DMLC_PS_GLOBAL_ROOT_PORT=9092 \
DMLC_NUM_GLOBAL_SERVER=1 \
DMLC_NUM_GLOBAL_WORKER=2 \
PS_VERBOSE=1 \
DMLC_INTERFACE=eth0 \
nohup python -c "import mxnet" > /dev/null &
```

These environment variables are defined as follows:
* `DMLC_ROLE_GLOBAL`: The role of the current process. In this case, it is a `global_scheduler` node. It could also be set to `global_server`.
* `DMLC_PS_GLOBAL_ROOT_URI`: The IP address of the global scheduler. In this case, it is set to `127.0.0.1`, meaning the process is running on the local machine.
* `DMLC_PS_GLOBAL_ROOT_PORT`: The port that the global scheduler binds to. In this case, the port is set to 9092.
* `DMLC_NUM_GLOBAL_SERVER`: The number of global servers. In this case, it is set to 1, meaning there is only one global server.
* `DMLC_NUM_GLOBAL_WORKER`: The number of local servers, i.e., the number of participating data centers. Here, it is set to 2, representing 2 participating data centers (Party A and Party B).
* `PS_VERBOSE`: The level of detail in the logs. Setting it to 0 disables log outputs, 1 outputs necessary log information, and 2 outputs log details.
* `DMLC_INTERFACE`: This specifies the network interface used for inter-process communication. In this case, it is set to `eth0`. This should be replaced with the actual network interface name used by your system or container.

Then, we launch a local scheduler, used to manage the global server and the master worker.

```shell
DMLC_ROLE=scheduler \
DMLC_PS_ROOT_URI=127.0.0.1 \
DMLC_PS_ROOT_PORT=9093 \
DMLC_NUM_SERVER=1 \
DMLC_NUM_WORKER=1 \
PS_VERBOSE=1 \
DMLC_INTERFACE=eth0 \
nohup python -c "import mxnet" > /dev/null &
```

Some new environment variables introduced here control intra-party behaviors:
* `DMLC_ROLE`: The role of the current process. In this case, it is a `scheduler` node. It could also be set to `server` and `worker`.
* `DMLC_PS_ROOT_URI`: The IP address of the local scheduler. Here, it is set to `127.0.0.1`, meaning the local scheduler runs on the local machine.
* `DMLC_PS_ROOT_PORT`: The port that the local scheduler binds to. It should differ from other schedulers (and the global scheduler) if they're launched on the same machine. Here, the port number is set to 9093.
* `DMLC_NUM_SERVER`: In the central party, this indicates the number of global server nodes. Here, it is set to 1.
* `DMLC_NUM_WORKER`: In the central party, this indicates the number of worker nodes (and the master worker). Here, we have only one master worker, so this value is set to 1.

To launch the global server, run the following commands:

```shell
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
```

In this case, `DMLC_PS_GLOBAL_ROOT_URI` and `DMLC_PS_GLOBAL_ROOT_PORT` refer to the setup of the global scheduler, while `DMLC_PS_ROOT_URI` and `DMLC_PS_ROOT_PORT` refer to the setup of the local scheduler. Other environment variables are as follows:
* `DMLC_ENABLE_CENTRAL_WORKER`: This option enables or disables the central party to participate in model training. If set to 0, the central party only provides a master worker to initialize the global server. If set to 1, the central party can provide a worker cluster to participate in model training, with the master worker attached to a worker node.
* `DMLC_NUM_ALL_WORKER`: The total number of worker nodes worldwide participating in model training. Here, with 2 workers in Party A and 2 workers in Party B, it's set to 4. Note that although the master worker is also a worker node, in this case it does not participate in model training, so it is not counted.

Lastly, we launch the master worker.

```shell
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
```

The master worker sets `DMLC_ROLE_MASTER_WORKER=1` to announce itself as a master worker node. It establishes a socket connection with the local scheduler, thus `DMLC_PS_ROOT_URI=127.0.0.1` and `DMLC_PS_ROOT_PORT=9093` are set to ensure that the master worker can find the local scheduler.

#### Launch nodes in other parties
Next, we will be launching a scheduler, a parameter server, and two workers in the other parties. Let's take one of them as an example.

First, we'll start with launching the local scheduler:

```shell
DMLC_ROLE=scheduler \
DMLC_PS_ROOT_URI=127.0.0.1 \
DMLC_PS_ROOT_PORT=9094 \
DMLC_NUM_SERVER=1 \
DMLC_NUM_WORKER=2 \
PS_VERBOSE=1 \
DMLC_INTERFACE=eth0 \
nohup python -c "import mxnet" > /dev/null &
```

This setup is similar to that of the local scheduler in the central party, but in this context, `DMLC_NUM_SERVER` specifies the number of local parameter servers within the current party, which typically sets to 1. Furthermore, `DMLC_NUM_WORKER` specifies the number of worker nodes within the current party. As we're planning to launch two worker nodes in this party, here we set this value to 2.

Next, we launch the local parameter server:

```shell
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
```

As we mentioned above, a parameter server is required to establish socket connections with both the local and global schedulers. Thus, it needs to know the IP and port address of both the local scheduler and the global scheduler. 

Finally, we'll launch two worker nodes:

```shell
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
```

The worker nodes are launched in a similar manner as before, but they connect to their own local scheduler within their party. 

The training data is divided among worker nodes. Each worker gets a slice of data to process, which is specified by the `--data-slice-idx` option. For example, the first worker gets the 0th slice of the data, and the second worker gets the 1st slice of the data.

### Multi-host Deployment
To deploy GeoMX on real-world geographical data centers, we need to use the multi-host mode for cluster deployment.

### Klonet-based Deployment
Coming soon.

## Environment Variables explained

`DMLC_ROLE`: Specifies the local role of current process. It can be `server`,  `worker` , or `scheduler`

`DMLC_ROLE_GLOBAL`: Specifies the global role of current process. It can be `global_server`, `worker`, or `global_scheduler`

`DMLC_ROLE_MASTER_WORKER`: Specifies if the role of current process is `master_worker`

`DMLC_PS_ROOT_URI`: Specifies the IP of the local scheduler

`DMLC_PS_ROOT_PORT`: Specifies the port that the local scheduler listens to

`DMLC_PS_GLOBAL_ROOT_URI`: Specifies the IP of the global scheduler

`DMLC_PS_GLOBAL_ROOT_PORT`: Specifies the port that the global scheduler listens to

`DMLC_NUM_SERVER`: Specifies how many server nodes are connected to the local/global scheduler

`DMLC_NUM_WORKER`: Specifies how many worker nodes are connected to the local/global scheduler

`DMLC_NUM_ALL_WORKER`: Specifies how many worker nodes are in the cluster

`DMLC_ENABLE_CENTRAL_WORKER`: Specifies whether to enable `master worker` to join training 

`PS_VERBOSE` Logging communication Value type: 1 or 2 Default value: (empty)

- `PS_VERBOSE=1` logs connection information like the IPs and ports of all nodes
- `PS_VERBOSE=2` logs all data communication information

## Manually start distributed jobs

Assume that we have 3 host machines:

- A: IP 10.1.1.34
- B: IP 10.1.1.29
- C: IP 10.1.1.33

### Configure Network

Containers are separated into three networks and they should be able to communicate with others.

- A: bridge docker0, 172.17.34.0/24
- B: bridge docker0, 172.17.29.0/24
- C: bridge docker0, 172.17.33.0/24

To separate the network, add the following configuration to `/etc/docker/daemon.json` on host A, B and C and restart docker:

```shell
// on host A (IP 10.1.1.34), set 172.17.29.1/24 on host B
// and set 172.17.33.1/24 on host C.
$ sudo vim /etc/docker/daemon.json
(vim) {
(vim)   "bip": "172.17.34.1/24"  
(vim) }
$ sudo service docker restart
$ sudo service docker status
```

Then configure the route table on host A, B and C to forward IP packets:

```shell
// on host A (IP 10.1.1.34)
$ sudo route add -net 172.17.29.0 netmask 255.255.255.0 gw 10.1.1.29
$ sudo route add -net 172.17.33.0 netmask 255.255.255.0 gw 10.1.1.33
// on host B (IP 10.1.1.29)
$ sudo route add -net 172.17.33.0 netmask 255.255.255.0 gw 10.1.1.33
$ sudo route add -net 172.17.34.0 netmask 255.255.255.0 gw 10.1.1.34
// on host C (IP 10.1.1.33)
$ sudo route add -net 172.17.29.0 netmask 255.255.255.0 gw 10.1.1.29
$ sudo route add -net 172.17.34.0 netmask 255.255.255.0 gw 10.1.1.34
```

Finally, configure the iptables on host A, B and C to support SNAT:

```shell
$ sudo iptables -P INPUT ACCEPT
$ sudo iptables -P FORWARD ACCEPT
$ sudo iptables -t nat -F POSTROUTING
// on host A (IP 10.1.1.34)
$ sudo iptables -t nat -A POSTROUTING -s 172.17.34.0/24 ! -d 172.17.0.0/16 -j MASQUERADE
// on host B (IP 10.1.1.29)
$ sudo iptables -t nat -A POSTROUTING -s 172.17.29.0/24 ! -d 172.17.0.0/16 -j MASQUERADE
// on host C (IP 10.1.1.33)
$ sudo iptables -t nat -A POSTROUTING -s 172.17.33.0/24 ! -d 172.17.0.0/16 -j MASQUERADE
```

> Besides, [weaveworks/weave](https://github.com/weaveworks/weave) is also practical and recommended.

### Run Containers

>  NOTE: Always start scheduler and global scheduler first to ensure their IP addresses are 172.17.XX.2 and 172.17.XX.3 respectively, otherwise the process would fail to bind the ports.

Run a container, set environment variables, map necessary ports outside, start ssh and configure public keys (read from `ssh_keys`), and compile automatically.
