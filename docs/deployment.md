- [Deployment Guide](#deployment-guide)
  - [Installation](#installation)
    - [Use Pre-built Docker Image](#use-pre-built-docker-image)
    - [Build from Dockerfile](#build-from-dockerfile)
    - [Build from Source Code](#build-from-source-code)
  - [Deployment](#deployment)
    - [Pseudo-distributed Deployment](#pseudo-distributed-deployment)
    - [Multi-host Deployment](#multi-host-deployment)
    - [Klonet-based Deployment](#klonet-based-deployment)
  - [Summary of Environment Variables](#summary-of-environment-variables)

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

#### Launch Nodes in the Central Party 
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

#### Launch Nodes in Other Parties
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

In this tutorial, we use three host machines as an example, namely A, B, and C with their respective IP addresses as follows:

- Host A IP: 10.1.1.34
- Host B IP: 10.1.1.29
- Host C IP: 10.1.1.33

To simulate the interconnection among three data centers using these three host machines, we manually segregate the Docker containers on these machines into three distinct network segments.

- Host A: bridge docker0, 172.17.34.0/24
- Host B: bridge docker0, 172.17.29.0/24
- Host C: bridge docker0, 172.17.33.0/24

> We can use the following configuration example to guide you on how to configure the Docker bridge network on your own machines. 
> 
> On each of the host machines, modify the Docker configuration file `/etc/docker/daemon.json` to set the `bip` (Bridge IP) to a specific subnet. For instance, on host A with IP 10.1.1.34, set the `bip` to 172.17.34.1/24. Similar modifications should be made for host B and C, as shown in the following:
>
> ```commandline
> // On host A (IP 10.1.1.34)
> $ sudo vim /etc/docker/daemon.json
> (vim) {
> (vim)   "bip": "172.17.34.1/24"
> (vim) }
> $ sudo service docker restart
> $ sudo service docker status
> ```
> This step defines the IP range for the Docker bridge network on each host. By separating the networks in this way, each Docker container on the hosts can have its unique IP, which assists in simulating intercommunication across different networks.
>
> Please note that once you change this configuration, you need to restart the Docker service to apply the changes. You can check the status of the Docker service using the `service docker status` command to ensure that it has restarted successfully.

To enable intercommunication between containers located within different network segments, we can configure the route table on the host machines to assist in forwarding IP packets sent from the containers.

The `route` command is used to modify the IP routing table of the Linux kernel. It's a way to define how packets should be forwarded depending on their destination address. Here is an example on how to set it up on each host:

```shell
// On host A (IP 10.1.1.34)
sudo route add -net 172.17.29.0 netmask 255.255.255.0 gw 10.1.1.29
sudo route add -net 172.17.33.0 netmask 255.255.255.0 gw 10.1.1.33

// On host B (IP 10.1.1.29)
sudo route add -net 172.17.33.0 netmask 255.255.255.0 gw 10.1.1.33
sudo route add -net 172.17.34.0 netmask 255.255.255.0 gw 10.1.1.34

// On host C (IP 10.1.1.33)
sudo route add -net 172.17.29.0 netmask 255.255.255.0 gw 10.1.1.29
sudo route add -net 172.17.34.0 netmask 255.255.255.0 gw 10.1.1.34
```

These commands add routes to the table so that packets sent to the Docker network on each host (e.g., 172.17.29.0, 172.17.33.0, and 172.17.34.0) are forwarded to the corresponding IP (represented by 10.1.1.29, 10.1.1.33, and 10.1.1.34 respectively). As a result, the containers on each host can communicate with the containers on the other hosts.

Next, we configure the iptables on hosts A, B, and C to support Source Network Address Translation (SNAT). SNAT is necessary in this scenario because it allows machines in different network segments (in this case, the Docker containers on different hosts) to communicate with each other. This is achieved by translating the source IP address of an outgoing packet to the IP address of the host machine. The host then routes the packet to its destination, and when the packet returns, it translates the destination IP back to the original Docker container.

Here's how to set up SNAT with iptables on each host:

```shell
sudo iptables -P INPUT ACCEPT
sudo iptables -P FORWARD ACCEPT
sudo iptables -t nat -F POSTROUTING

// On host A (IP 10.1.1.34)
sudo iptables -t nat -A POSTROUTING -s 172.17.34.0/24 ! -d 172.17.0.0/16 -j MASQUERADE

// On host B (IP 10.1.1.29)
sudo iptables -t nat -A POSTROUTING -s 172.17.29.0/24 ! -d 172.17.0.0/16 -j MASQUERADE

// On host C (IP 10.1.1.33)
sudo iptables -t nat -A POSTROUTING -s 172.17.33.0/24 ! -d 172.17.0.0/16 -j MASQUERADE
```

The `iptables -t nat -A POSTROUTING -s` command is applied to all packets coming from each Docker network (`-s 172.17.34.0/24`, `-s 172.17.29.0/24`, `-s 172.17.33.0/24`) that are not destined to their local network (`! -d 172.17.0.0/16`). The `-j MASQUERADE` option hides the Docker network behind the IP address of the host machine.

By setting up SNAT with iptables this way, we enable seamless communication between Docker containers across different network segments, which is crucial for a distributed system like GeoMX.

> NOTE: In addition to the above-mentioned method of manually configuring network route tables, there are many other ways to establish connectivity between Docker containers in different network segments. For example, [Weave](https://github.com/weaveworks/weave) and [Klonet](https://caod.oriprobe.com/articles/62233507/Klonet__a_network_emulation_platform_for_the_techn.htm) are good choices.
>
> Weave creates a virtual network that connects Docker containers deployed across multiple hosts. Essentially, it establishes a network bridge between hosts which allows containers to communicate as if they are on the same host.
> 
> Here's a basic example of how you could use Weave to connect Docker containers.
> 
> 1. Install Weave on each of the host machines:
> ```commandline
> sudo curl -L git.io/weave -o /usr/local/bin/weave
> sudo chmod a+x /usr/local/bin/weave
> ```
> 2. Launch Weave on each host:
> ```commandline
> weave launch
> ```
> 3. If you have Docker containers running, you can attach them to the Weave network:
> ```commandline
> weave attach <container_id>
> ```
> 4. When you run new Docker containers, you can add it to the Weave network via:
> ```commandline
> weave run <image>
> ```
> This will start the container and automatically attach it to the Weave network. Now, all containers connected via Weave can communicate seamlessly, regardless of the host they're on. If you encounter any problems using weave, please refer to the latest [weave docs](https://www.weave.works/docs/net/latest/install/installing-weave/) for the latest deployment guide.
> 
> Keep in mind that while Weave is an excellent tool, it's best suited for small to medium-sized networks. For larger networks or for networks with specific performance requirements, the Klonet platform might be more appropriate.

> NOTE: If there is a firewall between host machines, you must permit traffic to flow through TCP 6783 and UDP 6783 / 6784, which are Weave's control and data ports.

After setting up the network and ensuring the Docker containers can communicate with each other, the next step is to run the GeoMX processes in these containers. To do this, you need to set up the environment variables (described in the chapter of [pseudo-distributed deployment](#pseudo-distributed-deployment)) as per your GeoMX configuration and start the different node processes in different containers.

> NOTE: Kindly remember to correctly assign the IP addresses and port numbers for the global scheduler and all local schedulers. The containers running these schedulers should reflect their actual IP addresses within your network.

> NOTE: Please ensure that all these containers expose their ports to the host machine. This step is known as "port mapping" and is crucial for allowing external applications or systems to communicate with the GeoMX service running inside the Docker container.
> 
> To export a container's port to the host, use the `-p` option when running Docker image. For example, if we have the global scheduler listening on port 9092, we map it to port 9093 on its host machine via:
> ```commandline
> sudo docker run -it --rm --name geomx-cpu -p 9092:9092 lizonghango00o1/geomx:cpu-only bash
> ```
> Remember to adjust the port numbers to avoid port conflicts when setting up your Docker containers. When you map a container's port to a port on the host machine, that port on the host machine gets reserved for the container. This means that no other process or container can use that same port on the host machine while it's reserved. If multiple containers on the same host machine try to map to the same host port, a port conflict will occur, leading to errors and potentially failed deployments.
 
When setting up the global server and all local servers, you need to specify the IP and port number of the global scheduler. This is typically done by setting the `DMLC_PS_GLOBAL_ROOT_URI` and `DMLC_PS_GLOBAL_ROOT_PORT` environment variables to the IP and port number of the global scheduler.

For all the servers and workers (including the global server and master worker), it's necessary to specify the IP and port number of the local scheduler in their party. This can be done by setting the `DMLC_PS_ROOT_URI` and `DMLC_PS_ROOT_PORT` environment variables to the IP and port number of their own local scheduler.

Here's an example of how you might set these variables in a global server node:

```shell
DMLC_ROLE_GLOBAL=global_server \
DMLC_PS_GLOBAL_ROOT_URI=172.17.34.2 \  # IP of the global scheduler
DMLC_PS_GLOBAL_ROOT_PORT=9092 \        # Port of the global scheduler
DMLC_NUM_GLOBAL_SERVER=1 \
DMLC_NUM_GLOBAL_WORKER=2 \
DMLC_ROLE=server \
DMLC_PS_ROOT_URI=172.17.34.3 \         # IP of the local scheduler (in the central party)
DMLC_PS_ROOT_PORT=9093 \               # Port of the local scheduler (in the central party)
DMLC_NUM_SERVER=1 \
DMLC_NUM_WORKER=1 \
DMLC_ENABLE_CENTRAL_WORKER=0 \
DMLC_NUM_ALL_WORKER=4 \
PS_VERBOSE=1 \
DMLC_INTERFACE=weave \                 # Name of Weave's default network interface card
nohup python -c "import mxnet" > /dev/null &
```

Remember to replace the IP addresses and port numbers according to your actual network configuration. The configuration for other environment variables remains the same as previously discussed.

### Klonet-based Deployment
Klonet is a network emulation platform for the technology innovation. It is designed to support the development and testing of new network protocols and applications in a realistic environment. Klonet can emulate various network scenarios, such as wireless, mobile, satellite, and optical networks, and provide fine-grained control over the network parameters, such as bandwidth, delay, jitter, and packet loss. Klonet can also integrate with real devices and applications, such as routers, switches, sensors, and smartphones, to create hybrid network experiments. Klonet is based on the Linux operating system and uses virtualization and containerization technologies to create isolated network nodes and links. Klonet also provides a graphical user interface and a command-line interface for users to configure and manage their network experiments.

The tutorial for this part is coming soon :)

## Summary of Environment Variables

| Environment Variable       | Options                         | Node Used On                                                        | Description                                                                                           |
|----------------------------|---------------------------------|---------------------------------------------------------------------|-------------------------------------------------------------------------------------------------------|
| DMLC_ROLE                  | scheduler, server, worker       | local scheduler, local server, master worker, worker                | The role of the node within the party.                                                                |
| DMLC_PS_ROOT_URI           | IPv4 address                    | global server, master worker, local scheduler, local server, worker | IPv4 address of the local scheduler node.                                                             |
| DMLC_PS_ROOT_PORT          | Integer                         | same as above                                                       | Port number of the local scheduler node.                                                              |
| DMLC_NUM_SERVER            | Integer                         | same as above                                                       | Number of local servers in the participating party, or number of global servers in the central party. |
| DMLC_NUM_WORKER            | Integer                         | same as above                                                       | Number of workers in the current party, including the master worker.                                  |
| DMLC_ROLE_GLOBAL           | global_scheduler, global_server | global scheduler, global server                                     | The role of the node across different parties.                                                        |
| DMLC_PS_GLOBAL_ROOT_URI    | IPv4 address                    | global scheduler, global server, local server                       | IPv4 address of the global scheduler node.                                                            |
| DMLC_PS_GLOBAL_ROOT_PORT   | Integer                         | same as above                                                       | Port number of the global scheduler node.                                                             |
| DMLC_NUM_GLOBAL_SERVER     | Number                          | same as above                                                       | Number of global servers in the central party.                                                        |
| DMLC_NUM_GLOBAL_WORKER     | Number                          | same as above                                                       | Number of local servers worldwide.                                                                    |
| DMLC_ROLE_MASTER_WORKER    | 0, 1                            | master worker                                                       | Specify if the current node is the master worker.                                                     |
| DMLC_ENABLE_CENTRAL_WORKER | 0, 1                            | global server                                                       | Specify if the central party joins in model training.                                                 |
| DMLC_NUM_ALL_WORKER        | Number                          | global server, master worker, worker                                | Total number of workers actually participating in model training.                                     |
| DMLC_INTERFACE             | String                          | all                                                                 | Name of the network interface used by the node.                                                       |
| PS_VERBOSE                 | 0, 1, 2                         | all                                                                 | Verbosity level of the system logs.                                                                   |

Additional task-related environment variables can be found in the [configuration guide](https://github.com/INET-RC/GeoMX/blob/main/docs/configuration.md).