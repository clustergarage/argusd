# fim

## Building

```
docker build -t fim .
```

## Running

```
docker run --privileged -it --rm --pid=host fim /proc/$PID/ns/$NAMESPACE [paths...]

# Example:
docker run --privileged -it --rm --pid=host fim /proc/1234/ns/mnt /var/log/nginx
```
