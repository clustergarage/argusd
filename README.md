# fim-inotify

## Building

```
docker build -t clustergarage/fim-inotify .
```

## Running

```
docker run --privileged -it --rm --pid=host clustergarage/fim-inotify /proc/$PID/ns/$NAMESPACE [paths...]
```

### Example Run

```
# get list of pids by label
$ ./bin/get_container_pids run=nginx
1234

# watch for events
$ docker run --privileged -it --rm --pid=host clustergarage/fim-inotify /proc/1234/ns/mnt /var/log/nginx
Press ENTER key to terminate.
Listening for events.
IN_OPEN: /proc/1234/ns/mnt/foo.log [file]
IN_MODIFY: /proc/1234/ns/mnt/foo.log [file]
Listening for events stopped.
```
