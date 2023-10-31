//
// Created by hari on 10/31/23.
//
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>


#define MULTICAST_ADDR "224.0.0.1"
#define MULTICAST_PORT 5000
#define UNICAST_PORT 5000

int main() {
    int socket_fd;
    struct sockaddr_in multicast_addr, unicast_addr, unicast_recv_addr;
    char send_buf[] = "Hello via multicast!";
    char recv_buf[1024];
    long recv_len;

    // Create a UDP socket
    socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_fd < 0) {
        perror("socket");
        return 1;
    }

    int yes = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        perror("Reusing ADDR failed");
        return 1;
    }

    // Set the unicast address
    unicast_addr.sin_family = AF_INET;
    unicast_addr.sin_addr.s_addr = INADDR_ANY;
    unicast_addr.sin_port = htons(UNICAST_PORT);

    // Bind the socket to the unicast address
    if (bind(socket_fd, (struct sockaddr *) &unicast_addr, sizeof(unicast_addr)) < 0) {
        perror("bind");
        return 1;
    }

    // Set the multicast address
    multicast_addr.sin_family = AF_INET;
    multicast_addr.sin_addr.s_addr = inet_addr(MULTICAST_ADDR);
    multicast_addr.sin_port = htons(MULTICAST_PORT);

    // Set socket to non-blocking mode
    if (fcntl(socket_fd, F_SETFL, O_NONBLOCK) < 0) {
        perror("fcntl");
        return 1;
    }

    while (1) {
        // Send the multicast packet
        if (sendto(socket_fd, send_buf, strlen(send_buf), 0, (struct sockaddr *) &multicast_addr,
                   sizeof(multicast_addr)) < 0) {
            perror("sendto");
            break;
        }

        long recv_addr_len = sizeof(unicast_recv_addr);
        memset(&unicast_recv_addr, 0, recv_addr_len);
        // Receive the reply
        while ((recv_len = recvfrom(socket_fd, recv_buf, sizeof(recv_buf), 0, (struct sockaddr *) &unicast_recv_addr,
                                    (unsigned int *) &recv_addr_len)) > 0) {
            recv_buf[recv_len] = '\0';
            // Extract IP address of the responding host
            char *from_ip_address = inet_ntoa(unicast_recv_addr.sin_addr);
            printf("Received message: %s from %s\n", recv_buf, from_ip_address);
        }

        sleep(1);
    }

    close(socket_fd);

    return 0;
}
