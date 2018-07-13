// +build linux
// +build go1.10
package main

import (
	//"bufio"
	"bytes"
	"fmt"
	"io/ioutil"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"

	//"github.com/fsnotify/fsnotify"
	//"github.com/opencontainers/runc/libcontainer"
	//_ "github.com/opencontainers/runc/libcontainer/nsenter"
	specs "github.com/opencontainers/runtime-spec/specs-go"
	"golang.org/x/net/context"
	syscall "golang.org/x/sys/unix"
	"google.golang.org/grpc"
	"google.golang.org/grpc/reflection"

	pb "clustergarage.io/fim-proto/fim"
)

const (
	port = ":50051"
)

type server struct {
	// Spec is the OCI runtime spec that configures this sandbox.
	Spec *specs.Spec `json:"spec"`
}

func init() {
	runtime.LockOSThread()
}

func findCgroupMountpoint(cgroupType string) (string, error) {
	output, err := ioutil.ReadFile("/proc/mounts")
	if err != nil {
		return "", err
	}

	// /proc/mounts has 6 fields per line, one mount per line, e.g.
	// cgroup /sys/fs/cgroup/devices cgroup rw,relatime,devices 0 0
	for _, line := range strings.Split(string(output), "\n") {
		parts := strings.Split(line, " ")
		if len(parts) == 6 && parts[2] == "cgroup" {
			for _, opt := range strings.Split(parts[3], ",") {
				if opt == cgroupType {
					return parts[1], nil
				}
			}
		}
	}

	return "", fmt.Errorf("cgroup mountpoint not found for %s", cgroupType)
}

// Returns the relative path to the cgroup docker is running in.
// borrowed from docker/utils/utils.go
// modified to get the docker pid instead of using /proc/self
func getThisCgroup(cgroupType string) (string, error) {
	dockerpid, err := ioutil.ReadFile("/var/run/docker.pid")
	if err != nil {
		return "", err
	}
	result := strings.Split(string(dockerpid), "\n")
	if len(result) == 0 || len(result[0]) == 0 {
		return "", fmt.Errorf("docker pid not found in /var/run/docker.pid")
	}
	pid, err := strconv.Atoi(result[0])
	if err != nil {
		return "", err
	}
	output, err := ioutil.ReadFile(fmt.Sprintf("/proc/%d/cgroup", pid))
	if err != nil {
		return "", err
	}
	for _, line := range strings.Split(string(output), "\n") {
		parts := strings.Split(line, ":")
		// any type used by docker should work
		if parts[1] == cgroupType {
			return parts[2], nil
		}
	}
	return "", fmt.Errorf("cgroup '%s' not found in /proc/%d/cgroup", cgroupType, pid)
}

func (s *server) getPidForContainer(id string) (int, error) {
	pid := 0
	// memory is chosen randomly, any cgroup used by docker works
	cgroupType := "memory"
	cgroupRoot, err := findCgroupMountpoint(cgroupType)
	if err != nil {
		return pid, err
	}
	cgroupThis, err := getThisCgroup(cgroupType)
	if err != nil {
		return pid, err
	}

	id += "*"
	attempts := []string{
		filepath.Join(cgroupRoot, cgroupThis, id, "tasks"),
		// With more recent lxc versions use, cgroup will be in lxc/
		filepath.Join(cgroupRoot, cgroupThis, "lxc", id, "tasks"),
		// With more recent docker, cgroup will be in docker/
		filepath.Join(cgroupRoot, cgroupThis, "docker", id, "tasks"),
		// Even more recent docker versions under systemd use docker-<id>.scope/
		filepath.Join(cgroupRoot, "system.slice", "docker-"+id+".scope", "tasks"),
		// Even more recent docker versions under cgroup/systemd/docker/<id>/
		filepath.Join(cgroupRoot, "..", "systemd", "docker", id, "tasks"),
		// Kubernetes with docker and CNI is even more different
		filepath.Join(cgroupRoot, "..", "systemd", "kubepods", "*", "pod*", id, "tasks"),
	}

	var filename string
	for _, attempt := range attempts {
		filenames, _ := filepath.Glob(attempt)
		if len(filenames) > 1 {
			return pid, fmt.Errorf("Ambiguous id supplied: %v", filenames)
		} else if len(filenames) == 1 {
			filename = filenames[0]
			break
		}
	}
	if filename == "" {
		return pid, fmt.Errorf("Unable to find container: %v", id[:len(id)-1])
	}

	output, err := ioutil.ReadFile(filename)
	if err != nil {
		return pid, err
	}

	result := strings.Split(string(output), "\n")
	if len(result) == 0 || len(result[0]) == 0 {
		return pid, fmt.Errorf("No pid found for container")
	}

	pid, err = strconv.Atoi(result[0])
	if err != nil {
		return pid, fmt.Errorf("Invalid pid '%s': %s", result[0], err)
	}
	return pid, nil
}

func joinNS(path string) error {
	runtime.LockOSThread()
	f, err := os.OpenFile(path, os.O_RDONLY, 0)
	if err != nil {
		return fmt.Errorf("failed get network namespace fd: %v", err)
	}
	defer f.Close()
	if _, _, err := syscall.RawSyscall(syscall.SYS_SETNS, f.Fd(), syscall.CLONE_NEWNS, 0); err != 0 {
		return err
	}
	return nil
}

func (s *server) startNotify(pid int, in *pb.FimdConfig) {
	fmt.Println(" start notify: ", pid)
	for _, subject := range in.Subjects {
		fmt.Println(subject)
		//cmd := exec.Command("sh", "-c",
		//	fmt.Sprintf("sudo ./bin/fim_inotify -p%d -nmnt %s %s", pid,
		//		fmt.Sprintf("-t%s", strings.Join(subject.Paths, " -t")),
		//		fmt.Sprintf("-e%s", strings.Join(subject.Events, " -e"))))

		/*
			wd, err := os.Getwd()
			if err != nil {
				fmt.Println(wd, err)
				return
			}
			file := fmt.Sprintf("/proc/%d/ns/mnt", pid)
			f, err := os.Open(file)
			if err != nil {
				fmt.Println(err)
				return
			}
			if err := setNS(f.Fd(), syscall.CLONE_NEWNS|
				syscall.CLONE_NEWUTS|
				syscall.CLONE_NEWIPC|
				syscall.CLONE_NEWPID|
				syscall.CLONE_NEWNET|
				syscall.CLONE_NEWUSER); err != nil {
				fmt.Println(err)
				return
			}

			b, err := exec.Command("ls", "-la", "/var/log").CombinedOutput()
			if err != nil {
				fmt.Println(err)
				return
			}
			fmt.Println("string: ", string(b))
		*/

		var out bytes.Buffer
		cmd := exec.Command("ls", "-la", "/var/log")
		cmd.Stdout = &out
		cmd.Stderr = os.Stderr
		//uid, gid := os.Getuid(), os.Getgid()
		//fmt.Println("uid:", uid, "| gid:", gid)
		//cmd.SysProcAttr = &syscall.SysProcAttr{
		//	Cloneflags: syscall.CLONE_NEWNS,
		//	Credential: &syscall.Credential{
		//		Uid: uint32(uid),
		//		Gid: uint32(gid),
		//	},
		//	UidMappings: []syscall.SysProcIDMap{
		//		{ContainerID: 0, HostID: uid, Size: 1},
		//	},
		//	GidMappings: []syscall.SysProcIDMap{
		//		{ContainerID: 0, HostID: gid, Size: 1},
		//	},
		//}

		if err := joinNS(fmt.Sprintf("/proc/%d/ns/mnt", pid)); err != nil {
			fmt.Println(err)
			return
		}
		fmt.Println("string: ", out.String())

		/*
			fmt.Println(" === nsenter === ")
			cmd := exec.Command("sudo", "nsenter",
				fmt.Sprintf("--mount=/proc/%d/ns/mnt", pid),
				//"--",
				//"",
			) //.CombinedOutput()

			//if err != nil {
			//	fmt.Println(err)
			//	return
			//}

			stdout, err := cmd.StdoutPipe()
			if err != nil {
				fmt.Println(err)
				return
			}
			if err := cmd.Start(); err != nil {
				fmt.Println("start err", err)
				return
			}
			scanner := bufio.NewScanner(stdout)
			go func() {
				for scanner.Scan() {
					fmt.Println(scanner.Text())
				}
			}()

			fmt.Println(" === wait === ")
			if err := cmd.Wait(); err != nil {
				fmt.Println("wait err", err)
				return
			}
		*/

		/*
			fmt.Println(" === fsnotify === ")
			// @TODO: fsnotify properly
			watcher, err := fsnotify.NewWatcher()
			if err != nil {
				fmt.Println(err)
			}
			defer watcher.Close()

			done := make(chan bool)
			go func() {
				for {
					select {
					case event := <-watcher.Events:
						fmt.Println("event:", event)
						if event.Op&fsnotify.Create == fsnotify.Create {
							fmt.Println("created file:", event.Name)
						}
						if event.Op&fsnotify.Remove == fsnotify.Remove {
							fmt.Println("removed file:", event.Name)
						}
						if event.Op&fsnotify.Write == fsnotify.Write {
							fmt.Println("modified file:", event.Name)
						}
						if event.Op&fsnotify.Rename == fsnotify.Rename {
							fmt.Println("renamed file:", event.Name)
						}
						if event.Op&fsnotify.Chmod == fsnotify.Chmod {
							fmt.Println("chmodded file:", event.Name)
						}
					case err := <-watcher.Errors:
						fmt.Println("error:", err)
					}
				}
			}()

			for _, path := range subject.Paths {
				fmt.Println("   ... path:", path)
				err = watcher.Add(path)
				if err != nil {
					fmt.Println(err)
				}
			}

			<-done
		*/
	}
}

func (s *server) NewWatch(ctx context.Context, in *pb.FimdConfig) (*pb.FimdHandle, error) {
	cid := strings.Replace(in.ContainerId, "docker://", "", 1)
	pid, err := s.getPidForContainer(cid)
	if err != nil {
		return nil, err
	}

	// begin fsnotify and listen for events on stdout
	go s.startNotify(pid, in)

	// @TODO: return a real fd handle
	return &pb.FimdHandle{Id: 1234}, nil
}

func main() {
	listener, err := net.Listen("tcp", port)
	if err != nil {
		fmt.Printf("failed to listen: %v\n", err)
	}

	s := grpc.NewServer()
	pb.RegisterFimdServer(s, &server{
		Spec: &specs.Spec{
			Root: &specs.Root{
				Path:     "/",
				Readonly: true,
			},
			Process: &specs.Process{
				Args: []string{"/bin/true"},
			},
		},
	})
	// register reflection service on gRPC server
	reflection.Register(s)

	if err := s.Serve(listener); err != nil {
		fmt.Printf("failed to serve: %v\n", err)
	}
}
