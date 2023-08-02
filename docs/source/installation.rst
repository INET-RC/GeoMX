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
machine, please follow the instructions below.

* Step 1: Download the source code by cloning our GeoMX repository:

.. code:: bash

   git clone https://github.com/INET-RC/GeoMX.git

.. warning::

   If it fails due to network issues, please try this instead:

   .. code:: bash

      git clone https://ghproxy.com/https://github.com/INET-RC/GeoMX.git

* Step 2: Install third-party dependencies. Use the following command to do so:

.. code:: bash

   sudo apt update
   sudo apt install -y build-essential cmake libopencv-dev libopenblas-dev libsnappy-dev autogen autoconf automake libtool

* Step 3: Create a conda environment named ``geomx`` with Python 3.7 and install necessary packages:

.. code:: bash

   conda create -n geomx python=3.7
   conda activate geomx
   # Run the following commands in the conda environment named geomx.
   pip install --upgrade pip
   pip install numpy==1.17.3 pandas opencv-python -i https://mirrors.aliyun.com/pypi/simple

.. warning::

  If you haven't installed Anaconda3, or if your version of Anaconda3 is too high,
  it might cause the creation of the Python3.7 conda environment to fail.
  To solve this, you can use the following commands to install a compatible version
  of Anaconda3:

  .. code:: bash

    curl -k -so ~/anaconda.sh https://mirrors.tuna.tsinghua.edu.cn/anaconda/miniconda/Miniconda3-py37_4.9.2-Linux-x86_64.sh
    chmod +x ~/anaconda.sh
    ~/anaconda.sh -b -p /opt/conda
    rm ~/anaconda.sh
    echo "export PATH=/opt/conda/bin:\$PATH" >> ~/.bashrc
    echo "export CONDA_AUTO_UPDATE_CONDA=false" >> ~/.bashrc
    /opt/conda/bin/conda init bash

  If the installation was successful, you will see the following output when you
  re-open your shell:

  .. code:: bash

    (base) user@hostname:~$

* Step 4: Copy the configuration file. Based on your preference for running GeoMX
  on CPUs or GPUs, copy the corresponding configuration file to ``GeoMX/config.mk``.
  For CPU, use:

.. code:: bash

    cd GeoMX && cp make/cpu_config.mk ./config.mk

And for GPU, copy ``make/gpu_config.mk`` instead. Refer to
`cpu_config.mk <https://github.com/INET-RC/GeoMX/blob/main/make/cpu_config.mk>`_ and
`gpu_config.mk <https://github.com/INET-RC/GeoMX/blob/main/make/gpu_config.mk>`_
for detailed configuration instructions.

* Step 5: Build the source code. Here, we use all CPU cores to build GeoMX faster.
  You may need to decrease the value to avoid CPU overload.

.. code:: bash

    make -j$(nproc)

.. warning::

    This step may fail due to network issues. If that happens, rerun the command again
    at a later time. Once the build is successful, you will see a new folder ``lib``
    containing the library file ``libmxnet.so``.

.. warning::

    We might encounter a fatal error stating "opencv2/opencv.hpp: No such file or directory"
    during ``make``. If this happens, follow the instructions given in the `OpenCV official
    doc <https://docs.opencv.org/2.4/doc/tutorials/introduction/linux_install/linux_install.html#linux-installation>`_
    to install OpenCV2.

    If you're just testing GeoMX or do not require OpenCV,
    you can simply disable it in the ``./config.mk`` file by setting ``USE_OPENCV = 0``.

    If you're using the Dockerfile or pre-built Docker image we provide, OpenCV2 has already
    been installed and you won't need to worry about this error.

* Step 6: Bind GeoMX to Python.

.. code:: bash

    cd python && pip install -e .

The ``-e`` flag is optional. It is equivalent to ``--editable`` and means that if you
edit the source files, these changes will be reflected in the package installed.

* Step 7: Test if GeoMX is installed successfully.

.. code:: python

    import mxnet as mx
    from mxnet import nd

    # Use this if we build GeoMX with USE_CUDA = 0.
    nd.zeros(1, ctx=mx.cpu())
    # Use this if we build GeoMX with USE_CUDA = 1.
    # nd.zeros(1, ctx=mx.gpu(0))

If no errors are reported, GeoMX was installed successfully.

.. warning::

    If you encounter errors while compiling with ``USE_CUDA = 1``, try
    downgrading the CUDA version to ``8.0`` or ``10.1``. Higher CUDA
    versions might be not supported.