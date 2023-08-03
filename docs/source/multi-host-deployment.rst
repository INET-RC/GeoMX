Deploy GeoMX on Multiple Hosts
------------------------------

To deploy GeoMX on real-world geographical data centers, we need to use
the multi-host mode for cluster deployment.

In this tutorial, we use three host machines as an example, namely A, B,
and C with their respective IP addresses as follows:

-  Host A IP: 10.1.1.34
-  Host B IP: 10.1.1.29
-  Host C IP: 10.1.1.33

To simulate the interconnection among three data centers using these
three host machines, we manually segregate the Docker containers on
these machines into three distinct network segments.

-  Host A: bridge docker0, 172.17.34.0/24
-  Host B: bridge docker0, 172.17.29.0/24
-  Host C: bridge docker0, 172.17.33.0/24

..

.. note::
   We can use the following configuration example to guide you on how to
   configure the Docker bridge network on your own machines.

   On each of the host machines, modify the Docker configuration file
   ``/etc/docker/daemon.json`` to set the ``bip`` (Bridge IP) to a
   specific subnet. For instance, on host A with IP 10.1.1.34, set the
   ``bip`` to 172.17.34.1/24. Similar modifications should be made for
   host B and C, as shown in the following:

   .. code:: bash

      // On host A (IP 10.1.1.34)
      $ sudo vim /etc/docker/daemon.json
      (vim) {
      (vim)   "bip": "172.17.34.1/24"
      (vim) }
      $ sudo service docker restart
      $ sudo service docker status

   This step defines the IP range for the Docker bridge network on each
   host. By separating the networks in this way, each Docker container
   on the hosts can have its unique IP, which assists in simulating
   intercommunication across different networks.

   Please note that once you change this configuration, you need to
   restart the Docker service to apply the changes. You can check the
   status of the Docker service using the ``service docker status``
   command to ensure that it has restarted successfully.

To enable intercommunication between containers located within different
network segments, we can configure the route table on the host machines
to assist in forwarding IP packets sent from the containers.

The ``route`` command is used to modify the IP routing table of the
Linux kernel. It’s a way to define how packets should be forwarded
depending on their destination address. Here is an example on how to set
it up on each host:

.. code:: shell

   // On host A (IP 10.1.1.34)
   sudo route add -net 172.17.29.0 netmask 255.255.255.0 gw 10.1.1.29
   sudo route add -net 172.17.33.0 netmask 255.255.255.0 gw 10.1.1.33

   // On host B (IP 10.1.1.29)
   sudo route add -net 172.17.33.0 netmask 255.255.255.0 gw 10.1.1.33
   sudo route add -net 172.17.34.0 netmask 255.255.255.0 gw 10.1.1.34

   // On host C (IP 10.1.1.33)
   sudo route add -net 172.17.29.0 netmask 255.255.255.0 gw 10.1.1.29
   sudo route add -net 172.17.34.0 netmask 255.255.255.0 gw 10.1.1.34

These commands add routes to the table so that packets sent to the
Docker network on each host (e.g., 172.17.29.0, 172.17.33.0, and
172.17.34.0) are forwarded to the corresponding IP (represented by
10.1.1.29, 10.1.1.33, and 10.1.1.34 respectively). As a result, the
containers on each host can communicate with the containers on the other
hosts.

Next, we configure the iptables on hosts A, B, and C to support Source
Network Address Translation (SNAT). SNAT is necessary in this scenario
because it allows machines in different network segments (in this case,
the Docker containers on different hosts) to communicate with each
other. This is achieved by translating the source IP address of an
outgoing packet to the IP address of the host machine. The host then
routes the packet to its destination, and when the packet returns, it
translates the destination IP back to the original Docker container.

Here’s how to set up SNAT with iptables on each host:

.. code:: shell

   sudo iptables -P INPUT ACCEPT
   sudo iptables -P FORWARD ACCEPT
   sudo iptables -t nat -F POSTROUTING

   // On host A (IP 10.1.1.34)
   sudo iptables -t nat -A POSTROUTING -s 172.17.34.0/24 ! -d 172.17.0.0/16 -j MASQUERADE

   // On host B (IP 10.1.1.29)
   sudo iptables -t nat -A POSTROUTING -s 172.17.29.0/24 ! -d 172.17.0.0/16 -j MASQUERADE

   // On host C (IP 10.1.1.33)
   sudo iptables -t nat -A POSTROUTING -s 172.17.33.0/24 ! -d 172.17.0.0/16 -j MASQUERADE

The ``iptables -t nat -A POSTROUTING -s`` command is applied to all
packets coming from each Docker network (``-s 172.17.34.0/24``,
``-s 172.17.29.0/24``, ``-s 172.17.33.0/24``) that are not destined to
their local network (``! -d 172.17.0.0/16``). The ``-j MASQUERADE``
option hides the Docker network behind the IP address of the host
machine.

By setting up SNAT with iptables this way, we enable seamless
communication between Docker containers across different network
segments, which is crucial for a distributed system like GeoMX.

.. note::
   In addition to the above-mentioned method of manually
   configuring network route tables, there are many other ways to
   establish connectivity between Docker containers in different network
   segments. For example,
   `Weave <https://github.com/weaveworks/weave>`__ and
   `Klonet <https://caod.oriprobe.com/articles/62233507/Klonet__a_network_emulation_platform_for_the_techn.htm>`__
   are good choices.

   Weave creates a virtual network that connects Docker containers
   deployed across multiple hosts. Essentially, it establishes a network
   bridge between hosts which allows containers to communicate as if
   they are on the same host.

   Here’s a basic example of how you could use Weave to connect Docker
   containers.

   1. Install Weave on each of the host machines:

   .. code:: bash

      sudo curl -L https://raw.githubusercontent.com/weaveworks/weave/master/weave -o /usr/local/bin/weave
      sudo chmod a+x /usr/local/bin/weave

   2. Launch Weave on each host:

   .. code:: bash

      weave launch

   3. If you have Docker containers running, you can attach them to the
      Weave network:

   .. code:: bash

      weave attach <container_id>

   This will attach the specified container to the Weave network. Now,
   all containers connected via Weave can communicate seamlessly,
   regardless of the host they’re on. If you encounter any problems
   using weave, please refer to the latest `weave
   docs <https://www.weave.works/docs/net/latest/install/installing-weave/>`__
   for the latest deployment guide.

   Keep in mind that while Weave is an excellent tool, it’s best suited
   for small to medium-sized networks. For larger networks or for
   networks with specific performance requirements, the Klonet platform
   might be more appropriate.

..

.. warning::

   If there is a firewall between host machines, you must permit
   traffic to flow through TCP 6783 and UDP 6783 / 6784, which are
   Weave’s control and data ports.

After setting up the network and ensuring the Docker containers can
communicate with each other, the next step is to run the GeoMX processes
in these containers. To do this, you need to set up the environment
variables (described in the chapter of
:doc:`Deploy GeoMX in Pseudo-distributed Mode <pseudo-distributed-deployment>`
as per your GeoMX configuration and start the different node processes
in different containers.

.. warning::
   Kindly remember to correctly assign the IP addresses and port
   numbers for the global scheduler and all local schedulers. The
   containers running these schedulers should reflect their actual IP
   addresses within your network.

.. warning::
   Please ensure that all these containers expose their ports to
   the host machine. This step is known as “port mapping” and is crucial
   for allowing external applications or systems to communicate with the
   GeoMX service running inside the Docker container.

   To export a container’s port to the host, use the ``-p`` option when
   running Docker image. For example, if we have the global scheduler
   listening on port 9092, we map it to port 9092 on its host machine
   via:

   .. code:: bash

      sudo docker run -it --rm --name geomx-cpu -p 9092:9092 lizonghango00o1/geomx:cpu-only bash

   Remember to adjust the port numbers to avoid port conflicts when
   setting up your Docker containers. When you map a container’s port to
   a port on the host machine, that port on the host machine gets
   reserved for the container. This means that no other process or
   container can use that same port on the host machine while it’s
   reserved. If multiple containers on the same host machine try to map
   to the same host port, a port conflict will occur, leading to errors
   and potentially failed deployments.

When setting up the global server and all local servers, you need to
specify the IP and port number of the global scheduler. This is
typically done by setting the ``DMLC_PS_GLOBAL_ROOT_URI`` and
``DMLC_PS_GLOBAL_ROOT_PORT`` environment variables to the IP and port
number of the global scheduler.

For all the servers and workers (including the global server and master
worker), it’s necessary to specify the IP and port number of the local
scheduler in their party. This can be done by setting the
``DMLC_PS_ROOT_URI`` and ``DMLC_PS_ROOT_PORT`` environment variables to
the IP and port number of their own local scheduler.

Here’s an example of how you might set these variables in a global
server node:

.. code:: shell

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
   DMLC_INTERFACE=ethwe \                 # Name of network interface, the default is ethwe if Weave is used
   nohup python -c "import mxnet" > /dev/null &

Remember to replace the IP addresses and port numbers according to your
actual network configuration. The configuration for other environment
variables remains the same as previously discussed.