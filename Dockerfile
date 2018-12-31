FROM ubuntu:bionic as builder
RUN apt-get update && \
  apt-get install -y g++ gcc git golang-go ninja-build perl tar wget && \
  wget -qO- https://cmake.org/files/v3.12/cmake-3.12.1-Linux-x86_64.tar.gz | \
    tar --strip-components=1 -xz -C /usr/local
ENV GRPC_HEALTH_PROBE_VERSION v0.2.0
# Do this in a new folder `dockerbuild`. `build` is ignored via .dockerignore
# for builds outside docker, so we need to make sure we name this something
# other than `build`.
WORKDIR /opt/argusd
COPY . /opt/argusd
RUN cmake -H. \
    -Bdockerbuild \
    -GNinja \
    -Wno-dev \
    -DCMAKE_C_COMPILER=gcc \
    -DCMAKE_CXX_COMPILER=g++ \
    -DCMAKE_BUILD_TYPE=Release && \
  cmake --build dockerbuild
# Include grpc_health_probe for K8s liveness/readiness checks.
RUN wget -qO/bin/grpc_health_probe \
    https://github.com/grpc-ecosystem/grpc-health-probe/releases/download/${GRPC_HEALTH_PROBE_VERSION}/grpc_health_probe-linux-amd64 && \
  chmod +x /bin/grpc_health_probe

FROM alpine:latest
COPY --from=builder /opt/argusd/dockerbuild/argusd /
COPY --from=builder /bin/grpc_health_probe /bin/
# glog requires /tmp to exist as log_dir is /tmp by default.
COPY --from=builder /tmp /tmp
CMD ["/argusd"]
