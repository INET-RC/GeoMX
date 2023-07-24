FROM nvidia/cuda:9.0-devel

WORKDIR /root/

RUN APT_INSTALL="apt install -y --no-install-recommends" && \
    rm -rf /var/lib/apt/lists/* \
           /etc/apt/sources.list.d/cuda.list \
           /etc/apt/sources.list.d/nvidia-ml.list && \
    sed -i 's/archive.ubuntu.com/mirrors.tuna.tsinghua.edu.cn/g' \
        /etc/apt/sources.list && \
    apt update && \
    DEBIAN_FRONTEND=noninteractive $APT_INSTALL \
        build-essential \
        ca-certificates \
        cmake \
        zip \
        unzip \
        vim \ 
        wget \
        curl \
        git \
        apt-transport-https \
        openssh-client \
        openssh-server \
        libopencv-dev \
        libsnappy-dev \
        libopenblas-dev \
        tzdata \
        autogen \
        autoconf \
        automake \
        libtool

RUN apt autoremove && apt clean

# ==================================================================
# miniconda python3.7
# ------------------------------------------------------------------
RUN curl -so ~/anaconda.sh https://repo.anaconda.com/miniconda/Miniconda3-latest-Linux-x86_64.sh && \
    chmod +x ~/anaconda.sh && \
    ~/anaconda.sh -b -p /opt/conda && \
    rm ~/anaconda.sh

ENV PATH /opt/conda/bin:$PATH
ENV CONDA_AUTO_UPDATE_CONDA false

RUN conda config --add channels https://mirrors.tuna.tsinghua.edu.cn/anaconda/pkgs/free/ && \
    conda config --add channels https://mirrors.tuna.tsinghua.edu.cn/anaconda/pkgs/main/ && \
    conda config --add channels https://mirrors.tuna.tsinghua.edu.cn/anaconda/cloud/pytorch/ && \
    conda config --add channels https://mirrors.tuna.tsinghua.edu.cn/anaconda/cloud/conda-forge/ && \
    conda config --set show_channel_urls yes && \
    conda update --all

RUN pip install pandas opencv-python
RUN git clone https://ghproxy.com/https://github.com/INET-RC/GeoMX.git
RUN cd GeoMX && make -j$(nproc) && \
    cd python && pip install -e . && \
    python -c "import mxnet"
