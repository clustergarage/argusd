# fimd

## Development

### Prerequisites

```
sudo apt install build-essential autoconf libtool pkg-config
sudo apt install libgflags-dev libgtest-dev
sudo apt install clang libc++-dev
```

#### gRPC/Protobuf

```
# Clone repo
git clone -b $(curl -L https://grpc.io/release) https://github.com/grpc/grpc

# Build Protobuf
cd grpc/third_party/protobuf
sudo make install

# Build gRPC
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
docker build -t clustergarage/fimd .
```

## Running

### Kubernetes

```
kubectl apply -f configs/fimd.yaml
```

### OpenShift

```
# add scc user for running privileged
oc edit scc privileged
# add under users:
# - system:serviceaccount:kube-system:fim-admin

oc apply -f configs/fimd.yaml
```

## Example Output

```
[server] Starting inotify watcher...
Listening for events on:
 - /proc/1234/root/var/log/foo
IN_MODIFY: /var/log/foo/bar.log [file]
```
