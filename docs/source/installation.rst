Installation
============

This guide will walk you through the process of installing and deploying
GeoMX. It also explains the meaning of the environment variables used in
the accompanying shell scripts.

Use Pre-built Docker Image
~~~~~~~~~~~~~~~~~~~~~~~~~~

To simplify installation, we strongly recommend using our pre-built
Docker images, which are available on
`DockerHub <https://hub.docker.com/repository/docker/lizonghango00o1/geomx/general>`__:

.. code:: bash

   # If run on CPUs
   sudo docker run -it --rm --name geomx-cpu lizonghango00o1/geomx:cpu-only bash

   # If run on GPUs with CUDA 8.0
   sudo docker run -it --rm --name geomx-gpu lizonghango00o1/geomx:cu80 bash

   # If run on GPUs with CUDA 10.1
   sudo docker run -it --rm --name geomx-gpu lizonghango00o1/geomx:cu101 bash

These commands will automatically pull the required Docker image from
DockerHub and initiate a Docker container. You can directly interact
with GeoMX within this container.

.. note::
   For more in-depth information on using Docker / Nvidia-Docker, please
   refer to the `Docker Docs <https://docs.docker.com/get-started/>`__
   and the `NVIDIA Container
   Toolkit <https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/latest/index.html>`__.

Then we can use the script files provided in the ``scripts`` folder to
execute demo tasks. For example:

.. code:: bash

   # If run on CPUs
   cd GeoMX/scripts/cpu && bash run_vanilla_hips.sh

   # If run on GPUs
   cd GeoMX/scripts/gpu && bash run_vanilla_hips.sh

..

.. warning::
   If you are using the image versions with tags ``cu80`` and ``cu101``, the
   first-time initialization of GeoMX may take a few minutes. However,
   subsequent runs should proceed without delay. This issue is common and
   can occur with other frameworks like PyTorch and MXNET as well.

Build from Dockerfile
~~~~~~~~~~~~~~~~~~~~~

Alternatively, we can also generate a customized Docker image using a
Dockerfile. Some templates can be found in the ``docker`` folder.

To create a CPU-based Docker image, run:

.. code:: bash

   cd docker && sudo docker build -f build_on_cpu.dockerfile -t geomx:cpu-only .

And for a GPU-based Docker image, run:

.. code:: bash

   cd docker && sudo docker build -f build_on_gpu.dockerfile -t geomx:cu101 .

..

.. warning::
   This step may fail due to network issues. If this happens, retry or
   opt to compile GeoMX inside the Docker container.

Build from Source Code
~~~~~~~~~~~~~~~~~~~~~~

Though it is feasible to perform a direct installation on the host
machine, we generally advise against this approach. This is due to
potential issues that might arise from environmental incompatibilities.
For a detailed walkthrough of the installation process on a host
machine, please consult this
`README <https://github.com/INET-RC/GeoMX/blob/main/README.md>`__ file.