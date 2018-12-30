FROM ubuntu:bionic as builder
RUN apt-get update && \
  apt-get install -y cmake g++ gcc git ninja-build perl tar wget
ENV GRPC_HEALTH_PROBE_VERSION v0.2.0
# Do this in a new folder `dockerbuild`. `build` is ignored via .dockerignore
# for builds outside docker, so we need to make sure we name this something
# other than `build`.
WORKDIR /opt/argusd
COPY . /opt/argusd
RUN cmake -DCMAKE_C_COMPILER=gcc \
    -DCMAKE_CXX_COMPILER=g++ \
    -H. \
    -Bdockerbuild \
    -GNinja && \
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
