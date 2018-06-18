# fim

## Building

```
docker build -t fim .
```

## Running

```
docker run --privileged -it --rm --pid=host fim /proc/$PID/ns/$NAMESPACE [paths...]
```

### Example Run

```
# get list of pids by label
$ ./bin/get_container_pids run=nginx
1234

# watch for events
$ docker run --privileged -it --rm --pid=host fim /proc/1234/ns/mnt /var/log/nginx
Press ENTER key to terminate.
Listening for events.
IN_OPEN: /proc/1234/ns/mnt/foo.log [file]
IN_MODIFY: /proc/1234/ns/mnt/foo.log [file]
Listening for events stopped.
```

## TODO

- replace `get_container_pids` script with a kubernetes plugin
- plugin would keep a fim container in sync with all pods matching the target label
- if a pod spins up/down an appropriate action will add/remove a fim container to monitor

