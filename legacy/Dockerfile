FROM alpine

RUN apk add --update --no-cache make gcc inotify-tools musl-dev

WORKDIR /opt/fim
COPY . /opt/fim

RUN make

ENTRYPOINT ["/opt/fim/bin/fim_inotify"]
