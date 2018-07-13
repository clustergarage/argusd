# fim-proto

## Build

```
# build golang definitions
protoc -I. fim.proto --go_out=plugins=grpc:.

# build c++ definitions
make fim.grpc.pb.cc fim.pb.cc
```

### Generates

* `fim.pb.h` - the header which declares your generated message classes
* `fim.pb.cc` - which contains the implementation of your message classes
* `fim.grpc.pb.h` - the header which declares your generated service classes
* `fim.grpc.pb.cc` - which contains the implementation of your service classes
