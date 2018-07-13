FROM ubuntu:bionic

RUN apt-get update && \
    apt-get -y install apt-transport-https \
        autoconf \
        build-essential \
        ca-certificates \
        clang \
        curl \
        g++ \
        gnupg2 \
        libtool \
        libgflags-dev \
        libgtest-dev \
        libc++-dev \
        pkg-config \
        software-properties-common && \
    curl -fsSL https://download.docker.com/linux/$(. /etc/os-release; echo "$ID")/gpg > /tmp/dkey; apt-key add /tmp/dkey && \
    add-apt-repository \
        "deb [arch=amd64] https://download.docker.com/linux/$(. /etc/os-release; echo "$ID") \
        $(lsb_release -cs) \
        stable" && \
    apt-get update && \
    apt-get -y install docker-ce

RUN cd /usr/local/src && \
    git clone -b $(curl -L https://grpc.io/release) https://github.com/grpc/grpc && \
    cd grpc && \
    git submodule update --init && \
    make -j4 && \
    make install && \
    cd third_party/protobuf && \
    make install

WORKDIR /opt/fimd/
COPY . /opt/fimd/
RUN make -j4

CMD ["./fim_server"]
