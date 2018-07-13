FROM ubuntu:bionic as builder
RUN apt-get update && \
    apt-get -y install apt-transport-https \
        autoconf \
        build-essential \
        ca-certificates \
        clang \
        cmake \
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
# grpc, protobuf
RUN cd /usr/local/src && \
    git clone -b $(curl -L https://grpc.io/release) https://github.com/grpc/grpc && \
    cd grpc && \
    git submodule update --init && \
    make -j4 && \
    make install && \
    cd third_party/protobuf && \
    make install
# cmake
RUN cd /usr/local/src && \
    curl -O https://cmake.org/files/v3.10/cmake-3.10.3.tar.gz && \
    tar xvf cmake-3.10.3.tar.gz && \
    cd cmake-3.10.3 && \
    ./bootstrap && \
    make && \
    make install && \
    cd .. && \
    rm -rf cmake*
WORKDIR /opt/fimd/
COPY . /opt/fimd/
RUN mkdir -p build && \
    cd build && \
    cmake .. && \
    make -j4
CMD ["/bin/bash"]
ENTRYPOINT ["/opt/fimd/build/fimd"]

#FROM alpine:latest
#RUN apk --no-cache add ca-certificates
#WORKDIR /root/
#COPY --from=builder /opt/fimd/build/fimd .
#CMD ["./fimd"]
