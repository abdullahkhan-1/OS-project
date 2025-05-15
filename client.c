#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <semaphore.h>
#include <errno.h>

#define SERVER_FIFO "/tmp/patient_requests"
#define FIFO_SEM_NAME "/fifo_mutex"

typedef struct {
    char name[51];
    int age;
    int patient_id;
    char cnic[14];
    char phone[15];
    int bed_number;
    int critical;
    int active;
} patient_t;

typedef struct {
    pid_t client_pid;
    char command[20];
    patient_t patient;
    int search_id;
    char search_name[51];
    char search_cnic[14];
} request_t;

void current_timestamp(char *buffer, size_t size) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm *t = localtime(&ts.tv_sec);
    snprintf(buffer, size, "%04d-%02d-%02d %02d:%02d:%02d.%09ld",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec, ts.tv_nsec);
}

void send_request(char *command, patient_t *patient, char *search_cnic) {
    char timestamp[128];
    current_timestamp(timestamp, sizeof(timestamp));
    printf("[%s] Request initiated by client PID %d\n", timestamp, getpid());

    sem_t *fifo_sem = sem_open(FIFO_SEM_NAME, O_CREAT, 0666, 1);
    if (fifo_sem == SEM_FAILED) {
        perror("sem_open failed");
        exit(1);
    }

    current_timestamp(timestamp, sizeof(timestamp));
    printf("[%s] Attempting to acquire FIFO semaphore...\n", timestamp);
    sem_wait(fifo_sem);
    current_timestamp(timestamp, sizeof(timestamp));
    printf("[%s] Acquired FIFO semaphore.\n", timestamp);

    int server_fd = open(SERVER_FIFO, O_WRONLY);
    if (server_fd < 0) {
        perror("Cannot open server FIFO");
        sem_post(fifo_sem);
        sem_close(fifo_sem);
        exit(1);
    }

    char client_fifo[64];
    sprintf(client_fifo, "/tmp/client_%d", getpid());
    mkfifo(client_fifo, 0666);

    request_t req = {0};
    req.client_pid = getpid();
    strcpy(req.command, command);
    if (patient) req.patient = *patient;
    if (search_cnic) strcpy(req.search_cnic, search_cnic);

    write(server_fd, &req, sizeof(req));
    close(server_fd);

    int client_fd = open(client_fifo, O_RDONLY);
    if (client_fd < 0) {
        perror("Cannot open client FIFO");
        unlink(client_fifo);
        sem_post(fifo_sem);
        sem_close(fifo_sem);
        exit(1);
    }

    char response[256] = {0};
    read(client_fd, response, sizeof(response));
    printf("Server response: %s\n", response);

    close(client_fd);
    unlink(client_fifo);
    sem_close(fifo_sem); // FIFO semaphore is released by server
    current_timestamp(timestamp, sizeof(timestamp));
    printf("[%s] Released FIFO semaphore.\n", timestamp);
}

int main() {
    patient_t patient;
    char command[20];

    while (1) {
        printf("Enter command (insert/release/search/exit): ");
        scanf("%s", command);

        if (strcmp(command, "insert") == 0) {
            printf("Enter patient details:\n");
            printf("Name: "); scanf(" %[^\n]", patient.name);
            printf("Age: "); scanf("%d", &patient.age);
            printf("CNIC: "); scanf("%s", patient.cnic);
            printf("Phone: "); scanf("%s", patient.phone);
            printf("Critical? (1=Yes, 0=No): "); scanf("%d", &patient.critical);

            send_request("insert", &patient, NULL);
        } else if (strcmp(command, "release") == 0) {
            printf("Enter patient ID to release: ");
            scanf("%d", &patient.patient_id);
            send_request("release", &patient, NULL);
        } else if (strcmp(command, "search") == 0) {
            char cnic[14];
            printf("Enter CNIC to search: ");
            scanf("%s", cnic);
            send_request("search", NULL, cnic);
        } else if (strcmp(command, "exit") == 0) {
            printf("Exiting client...\n");
            break;
        } else {
            printf("Invalid command!\n");
        }
    }

    return 0;
}

