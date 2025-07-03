#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <time.h>
#include <errno.h>

#define SHM_SIZE 2000
#define TIME_SCALE 100000 // 100ms = 100000 microseconds per minute

// Constants for shared memory organization
#define TIME_INDEX 0
#define EMPTY_TABLES_INDEX 1
#define NEXT_WAITER_INDEX 2
#define PENDING_ORDERS_INDEX 3

// Waiter areas in shared memory
#define WAITER_U_START 100
#define WAITER_V_START 300
#define WAITER_W_START 500
#define WAITER_X_START 700
#define WAITER_Y_START 900
#define COOK_QUEUE_START 1100

// Semaphore indices
#define MUTEX 0
#define COOK_SEM 1
#define WAITER_U 2
#define WAITER_V 3
#define WAITER_W 4
#define WAITER_X 5
#define WAITER_Y 6
#define CUSTOMER_START 7

// For waiter area in shared memory
#define FR_INDEX 0
#define PO_INDEX 1
#define QUEUE_START 2

// Semaphore operations
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

void sem_wait(int semid, int sem_num) {
    struct sembuf sb = {sem_num, -1, 0};
    if (semop(semid, &sb, 1) == -1) {
        perror("semop wait");
        exit(1);
    }
}

void sem_signal(int semid, int sem_num) {
    struct sembuf sb = {sem_num, 1, 0};
    if (semop(semid, &sb, 1) == -1) {
        perror("semop signal");
        exit(1);
    }
}

// Function to display current time
void print_time(int minutes) {
    int hour = 11 + minutes / 60;
    int minute = minutes % 60;
    char am_pm = (hour < 12) ? 'a' : 'p';
    if (hour > 12) hour -= 12;
    printf("[%d:%02d %cm] ", hour, minute, am_pm);
}

// Function to implement customer behavior
void cmain(int customer_id, int arrival_time, int customer_count, int shmid, int semid) {
    int *shm = (int *)shmat(shmid, NULL, 0);
    if (shm == (int *)-1) {
        perror("shmat");
        exit(1);
    }
    
    // Check current time and set arrival time if needed
    sem_wait(semid, MUTEX);         // ********************************************************************************
    if (arrival_time > shm[TIME_INDEX]) {
        shm[TIME_INDEX] = arrival_time;
    }
    int curr_time = shm[TIME_INDEX];
    
    
    // Check if it's after 3:00pm
    if (curr_time > 240) {
        print_time(curr_time);
        printf(" 				Customer %d leaves (late arrival)\n", customer_id);
        sem_signal(semid, MUTEX);
        if (shmdt(shm) == -1) {
            perror("shmdt");
        }
        exit(0);
    }
    
    // Check if any table is empty
    if (shm[EMPTY_TABLES_INDEX] <= 0) {
        print_time(curr_time);
        printf(" 				Customer %d leaves (no empty table)\n", customer_id);
        sem_signal(semid, MUTEX);
        if (shmdt(shm) == -1) {
            perror("shmdt");
        }
        exit(0);
    }

    print_time(curr_time);
    printf(" Customer %d arrives (count = %d)\n", customer_id, customer_count);
    // Use an empty table
    shm[EMPTY_TABLES_INDEX]--;
    
    // Find the waiter to serve
    int waiter_id = shm[NEXT_WAITER_INDEX];
    shm[NEXT_WAITER_INDEX] = (waiter_id + 1) % 5;  // Update next waiter in circular fashion
    int waiter_area_start = WAITER_U_START + waiter_id * 200;
    char waiter_name = 'U' + waiter_id;
    
    // Write to waiter's queue
    int po_index = waiter_area_start + PO_INDEX;
    int queue_index = waiter_area_start + QUEUE_START + (shm[po_index] * 2);
    shm[queue_index] = customer_id;
    shm[queue_index + 1] = customer_count;
    shm[po_index]++;        // stores the number of pending orders.
    
    
    sem_signal(semid, MUTEX);           // .............................................................................

    // wakeup the waiter
    sem_signal(semid, WAITER_U + waiter_id);
    
    // Wait for the waiter to attend
    sem_wait(semid, CUSTOMER_START + customer_id - 1);

    sem_wait(semid, MUTEX);
    int tt = shm[TIME_INDEX];
    sem_signal(semid, MUTEX);

    print_time(tt);
    printf("   Customer %d: Order placed to waiter %c\n", customer_id, waiter_name);
    
    // Wait for food to be served
    sem_wait(semid, CUSTOMER_START + customer_id - 1);
    
    // Food is served, start eating
    sem_wait(semid, MUTEX);
    int curr_time2 = shm[TIME_INDEX];
    int waiting_time = curr_time2 - curr_time;
    print_time(curr_time2);
    printf(" 	  Customer %d: gets food [waiting time = %d]\n", customer_id, waiting_time);
    sem_signal(semid, MUTEX);
    
    // Eat for 30 minutes
    usleep(30 * TIME_SCALE);
    
    // Update time after eating
    sem_wait(semid, MUTEX);
    int new_time = curr_time + 30;
    if (new_time > shm[TIME_INDEX]) {
        shm[TIME_INDEX] = new_time;
    }
    
    // Free the table
    shm[EMPTY_TABLES_INDEX]++;
    
    // print_time(shm[TIME_INDEX]);
    print_time(curr_time2+30);
    printf(" 		  Customer %d: Finished eating, leaving (%d tables available)\n", 
           customer_id, shm[EMPTY_TABLES_INDEX]);
    sem_signal(semid, MUTEX);
    
    // Detach from shared memory
    if (shmdt(shm) == -1) {
        perror("shmdt");
    }
    
    exit(0);
}

int main() {
    // Create a key for shared memory and semaphores (same as cook.c)
    key_t key = ftok("./cook", 'R');
    if (key == -1) {
        perror("ftok");
        exit(1);
    }
    
    // Get the shared memory segment
    int shmid = shmget(key, SHM_SIZE * sizeof(int), 0666);
    if (shmid == -1) {
        perror("shmget");
        exit(1);
    }
    
    // Get the semaphores
    int semid = semget(key, 207, 0666);
    if (semid == -1) {
        perror("semget");
        exit(1);
    }
    
    // Read customer info from file
    FILE *fp = fopen("customers.txt", "r");
    if (fp == NULL) {
        perror("fopen");
        exit(1);
    }
    
    int prev_arrival_time = 0;
    int customer_id, arrival_time, customer_count;
    pid_t *customer_pids = NULL;
    int num_customers = 0;
    
    // Count how many customers there are
    int line_count = 0;
    while (fscanf(fp, "%d %d %d", &customer_id, &arrival_time, &customer_count) == 3) {
        if (customer_id == -1) break;
        line_count++;
    }
    rewind(fp);
    
    // Allocate array for customer PIDs
    customer_pids = (pid_t*)malloc(line_count * sizeof(pid_t));
    if (customer_pids == NULL) {
        perror("malloc");
        exit(1);
    }
    
    // Process customers
    while (fscanf(fp, "%d %d %d", &customer_id, &arrival_time, &customer_count) == 3) {
        if (customer_id == -1) break;
        
        // Wait for the time difference between consecutive customers
        if (arrival_time > prev_arrival_time) {
            int wait_time = arrival_time - prev_arrival_time;
            usleep(wait_time * TIME_SCALE);
            
            // Update the shared memory time
            int *shm = (int *)shmat(shmid, NULL, 0);
            if (shm != (int *)-1) {
                sem_wait(semid, MUTEX);
                if (arrival_time > shm[TIME_INDEX]) {
                    shm[TIME_INDEX] = arrival_time;
                }
                sem_signal(semid, MUTEX);
                shmdt(shm);
            }
        }
        prev_arrival_time = arrival_time;
        
        // Fork a child process for the customer
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork customer");
            exit(1);
        } else if (pid == 0) {
            // Child process for the customer
            cmain(customer_id, arrival_time, customer_count, shmid, semid);
            // Never returns
        } else {
            // Parent process
            customer_pids[num_customers++] = pid;
        }
    }
    
    fclose(fp);
    
    // Wait for all customer processes to terminate
    for (int i = 0; i < num_customers; i++) {
        waitpid(customer_pids[i], NULL, 0);
    }
    
    free(customer_pids);
    
    // Clean up IPC resources
    shmctl(shmid, IPC_RMID, NULL);
    semctl(semid, 0, IPC_RMID);
    
    return 0;
}