# fimd

## Development

### Prerequisites

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

## Running

### Kubernetes

```
kubectl apply -f configs/fimd.yaml
```

### OpenShift

```
oc adm policy add-scc-to-user privileged -n kube-system -z fim-admin

oc apply -f configs/fimd.yaml
```

## Example Output

```
[server] Starting inotify watcher...
Listening for events on:
 - /proc/1234/root/var/log/foo
IN_MODIFY: /var/log/foo/bar.log [file]
```
