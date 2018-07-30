# fimd

## Development

### Prerequisites for Local build

```
# install necessary packages
sudo apt install build-essential autoconf libtool pkg-config
sudo apt install libgflags-dev libgtest-dev
sudo apt install clang libc++-dev

# install gRPC/Protobuf
git clone -b $(curl -L https://grpc.io/release) https://github.com/grpc/grpc

# build protobuf
cd grpc/third_party/protobuf
sudo make install

# build gRPC
cd grpc
git submodule update --init
make
sudo make install
```

### Building

#### CMake

```
mkdir build && cd $_
cmake ..
make
```

#### Docker

```
docker build -t clustergarage/fimd:latest .
```
