#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <time.h>

#define SERVER_FIFO "/tmp/patient_requests"
#define SHM_NAME "/patient_shm"
#define FIFO_SEM_NAME "/fifo_mutex"
#define MAX_PATIENTS 100
#define REGULAR_BEDS 10
#define EMERGENCY_BEDS 3

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
    patient_t patients[MAX_PATIENTS];
    int last_patient_id;
    int regular_beds[REGULAR_BEDS];
    int emergency_beds[EMERGENCY_BEDS];
} patient_shm_t;

typedef struct {
    pid_t client_pid;
    char command[20];
    patient_t patient;
    int search_id;
    char search_name[51];
    char search_cnic[14];
} request_t;

sem_t *mutex;

void log_with_timestamp(const char *msg) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char ts[64];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", t);
    printf("[%s] %s\n", ts, msg);
    fflush(stdout);
}

void write_log(patient_t *p) {
    int fd = open("patient_log.txt", O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd >= 0) {
        char buf[256];
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char timestamp[64];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", t);

        int len = snprintf(buf, sizeof(buf),
            "[%s] Released: ID=%d Name=%s Age=%d CNIC=%s Phone=%s Bed=%d Critical=%d\n",
            timestamp, p->patient_id, p->name, p->age, p->cnic, p->phone, p->bed_number, p->critical);
        write(fd, buf, len);
        close(fd);
    }
}

void handle_insert(int client_fd, patient_shm_t *shm_ptr, request_t *req) {
    log_with_timestamp("Waiting to acquire mutex in insert");
    sem_wait(mutex);
    log_with_timestamp("Mutex acquired in insert");

    patient_t *p = &req->patient;
    p->patient_id = shm_ptr->last_patient_id++;
    p->bed_number = 0;
    p->active = 1;

    for (int i = 0; i < REGULAR_BEDS; i++) {
        if (shm_ptr->regular_beds[i] == 0) {
            shm_ptr->regular_beds[i] = p->patient_id;
            p->bed_number = i + 1;
            break;
        }
    }

    if (p->bed_number == 0 && p->critical == 1) {
        for (int i = 0; i < EMERGENCY_BEDS; i++) {
            if (shm_ptr->emergency_beds[i] == 0) {
                shm_ptr->emergency_beds[i] = p->patient_id;
                p->bed_number = i + REGULAR_BEDS + 1;
                break;
            }
        }
    }

    if (p->bed_number == 0) {
        write(client_fd, "No beds available\n", strlen("No beds available\n"));
    } else {
        shm_ptr->patients[p->patient_id] = *p;
        char response[64];
        int len = sprintf(response, "Admitted: ID=%d Bed=%d\n", p->patient_id, p->bed_number);
        write(client_fd, response, len);
    }

    sem_post(mutex);
    log_with_timestamp("Mutex released in insert");
}

void handle_release(int client_fd, patient_shm_t *shm_ptr, request_t *req) {
    log_with_timestamp("Waiting to acquire mutex in release");
    sem_wait(mutex);
    log_with_timestamp("Mutex acquired in release");

    int found = 0;
    for (int i = 0; i < MAX_PATIENTS; i++) {
        if (shm_ptr->patients[i].active == 1 &&
            shm_ptr->patients[i].patient_id == req->patient.patient_id) {

            int bed = shm_ptr->patients[i].bed_number;
            if (bed > 0 && bed <= REGULAR_BEDS) {
                shm_ptr->regular_beds[bed - 1] = 0;
            } else if (bed > REGULAR_BEDS && bed <= REGULAR_BEDS + EMERGENCY_BEDS) {
                shm_ptr->emergency_beds[bed - REGULAR_BEDS - 1] = 0;
            }

            shm_ptr->patients[i].active = 0;
            write_log(&shm_ptr->patients[i]);

            write(client_fd, "Patient released\n", 18);
            found = 1;
            break;
        }
    }

    if (!found) {
        write(client_fd, "Invalid patient ID\n", 20);
    }

    sem_post(mutex);
    log_with_timestamp("Mutex released in release");
}

void handle_search(int client_fd, patient_shm_t *shm_ptr, request_t *req) {
    log_with_timestamp("Waiting to acquire mutex in search");
    sem_wait(mutex);
    log_with_timestamp("Mutex acquired in search");

    int found = 0;
    for (int i = 0; i < MAX_PATIENTS; i++) {
        if (shm_ptr->patients[i].active == 1 &&
            strcmp(shm_ptr->patients[i].cnic, req->search_cnic) == 0) {

            patient_t *p = &shm_ptr->patients[i];
            char response[128];
            int len = sprintf(response, "ID=%d Name=%s Age=%d Phone=%s Bed=%d Critical=%d\n",
                              p->patient_id, p->name, p->age, p->phone, p->bed_number, p->critical);
            write(client_fd, response, len);
            found = 1;
            break;
        }
    }

    if (!found)
        write(client_fd, "Patient not found\n", strlen("Patient not found\n"));

    sem_post(mutex);
    log_with_timestamp("Mutex released in search");
}

void *process_client(void *arg) {
    int fifo_fd = *((int *)arg);
    free(arg);

    request_t req;
    read(fifo_fd, &req, sizeof(req));

    // Open FIFO semaphore
    sem_t *fifo_sem = sem_open(FIFO_SEM_NAME, 0);
    sem_post(fifo_sem);  // Release FIFO semaphore
    sem_close(fifo_sem);

    char client_fifo[64];
    sprintf(client_fifo, "/tmp/client_%d", req.client_pid);
    int client_fd = open(client_fifo, O_WRONLY);

    int shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
    patient_shm_t *shm_ptr = mmap(NULL, sizeof(patient_shm_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    if (strcmp(req.command, "insert") == 0) {
        handle_insert(client_fd, shm_ptr, &req);
    } else if (strcmp(req.command, "release") == 0) {
        handle_release(client_fd, shm_ptr, &req);
    } else if (strcmp(req.command, "search") == 0) {
        handle_search(client_fd, shm_ptr, &req);
    } else {
        write(client_fd, "Unknown command\n", 17);
    }

    close(client_fd);
    close(fifo_fd);
    return NULL;
}

int main() {
    unlink(SERVER_FIFO);
    mkfifo(SERVER_FIFO, 0666);

    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    ftruncate(shm_fd, sizeof(patient_shm_t));
    patient_shm_t *shm_ptr = mmap(NULL, sizeof(patient_shm_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    memset(shm_ptr, 0, sizeof(patient_shm_t));

    mutex = sem_open("/mutex", O_CREAT, 0666, 1);
    sem_open(FIFO_SEM_NAME, O_CREAT, 0666, 1); // Ensure FIFO semaphore exists

    int dummy_fd = open(SERVER_FIFO, O_WRONLY | O_NONBLOCK); // Keeps FIFO open

    while (1) {
        int fifo_fd = open(SERVER_FIFO, O_RDONLY);
        if (fifo_fd < 0) continue;

        int *fd_ptr = malloc(sizeof(int));
        *fd_ptr = fifo_fd;
        pthread_t tid;
        pthread_create(&tid, NULL, process_client, fd_ptr);
        pthread_detach(tid);
    }

    sem_close(mutex);
    sem_unlink("/mutex");
    sem_unlink(FIFO_SEM_NAME);
    munmap(shm_ptr, sizeof(patient_shm_t));
    shm_unlink(SHM_NAME);
    unlink(SERVER_FIFO);
    return 0;
}

