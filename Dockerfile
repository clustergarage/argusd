FROM ubuntu

RUN apt-get update && \
  apt-get install -y inotify-tools gcc && \
  rm -rf /var/lib/apt/lists/*

WORKDIR /opt/fim
COPY . /opt/fim

RUN gcc -o bin/fim-inotify src/fim-inotify.c

ENTRYPOINT ["/opt/fim/bin/fim-inotify"]
