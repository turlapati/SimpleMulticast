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

#define INTERFACE_NAME "eth0"
#define MULTICAST_ADDR "224.0.0.1"
#define MULTICAST_PORT 5000
#define UNICAST_PORT 5000

#define MAX_FAILURES 10
//#define TIMEOUT 1

// Make sure the SEND & RECV names are opposite of what you have in other process
#define SEND_MQ_NAME "/sm2ls_queue"
#define RECV_MQ_NAME "/ls2sm_queue"

typedef struct DiscoveredList {
    char ip_address[16];
    int failures;
    struct DiscoveredList *next;
} discovered_list;

typedef struct SnapShot {
    char ip_address[16];
    struct SnapShot *next;
} current_list;

discovered_list *head = NULL;
mqd_t mqd_send, mqd_receive;
pthread_t thread_id;
pthread_mutex_t discovered_list_lock;

// Function to add a node to the current_list
void add_to_current_list(current_list **head_ref, char *ip_address) {
    current_list *new_node = malloc(sizeof(current_list));
    strcpy(new_node->ip_address, ip_address);
    new_node->next = *head_ref;
    *head_ref = new_node;
}

// Function to add a node to the discovered_list
void add_to_discovered_list(discovered_list **head_ref, char *ip_address) {
    discovered_list *new_node = malloc(sizeof(discovered_list));
    strcpy(new_node->ip_address, ip_address);
    new_node->failures = 0;
    new_node->next = *head_ref;
    *head_ref = new_node;
}

// Function to add new nodes to the discovered list based on the current list
void add_new_nodes_to_discovered_list(discovered_list **head_ref, current_list *current_head) {
    current_list *curr_temp = current_head;
    while (curr_temp != NULL) {
        discovered_list *temp = *head_ref;
        while (temp != NULL && strcmp(temp->ip_address, curr_temp->ip_address) != 0) {
            temp = temp->next;
        }
        if (temp == NULL) {
            // add node to discovered list
            add_to_discovered_list(head_ref, curr_temp->ip_address);
        }
        curr_temp = curr_temp->next;
    }
}

// Function to delete a node from the discovered_list
void delete_discovered_list_node(discovered_list **head_ref, char *ip_address) {
    discovered_list *temp = *head_ref, *prev;

    // If head node itself holds the key to be deleted
    if (temp != NULL && strcmp(temp->ip_address, ip_address) == 0) {
        *head_ref = temp->next;   // Changed head
        free(temp);               // free old head
        return;
    }

    // Search for the key to be deleted, keep track of the previous node as we need to change 'prev->next'
    while (temp != NULL && strcmp(temp->ip_address, ip_address) != 0) {
        prev = temp;
        temp = temp->next;
    }

    // If key was not present in linked list
    if (temp == NULL) return;

    // Unlink the node from linked list
    prev->next = temp->next;

    free(temp);  // Free memory
}

// Function to update the discovered list based on the current list
void update_discovered_list(discovered_list **head_ref, current_list *current_head) {
    discovered_list *temp = *head_ref;
    while (temp != NULL) {
        current_list *curr_temp = current_head;
        while (curr_temp != NULL && strcmp(temp->ip_address, curr_temp->ip_address) != 0) {
            curr_temp = curr_temp->next;
        }
        if (curr_temp == NULL) {
            temp->failures++;
            if (temp->failures > MAX_FAILURES) {
                // delete node from discovered list
                delete_discovered_list_node(head_ref, temp->ip_address);
            }
        } else {
            temp->failures = 0;
        }
        temp = temp->next;
    }
}

// Function to free the memory allocated for the current_list
void free_current_list(current_list *current_head) {
    current_list *tmp;

    while (current_head != NULL) {
        tmp = current_head;
        current_head = current_head->next;
        free(tmp);
    }
}

// Function to free the memory allocated for the discovered_list
void free_discovered_list(discovered_list *disc_head) {
    discovered_list *tmp;

    while (disc_head != NULL) {
        tmp = disc_head;
        disc_head = disc_head->next;
        free(tmp);
    }
}

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
    strcpy(interface.ifr_ifrn.ifrn_name, INTERFACE_NAME);
    if (setsockopt(socket_fd, SOL_SOCKET, SO_BINDTODEVICE, &interface, sizeof(interface)) < 0) {
        perror("Bind to INTERFACE_NAME");
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

        current_list *current_head = NULL;

        long recv_addr_len = sizeof(unicast_recv_addr);
        memset(&unicast_recv_addr, 0, recv_addr_len);
        // Receive the reply
        while ((recv_len = recvfrom(socket_fd, recv_buf, sizeof(recv_buf), 0, (struct sockaddr *) &unicast_recv_addr,
                                    (unsigned int *) &recv_addr_len)) > 0) {
            recv_buf[recv_len] = '\0';
            // Extract IP address of the responding host
            char *from_ip_address = inet_ntoa(unicast_recv_addr.sin_addr);
            // printf("Received message: %s from %s\n", recv_buf, from_ip_address);
            // Build a current_list using from_ip_address
            add_to_current_list(&current_head, from_ip_address);
        }

        // Lock the mutex to update the discovered_list
        pthread_mutex_lock(&discovered_list_lock);

        // Compare ip_address fields in current_list and discovered_list
        // If any IP address in discovered_list is not in current_list, increase its failures count.
        // If the failure count is greater than MAX_FAILURES, delete the IP address from the discovered_list
        update_discovered_list(&head, current_head);

        // If IP address not found, add it to discovered_list
        add_new_nodes_to_discovered_list(&head, current_head);

        // Unlock the mutex
        pthread_mutex_unlock(&discovered_list_lock);

        // Free memory allocated for current list
        free_current_list(current_head);

        sleep(1);
    }

    close(socket_fd);

    // Free memory allocated for discovered list
    free_discovered_list(head);

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

    if (pthread_mutex_init(&discovered_list_lock, NULL) != 0) {
        perror("pthread_mutex_init");
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

//        printf("MQ Message: %s\n", buffer);

        if (strcmp(buffer, "GET_LIST") == 0) {
            char reply[1024] = "";

            // Lock the mutex before reading the discovered_list
            pthread_mutex_lock(&discovered_list_lock);

            discovered_list *tmp = head;
            while (tmp != NULL) {
                strcat(reply, "\n");
                strcat(reply, tmp->ip_address);
                tmp = tmp->next;
            }

            // Unlock the mutex
            pthread_mutex_unlock(&discovered_list_lock);

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