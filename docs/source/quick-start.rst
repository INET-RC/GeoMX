Quick Start
===========

This guide will help you get started with GeoMX in just a few minutes.

.. note::
    -  **Prerequisites:** We provide a pre-built Docker image for a quick trial of
       GeoMX. For this, Docker needs to be installed following this
       `guide <https://docs.docker.com/engine/install/ubuntu/>`__.

Pull the Docker Image
---------------------

To run on CPUs, use:

.. code:: bash

   sudo docker pull lizonghango00o1/geomx:cpu-only

To run on GPUs with CUDA 8.0, use:

.. code:: bash

   sudo docker pull lizonghango00o1/geomx:cu80

To run on GPUs with CUDA 10.1, use:

.. code:: bash

   sudo docker pull lizonghango00o1/geomx:cu101

Run the Docker Container
------------------------

If you’re using the image with tag ``cpu-only``, use the following
command to start the container:

.. code:: bash

   sudo docker run -it --rm --name geomx lizonghango00o1/geomx:cpu-only bash

That’s similar for the GPU versions ``cu80`` and ``cu101``, but we need
to add the ``--gpus all`` option:

.. code:: bash

   sudo docker run -it --rm --name geomx --gpus all lizonghango00o1/geomx:cu101 bash

Run the Demo Scripts
--------------------

We provide some `example
scripts <https://github.com/INET-RC/GeoMX/tree/main/scripts>`__ of how
to train a deep learning model using GeoMX in the ``scripts`` and
``examples`` folders.

To quickly start with the vanilla version of GeoMX, run:

.. code:: bash

   # If run on CPUs
   cd GeoMX/scripts/cpu && bash run_vanilla_hips.sh

   # If run on GPUs
   cd GeoMX/scripts/gpu && bash run_vanilla_hips.sh

We also provide scripts for several other optimization techniques, they
are:

#. Bidirectional Gradient Sparsification (Bi-Sparse): ``bash run_bisparse_compression.sh``.

#. Low-Precision Quantization (FP16): ``bash run_fp16.sh``.

#. Mixed-Precision Quantization (MPQ): ``bash run_mixed_precision.sh``.

#. Differential Gradient Transmission (DGT): ``bash run_dgt.sh``.

#. TSEngine: ``bash run_tsengine.sh``.

#. Priority-based Parameter Propagation (P3): ``bash run_p3.sh``.

#. Multi-Server Load Balancing (MultiGPS): ``bash run_multi_gps.sh``.

#. Mixed-Synchronous Algorithm (MixedSync): ``bash run_mixed_sync.sh``.

#. Hierarchical Frequency Aggregation (HFA): ``bash run_hfa_sync.sh``.

.. warning::
    NOTE: If you are using the images with tags ``cu80`` and ``cu101``,
    the first-time initialization of GeoMX may take a few minutes.
    However, subsequent runs should proceed without delay. This issue is
    common and can occur with other frameworks like PyTorch and MXNET as
    well.
