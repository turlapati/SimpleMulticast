//
// Created by hari on 10/31/23.
//
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define MULTICAST_ADDR "224.0.0.1"
#define MULTICAST_PORT 5000
#define UNICAST_PORT 5000

int main() {
    int socket_fd;
    struct sockaddr_in unicast_send_addr, unicast_recv_addr;
    char recv_buf[1024];
    long recv_len;
    char send_buf[] = "Reply via unicast!";

    // Create a UDP socket
    socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_fd < 0) {
        perror("socket creation failed");
        return 1;
    }

    int yes = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        perror("Reusing ADDR failed");
        return 1;
    }

    // Join multicast group
    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(MULTICAST_ADDR);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(socket_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        perror("setsockopt IP_ADD_MEMBERSHIP failed");
        return 1;
    }

    // Set the unicast address
    memset(&unicast_send_addr, 0, sizeof(unicast_send_addr));
    unicast_send_addr.sin_family = AF_INET;
    unicast_send_addr.sin_addr.s_addr = INADDR_ANY;
    unicast_send_addr.sin_port = htons(UNICAST_PORT);

    // Bind the socket to the unicast address
    if (bind(socket_fd, (struct sockaddr *) &unicast_send_addr, sizeof(unicast_send_addr)) < 0) {
        perror("bind");
        return 1;
    }

    while (1) {
        long recv_addr_len = sizeof(unicast_recv_addr);
        memset(&unicast_recv_addr, 0, recv_addr_len);
        // Receive the multicast packet
        recv_len = recvfrom(socket_fd, recv_buf, sizeof(recv_buf), 0, (struct sockaddr *) &unicast_recv_addr,
                            (unsigned int *) &recv_addr_len);
        if (recv_len < 0) {
            perror("recvfrom");
            break;
        }
        recv_buf[recv_len] = '\0';
        // Extract IP address of the responding host
        char *from_ip_address = inet_ntoa(unicast_recv_addr.sin_addr);
        printf("Received message: %s from %s\n", recv_buf, from_ip_address);

        // Respond to the received message
        if (sendto(socket_fd, send_buf, strlen(send_buf), 0, (struct sockaddr *) &unicast_recv_addr,
                   sizeof(unicast_recv_addr)) < 0) {
            perror("sendto");
            break;
        }

        sleep(1);
    }

    close(socket_fd);
    return 0;
}

