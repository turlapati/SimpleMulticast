//
// Created by hari on 10/31/23.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <unistd.h>
#include <mqueue.h>
#include <fcntl.h>           /* For O_* constants */
#include <pthread.h>

#define MULTICAST_ADDR "224.0.0.1"
#define MULTICAST_PORT 5000
#define UNICAST_PORT 5000

#define MAX_IPS 254
#define MAX_FAILURES 10
//#define TIMEOUT 1

// Make sure the SEND & RECV names are opposite of what you have in other process
#define SEND_MQ_NAME "/sm2ls_queue"
#define RECV_MQ_NAME "/ls2sm_queue"

typedef struct Host {
    char ip_address[16];
    int failures;
    struct Host *next;
} Host;

Host *head = NULL;

mqd_t mqd_send, mqd_receive;
pthread_t thread_id;

void *worker_thread(__attribute__((unused)) void *arg) {
    int socket_fd;
    struct sockaddr_in multicast_addr, unicast_addr, unicast_recv_addr;
    char send_buf[] = "Hello via multicast!";
    char recv_buf[1024];
    long recv_len;

    // Create a UDP socket
    socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_fd < 0) {
        perror("socket");
        pthread_exit((void *) 1);
    }

    int int_no = 0;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &int_no, sizeof(int_no)) < 0) {
        perror("Reusing ADDR failed");
        pthread_exit((void *) 1);
    }

    if (setsockopt(socket_fd, IPPROTO_IP, IP_MULTICAST_LOOP, &int_no, sizeof(int_no)) < 0) {
        perror("Setting MULTICAST_LOOP=0 failed");
        pthread_exit((void *) 1);
    }

    struct ifreq interface;
    strcpy(interface.ifr_ifrn.ifrn_name, "eth0");
    if (setsockopt(socket_fd, SOL_SOCKET, SO_BINDTODEVICE, &interface, sizeof(interface)) < 0) {
        perror("Bind to eth0");
        pthread_exit((void *) 1);
    }

    // Set the unicast address
    unicast_addr.sin_family = AF_INET;
    unicast_addr.sin_addr.s_addr = INADDR_ANY;
    unicast_addr.sin_port = htons(UNICAST_PORT);

    // Bind the socket to the unicast address
    if (bind(socket_fd, (struct sockaddr *) &unicast_addr, sizeof(unicast_addr)) < 0) {
        perror("bind");
        pthread_exit((void *) 1);
    }

    // Set the multicast address
    multicast_addr.sin_family = AF_INET;
    multicast_addr.sin_addr.s_addr = inet_addr(MULTICAST_ADDR);
    multicast_addr.sin_port = htons(MULTICAST_PORT);

    // Set socket to non-blocking mode
    if (fcntl(socket_fd, F_SETFL, O_NONBLOCK) < 0) {
        perror("fcntl");
        pthread_exit((void *) 1);
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
//            printf("Received message: %s from %s\n", recv_buf, from_ip_address);

            // Check if IP address is already in hosts array
            int i;
            for (i = 0; i < host_count; i++) {
                if (strcmp(hosts[i].ip_address, from_ip_address) == 0) {
                    // IP address found in array, reset failures count
                    hosts[i].failures = 0;
                    break;
                }
            }

            // If IP address not found in array, add it
            if (i == host_count && host_count < MAX_IPS) {
                strncpy(hosts[host_count].ip_address, from_ip_address, sizeof(hosts[host_count].ip_address));
                hosts[host_count].failures = 0;
                host_count++;
                printf("Added new host...\n");
            } else if (host_count >= MAX_IPS) {
                printf("Hosts array is full\n");
            }
        }

        // Delete IP address if no response N times
        for (int i = 0; i < host_count; i++) {
            if (hosts[i].failures >= MAX_FAILURES) {
                // Remove host from list
                for (int j = i; j < host_count - 1; j++) {
                    memcpy(&hosts[j], &hosts[j + 1], sizeof(Host));
                }
                host_count--;
            }
        }

        sleep(1);
    }

    close(socket_fd);

    pthread_exit(NULL);
}

void handle_signal(__attribute__((unused)) int signum) {
    char quit_msg[] = "QUIT";
    if (mq_send(mqd_receive, quit_msg, sizeof(quit_msg), 0) == -1) {
        perror("mq_send from handle_signal");
    }
    // terminate the worker thread
    pthread_cancel(thread_id);
}

int main() {
    // Initialize message queue attributes
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = 1024;
    attr.mq_curmsgs = 0;

    // Initialize message queue
    mqd_send = mq_open(SEND_MQ_NAME, O_CREAT | O_WRONLY, 0666, &attr);
    if (mqd_send == (mqd_t) -1) {
        perror("mq_open SEND_MQ_NAME");
        exit(EXIT_FAILURE);
    }
    // NOTE: The receiving message queue need RW permissions, since signal handler can post a message to main thread
    mqd_receive = mq_open(RECV_MQ_NAME, O_CREAT | O_RDWR, 0666, &attr);
    if (mqd_receive == (mqd_t) -1) {
        perror("mq_open RECV_MQ_NAME");
        exit(EXIT_FAILURE);
    }

    // Initialize worker thread
    if (pthread_create(&thread_id, NULL, worker_thread, NULL) != 0) {
        perror("pthread_create failed");
        exit(EXIT_FAILURE);
    }

    // Register signal handlers
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGQUIT, handle_signal);
    signal(SIGHUP, handle_signal);

    // Main program loop
    while (1) {
        char buffer[2048];
        ssize_t bytes_read;

        // Wait for next message (Blocking)
        bytes_read = mq_receive(mqd_receive, buffer, sizeof(buffer), NULL);
        if (bytes_read == -1) {
            perror("mq_receive");
            break;
        }

        buffer[bytes_read] = '\0';

        printf("MQ Message: %s\n", buffer);

        if (strcmp(buffer, "GET_LIST") == 0) {
            char reply[1024] = "";
            for (int i = 0; i < host_count; i++) {
                if (i > 0)
                    strcat(reply, "\n");
                strcat(reply, hosts[i].ip_address);
            }
            if (mq_send(mqd_send, reply, sizeof(reply), 0) == -1) {
                perror("mq_send");
            }
        } else if (strcmp(buffer, "QUIT") == 0) {
            break;
        }

        sleep(1);
    }

    // Unlink the message queue
    if (mq_unlink(SEND_MQ_NAME) == -1) {
        perror("mq_unlink SEND_MQ_NAME");
        exit(EXIT_FAILURE);
    }

    if (mq_unlink(RECV_MQ_NAME) == -1) {
        perror("mq_unlink RECV_MQ_NAME");
        exit(EXIT_FAILURE);
    }

    // Clean up
    if (mq_close(mqd_send) == -1) {
        perror("mq_close mqd_send");
        exit(EXIT_FAILURE);
    }

    if (mq_close(mqd_receive) == -1) {
        perror("mq_close mqd_receive");
        exit(EXIT_FAILURE);
    }

    if (pthread_join(thread_id, NULL) != 0) {
        perror("pthread_join");
        exit(EXIT_FAILURE);
    }

    return 0;
}