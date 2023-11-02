//
// Created by hari on 10/31/23.
//
#include <fcntl.h>
#include <mqueue.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

// Make sure the SEND & RECV names are opposite of what you have in other process
#define SEND_MQ_NAME "/ls2sm_queue"
#define RECV_MQ_NAME "/sm2ls_queue"

int main(int argc, char *argv[]) {
    mqd_t mqd_send, mqd_receive;
    char send_buffer[256];
    char recv_buffer[2048];
    long num_queries, interval, num_retries;

    // Check command-line arguments
    if (argc != 3) {
        fprintf(stderr, "Usage: %s num_queries interval\n", argv[0]);
        exit(1);
    }

    char *end_ptr;

    num_queries = strtol(argv[1], &end_ptr, 10);
    if (errno == ERANGE || *end_ptr != '\0') {
        fprintf(stderr, "Invalid number: %s\n", argv[1]);
        exit(1);
    }

    num_retries = num_queries; // The while loop below wants to distinguish num_queries == 0 situation

    interval = strtol(argv[2], &end_ptr, 10);
    if (errno == ERANGE || *end_ptr != '\0') {
        fprintf(stderr, "Invalid number: %s\n", argv[2]);
        exit(1);
    }

    // Initialize message queue attributes
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = 1024;
    attr.mq_curmsgs = 0;

    // Open the message queue ListServers->SndMulCast
    mqd_send = mq_open(SEND_MQ_NAME, O_CREAT | O_WRONLY, 0666, &attr);
    if (mqd_send == (mqd_t) -1) {
        perror("mq_open for SEND_MQ_NAME:");
        exit(1);
    }

    // Open the message queue SndMulCast->ListServers
    mqd_receive = mq_open(RECV_MQ_NAME, O_CREAT | O_RDONLY, 0666, &attr);
    if (mqd_receive == (mqd_t) -1) {
        perror("mq_open for RECV_MQ_NAME:");
        exit(1);
    }

//    printf("Start...\n");
    int ret_val = 0;

    struct timespec my_timespec;
    my_timespec.tv_sec = interval - 1;
    my_timespec.tv_nsec = 0;

    // Receive and print the response
    ssize_t bytes_read;

    // Send the "GET_LIST" command periodically
    while (num_queries == 0 || num_retries-- > 0) {
//        printf("Before mq_send... %ld\n", num_queries);
        strcpy(send_buffer, "GET_LIST");
        if (mq_send(mqd_send, send_buffer, sizeof(send_buffer), 0) == -1) {
            perror("mq_send");
            ret_val = 1;
            break;
        }

        printf("Sent GET_LIST command\n");

        // bytes_read = mq_receive(mqd_receive, recv_buffer, sizeof(recv_buffer), NULL);
        bytes_read = mq_timedreceive(mqd_receive, recv_buffer, sizeof(recv_buffer), NULL, &my_timespec);
        if (bytes_read >= 0) {
            recv_buffer[bytes_read] = '\0';
            printf("Received: %s\n", recv_buffer);
        } else {
            if (errno != ETIMEDOUT) {
                printf("Error num: %d\n", errno);
                perror("mq_receive");
                ret_val = 1;
                break;
            }
            printf("No response...\n");
        }

        sleep(interval);
    }

    // Unlink the message queue
    mq_unlink(RECV_MQ_NAME);
    mq_unlink(SEND_MQ_NAME);
    // Close the message queue
    mq_close(mqd_send);
    mq_close(mqd_receive);

    return ret_val;
}
