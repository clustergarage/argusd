FROM golang:latest as builder
WORKDIR /go/src/clustergarage.io/fimd/
COPY . /go/src/clustergarage.io/fimd/
RUN CGO_ENABLED=0 go build -a -installsuffix cgo -o fimd .

FROM alpine:latest
RUN apk --no-cache add ca-certificates
WORKDIR /root/
COPY --from=builder /go/src/clustergarage.io/fimd/fimd .
CMD ["./fimd"]
