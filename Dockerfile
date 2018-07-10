FROM golang:latest as builder
RUN apt-get update && \
    apt-get -y install apt-transport-https \
        ca-certificates \
        curl \
        gnupg2 \
        software-properties-common && \
    curl -fsSL https://download.docker.com/linux/$(. /etc/os-release; echo "$ID")/gpg > /tmp/dkey; apt-key add /tmp/dkey && \
    add-apt-repository \
        "deb [arch=amd64] https://download.docker.com/linux/$(. /etc/os-release; echo "$ID") \
        $(lsb_release -cs) \
        stable" && \
    apt-get update && \
    apt-get -y install docker-ce
WORKDIR /go/src/clustergarage.io/fimd/
COPY . /go/src/clustergarage.io/fimd/
RUN CGO_ENABLED=0 go build -a -installsuffix cgo -o fimd .
CMD ["./fimd"]

#FROM golang:alpine
#RUN apk add --update --no-cache sudo bash ca-certificates docker
#WORKDIR /root/
#COPY --from=builder /go/src/clustergarage.io/fimd/fimd .
#COPY --from=builder /go/src/clustergarage.io/fimd/bin/fim_inotify .
#CMD ["./fimd"]
