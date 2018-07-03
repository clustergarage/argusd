package main

import (
	"bufio"
	"bytes"
	"fmt"
	"log"
	"net"
	"os"
	"os/exec"
	"strings"

	dockercli "github.com/docker/docker/client"
	//"github.com/fsnotify/fsnotify"
	"golang.org/x/net/context"
	"google.golang.org/grpc"
	"google.golang.org/grpc/reflection"

	pb "clustergarage.io/fim-proto/fim"
)

const (
	port = ":50051"
)

type server struct{}

func (s *server) getContainerPID(in *pb.FimdConfig) (pid int32, err error) {
	// @TODO: find container PID
	// - for now, assume docker (docker:// protocol)
	// - later add cri,rkt,runc,etc.

	// @TODO: temporary hack; for when docker API versions
	// are out of sync (minikube-provided docker, etc.)
	// ==== start hack ====
	cmd := exec.Command("docker", "version", "--format", "{{.Server.APIVersion}}")
	cmdOutput := &bytes.Buffer{}
	cmd.Stdout = cmdOutput
	err = cmd.Run()
	if err != nil {
		return 0, err
	}
	apiVersion := strings.TrimSpace(string(cmdOutput.Bytes()))
	os.Setenv("DOCKER_API_VERSION", apiVersion)
	// ==== end hack ====

	cli, err := dockercli.NewEnvClient()
	if err != nil {
		return 0, err
	}
	//cid := strings.TrimLeft(in.ContainerId, "docker://")
	cid := strings.Replace(in.ContainerId, "docker://", "", 1)
	inspect, err := cli.ContainerInspect(context.Background(), cid)
	if err != nil {
		return 0, err
	}
	return int32(inspect.State.Pid), nil
}

func (s *server) startNotify(pid int32, in *pb.FimdConfig) {
	for _, subject := range in.Subjects {
		cmd := exec.Command("sh", "-c",
			fmt.Sprintf("sudo ./bin/fim_inotify -p%d -nmnt %s %s", pid,
				fmt.Sprintf("-t%s", strings.Join(subject.Paths, " -t")),
				fmt.Sprintf("-e%s", strings.Join(subject.Events, " -e"))))

		stdout, err := cmd.StdoutPipe()
		if err != nil {
			return
		}
		if err := cmd.Start(); err != nil {
			return
		}

		scanner := bufio.NewScanner(stdout)
		go func() {
			for scanner.Scan() {
				fmt.Println(scanner.Text())
			}
		}()

		if err := cmd.Wait(); err != nil {
			return
		}
	}

	/*
		// @TODO: nsenter into /proc/$PID/ns/mnt
		fmt.Printf("sudo nsenter --target %s --mount\n", pid)
		cmd := exec.Command("sudo", "nsenter", "--target", pid, "--mount")
		cmdOutput := &bytes.Buffer{}
		cmd.Stdout = cmdOutput
		if err := cmd.Run(); err != nil {
			fmt.Println(err)
			return
		}
		fmt.Println(string(cmdOutput.Bytes()))
	*/

	/*
		// @TODO: fsnotify properly
		watcher, err := fsnotify.NewWatcher()
		if err != nil {
			log.Fatal(err)
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

		for _, subject := range in.Subjects {
			for _, path := range subject.Paths {
				err = watcher.Add(path)
				if err != nil {
					log.Fatal(err)
				}
			}
		}

		<-done
	*/
}

func (s *server) NewWatch(ctx context.Context, in *pb.FimdConfig) (*pb.FimdHandle, error) {
	pid, err := s.getContainerPID(in)
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
		log.Fatalf("failed to listen: %v", err)
	}

	s := grpc.NewServer()
	pb.RegisterFimdServer(s, &server{})
	// register reflection service on gRPC server
	reflection.Register(s)

	if err := s.Serve(listener); err != nil {
		log.Fatalf("failed to serve: %v", err)
	}
}
