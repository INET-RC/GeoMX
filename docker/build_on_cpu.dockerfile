FROM ubuntu:16.04

WORKDIR /root/

RUN APT_INSTALL="apt install -y --no-install-recommends" && \
    rm -rf /var/lib/apt/lists/* && \
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
        libtool \
        iputils-ping \
        net-tools \
        htop \
        psmisc

RUN apt autoremove && apt clean

RUN curl -k -so ~/anaconda.sh https://mirrors.tuna.tsinghua.edu.cn/anaconda/miniconda/Miniconda3-py37_4.9.2-Linux-x86_64.sh && \
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

RUN pip install numpy==1.17.3 pandas opencv-python -i https://mirrors.aliyun.com/pypi/simple

RUN git clone https://ghproxy.com/https://github.com/INET-RC/GeoMX.git
RUN cd GeoMX && cp make/cpu_config.mk ./config.mk && make -j$(nproc) && \
    cd python && pip install -e .
