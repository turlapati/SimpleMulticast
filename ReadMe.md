# Testing Multicast Client & Server
## Statically compile the binaries
Plan is to use a basic (or minimum size) docker image like **_scratch_** or **_alpine_** to run the client and server
programs.
Make sure that CMakeLists.txt has the following directives to build a static binary.
```shell
set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++ -static")
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
set(BUILD_SHARED_LIBS OFF)
```
Check the binary with **_ldd_** _<binary_name>_ command to make sure it does not have dependencies on any shared
libraries

## Client

Client will send multicast messages and expects unicast response from all listeners. There can be zero, one or more
than one listener out there in the local network.

To dockerized client executable, use _**Dockerfile_client**_:

```dockerfile
FROM scratch
WORKDIR /app
ADD ./theClient /app
CMD ["/app/theClient"]
```
### Create docker image
```shell
sudo docker build -t the_client -f Dockerfile_client .
```
## Server

The server will join a multicast group and wait for a message. Once it receives a multicast packet, it sends unicast
reply to the sender.

To dockerized server executable, use _**Dockerfile_server**_:
```dockerfile
FROM scratch
WORKDIR /app
ADD ./theServer /app
CMD ["/app/theServer"]
```
### Create docker image
```shell
sudo docker build -t the_server -f Dockerfile_server .
```
## Instructions for Docker
### Create a Docker Network
Note: Change **enp3s0** below to match with your host-machine's interface name
```shell
sudo docker network create -d ipvlan \
                           --subnet=192.168.92.0/24 \
                           --gateway=192.168.92.1 \
                           -o ipvlan_mode=l2 \
                           -o parent=enp3s0 \
                           my_network
```
### Start one or more Server instance(s)
Make sure to assign a _unique static IP address_ within the subnet for each instance and avoid IP address conflicts
```shell
sudo docker run -it \
                --network my_network \
                --ip 192.168.92.100 \
                --cap-add NET_ADMIN \
                the_server
```

### Start the Client to start sending multicasts
```shell
sudo docker run -it \
                --network my_network \
                --ip 10.255.30.200 \
                --cap-add NET_ADMIN \
                the_client
```

## References

https://www.cs.unc.edu/~jeffay/dirt/FAQ/comp249-001-F99/mcast-socket.html

## Other useful commands

#### Stop-all docker processes

```shell
sudo docker stop $(sudo docker ps -q)
```

#### Remove all docker images

```shell
sudo docker rmi -f $sudo docker images -q)
```