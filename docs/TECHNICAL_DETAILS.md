# Technical Details

## Watching Events on procfs

procfs has a per-process root filesystem located at `/proc/[pid]/root`. We leverage this in order to monitor for filesystem events in the container without having to `setns` into the `mnt` namespace and listen directly on the paths here. This is because the `mnt` namespace is slightly different than other namespaces, in that performing a `setns` in a multi-threaded environment will not work. We bypass this by simply listening relative to the process' root instead.

For example if watching `/path/to/file` is desired, we will set up the `inotify` watch on `/proc/[pid]/root/path/to/file`. Once events are received we strip off the `/proc/[pid]/root` prefix so it appears as if it was simply that original path.

## Spawning `inotify` and `mqueue` Child Processes

An anonymous `eventfd` pipe is created and passed into the **argusnotify** process. The notify process uses this to listen on the `ppoll` loop for any events sent to this anonymous pipe; the only event we send here is an exit event so we can kill the notify process from the parent process.

An `mqueue` is also started and passed into the **argusnotify** process. It is used when receiving events from these children processes to log that event in the parent process. The main log message is written to a file (`glog` logging framework) and a bidirectional gRPC stream so the **argus-controller** can record it in Prometheus. This is all done in a separate child thread that is spawned at the same time the `inotify` watcher is created.

These file descriptors are used when spawning the **argusnotify** process as a separate child thread. A `condition_variable` is kept for purpose of killing and recreating the process when updating an existing watcher, as well as cleaning up after itself if it were to critically fail. This child process is sent an exit message from the parent by way of the anonymous `eventfd` pipe in case we want to kill the child process from the parent.

## Recursive `inotify` Watchers

A `recursive: true` flag can be added when specifying an instance of the CRD used in the **argus** K8s configuration. Additionally, a `depth: N` flag can be specified in conjunction with this to only watch an `N` depth of recursiveness.

If specified as recursive, an internal data structure is kept up-to-date based on create, delete, and move events of directories under the path(s) specified in your CRD definition. In the event of an overflow, the tree is rebuilt; if the directory is unmounted or moved to a location outside of this tree, all remaining events are immediately discarded.

You may find when watching recursively that it is a bit noisy. If you want to filter out some directories such as a `.git` or cache folder, you can specify an `ignore` list similar to `path`. This will make sure `inotify` doesn't watch any unneeded files/folders and that you won't receive any unwanted events flooding your log.

## Finding the PID from Container ID

The **argus-controller** will pass the daemon a container ID, since it will not necessarily be sitting on the same node that needs to be monitored. It is then up to the daemon to find the process ID from the container ID.

This can be done for each container runtime, albeit each one is different.

**Docker** is by far the most complex, and over time this has changed with new versions and if it was running in Kubernetes or not. First, it requires the `docker.sock`, `docker.pid`, and `/sys/fs/cgroup` to be mounted from the host into the container. We grab the cgroup root and process cgroup paths and store them to construct the following attempts:

- First check under:
  `[cgroup_root]/[process_cgroup]/[container_id]/tasks`
- With more recent LXC, the cgroup will be in lxc/:
  `[cgroup_root]/[process_cgroup]/lxc/[container_id]/tasks`
- With more recent Docker, the cgroup will be in docker/:
  `[cgroup_root]/[process_cgroup]/docker/[container_id]/tasks`
- Even more recent Docker versions under systemd:
  `[cgroup_root]/system.slice/docker-[container_id].scope/tasks`
- Even more recent Docker versions:
  `[cgroup_root]/../systemd/docker/[container_id]/tasks`
- Kubernetes with Docker and CNI is even more different:
  `[cgroup_root]/../systemd/kubepods/*/pod*/[container_id]/tasks`
- Another flavor of containers location in recent Kubernetes 1.11+:
  `[cgroup_root]/[process_cgroup]/kubepods.slice/kubepods-besteffort.slice/*/docker-[container_id].scope/tasks`
- When running inside of a container with recent Kubernetes 1.11+:
  `[cgroup_root]/kubepods.slice/kubepods-besteffort.slice/*/docker-[container_id].scope/tasks`

Other container runtimes are much simpler and deterministic:

- **cri-o**:
  `/var/run/crio/[container_id]/pidfile`
- **rkt**:
  `/var/lib/rkt/pods/run/[container_id]/pid`
- **containerd**:
  `/var/run/containerd/*/*/[container_id]/init.pid`
