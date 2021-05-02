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