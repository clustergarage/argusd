package main

import (
	//"bufio"
	//"fmt"
	"log"
	"net"
	//"os/exec"
	//"strings"

	pb "clustergarage.io/fim-proto/fim"
	//"golang.org/x/net/context"
	"google.golang.org/grpc"
	"google.golang.org/grpc/reflection"
)

const (
	port = ":50051"
)

type server struct{}

// @TODO: configurable:
// - path to fim-inotify, or fallback on $PATH
// - namespace (-n)
// - events (-e)
// - extra flags [--only-dir|--dont-follow|--exclude-unlink|--oneshot]
/*
func (s *server) beginFimInotify(in *pb.InotifyConfig) {
	cmd := exec.Command("sh", "-c",
		fmt.Sprintf("sudo ./bin/fim_inotify -p%d -nmnt %s\n", in.Pid,
			fmt.Sprintf("-t%s", strings.Join(strings.Split(in.Paths, ":"), " -t"))))

	stdout, err := cmd.StdoutPipe()
	if err != nil {
		log.Fatal(err)
	}
	if err := cmd.Start(); err != nil {
		log.Fatal(err)
	}

	scanner := bufio.NewScanner(stdout)
	go func() {
		for scanner.Scan() {
			fmt.Println(scanner.Text())
		}
	}()

	if err := cmd.Wait(); err != nil {
		log.Fatal(err)
	}
}

func (s *server) NewWatch(ctx context.Context, in *pb.InotifyConfig) (*pb.WatchHandle, error) {
	// begin fim-inotify command and listen for events on stdout
	go s.beginFimInotify(in)

	// @TODO: what is the uri supposed to return?
	return &pb.WatchHandle{Uri: fmt.Sprintf("/proc/%d/ns/mnt", in.Pid)}, nil
}
*/

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
