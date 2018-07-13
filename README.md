# fim\_inotify

```
Usage:
 fim_inotify -p<pid> -n<namespace> -t<path>... [-e<event>...] [-f<format>]

Watch for namespace events within paths of a target PID.

Options:
 -p, --pid <pid>        target PID to watch
 -n, --ns <namespace>   target namespace {ipc|net|mnt|pid|user|uts}
 -t, --path <path>      target watch path(s)
 -e, --event <event>    event to watch {access|modify|attrib|open|close|create|delete|move|all}
     --only-dir         only watch path if it is a directory
     --dont-follow      do not follow a symlink
     --exclude-unlink   exclude events on unlinked objects
     --oneshot          only send event once
 -f, --format <format>  custom log format

 -h, --help             display this help
 -v, --version          display version
```

## Prerequisites

```
sudo apt install build-essential autoconf libtool pkg-config
sudo apt install libgflags-dev libgtest-dev
sudo apt install clang libc++-dev
```

### gRPC/Protobuf

```
# Clone repo
git clone -b $(curl -L https://grpc.io/release) https://github.com/grpc/grpc

# Build Protobuf
cd grpc/third_party/protobuf
sudo make install

# Build gRPC
cd grpc
git submodule update --init
make -j4
sudo make install
```

## Building

### Make

```
mkdir build
cd build
cmake ..
make

# clean all
make clean
```

### Docker

```
docker build -t clustergarage/fim_inotify:latest .
```

## OpenShift SCC

```
oc edit scc privileged
# add under users:
# - system:serviceaccount:kube-system:fim-admin
```

## Running

### Shell

```
./bin/fim_inotify -p1234 -nmnt -t/some/path
```

### Docker

```
docker run --privileged -it --rm --pid=host clustergarage/fim_inotify:latest -p1234 -nmnt -t/some/path
```

#### Example Output

```
# get list of pids by label
$ ./bin/get_container_pids run=nginx
1234

# watch for events
$ docker run --privileged -it --rm --pid=host clustergarage/fim_inotify:latest -p1234 -nmnt -t/var/log/nginx -emodify
Listening for events.
IN_MODIFY: /var/log/nginx/some.log [file]
...
```
