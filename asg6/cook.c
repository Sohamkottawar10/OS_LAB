#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <time.h>

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

int last_time;
int last_time_cook;
char last_cook_name;
int global_shm;

// Semaphore operations
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

void print_cook_ending() {
    if(last_time_cook == last_time) printf("Cook %c:Leaving\n", last_cook_name);
}

// Function to display current time
void print_time(int minutes) {
    int hour = 11 + minutes / 60;
    int minute = minutes % 60;
    char am_pm = (hour < 12) ? 'a' : 'p';
    // int last_time = minutes;
    if (hour > 12) hour -= 12;
    printf("[%d:%02d %cm] ", hour, minute, am_pm);
}

// Semaphore operations
void sem_wait(int semid, int sem_num) {
    struct sembuf sb = {sem_num, -1, 0};
    if (semop(semid, &sb, 1) == -1) {
        print_time(last_time);
        print_cook_ending();
        exit(1);
    }
}

void sem_signal(int semid, int sem_num) {
    struct sembuf sb = {sem_num, 1, 0};
    if (semop(semid, &sb, 1) == -1) {
        print_time(last_time);
        exit(1);
    }
}

// Function to implement cook behavior
void cmain(int cook_id, int shmid, int semid) {
    int *shm = (int *)shmat(shmid, NULL, 0);
    if (shm == (int *)-1) {
        perror("shmat");
        exit(1);
    }

    // char cook_name = (cook_id == 0) ? 'C' : 'D';
    
    // Print cook is ready
    sem_wait(semid, MUTEX);
    int curr_time = shm[TIME_INDEX];
    print_time(curr_time);
    if (cook_id == 0) {
        printf("Cook C is ready\n");
    } else {
        printf("\tCook D is ready\n");
    }
    sem_signal(semid, MUTEX);

    while (1) {
        // Wait until woken up by a waiter
        sem_wait(semid, COOK_SEM);
        
        // Check if it's after 3:00pm and the cooking queue is empty
        sem_wait(semid, MUTEX);     // wait until no other process is accessing the shared variable time.
        curr_time = shm[TIME_INDEX];
        int pending_orders = shm[PENDING_ORDERS_INDEX];
        
        if (curr_time > 240 && pending_orders == 0) {  // 240 mins = 4 hours after 11am = 3pm
            // Wake up all waiters
            sem_signal(semid, WAITER_U);
            sem_signal(semid, WAITER_V);
            sem_signal(semid, WAITER_W);
            sem_signal(semid, WAITER_X);
            sem_signal(semid, WAITER_Y);
            sem_signal(semid, MUTEX);
            break;
        }

        // If there are no pending orders, continue waiting
        if (pending_orders == 0) {
            sem_signal(semid, MUTEX);
            continue;
        }

        // Get a cooking request from the queue
        int order_index = COOK_QUEUE_START;
        int waiter_id = shm[order_index];
        int customer_id = shm[order_index + 1];
        int customer_count = shm[order_index + 2];
        
        // Remove this order from the queue by shifting the queue
        for (int i = 0; i < (pending_orders - 1) * 3; i++) {
            shm[order_index + i] = shm[order_index + i + 3];
        }
        
        shm[PENDING_ORDERS_INDEX]--;
        
        // Print starting order preparation
        print_time(curr_time);
        if (cook_id == 0) {
            printf("Cook C: Preparing order (Waiter %c, Customer %d, Count %d)\n", 
                   'U' + waiter_id, customer_id, customer_count);
        } else {
            printf("\tCook D: Preparing order (Waiter %c, Customer %d, Count %d)\n", 
                   'U' + waiter_id, customer_id, customer_count);
        }
        sem_signal(semid, MUTEX);
        
        // Cook prepares food (5 minutes per person)
        int cook_time = 5 * customer_count;
        usleep(cook_time * TIME_SCALE);
        
        // Update the time after cooking
        sem_wait(semid, MUTEX);
        int new_time = curr_time + cook_time;
        if (new_time > shm[TIME_INDEX]) {
            shm[TIME_INDEX] = new_time;
        }
        
        // Store the customer ID in the waiter's FR area
        int waiter_area_start = WAITER_U_START + waiter_id * 200;
        shm[waiter_area_start] = customer_id;  // FR area
        
        // Notify the waiter that food is ready
        print_time(shm[TIME_INDEX]);
        if (cook_id == 0) {
            last_cook_name = 'C';
            last_time_cook = shm[TIME_INDEX];
            printf("Cook C: Prepared order (Waiter %c, Customer %d, Count %d)\n", 
                   'U' + waiter_id, customer_id, customer_count);
        } else {
            last_time_cook = shm[TIME_INDEX];
            last_cook_name = 'D';
            printf("\tCook D: Prepared order (Waiter %c, Customer %d, Count %d)\n", 
                   'U' + waiter_id, customer_id, customer_count);
        }
        if(shm[TIME_INDEX] > last_time){
            last_time = shm[TIME_INDEX];
        }
        sem_signal(semid, MUTEX);
        
        // Signal the waiter
        sem_signal(semid, WAITER_U + waiter_id);
    }
    
    // Detach from shared memory
    if (shmdt(shm) == -1) {
        perror("shmdt");
        exit(1);
    }
    
    exit(0);
}

int main() {
    // Create a key for shared memory and semaphores
    key_t key = ftok("./cook", 'R');
    if (key == -1) {
        perror("ftok");
        exit(1);
    }
    
    // Create shared memory
    int shmid = shmget(key, SHM_SIZE * sizeof(int), IPC_CREAT | 0666);
    if (shmid == -1) {
        perror("shmget");
        exit(1);
    }
    
    // Attach to shared memory
    int *shm = (int *)shmat(shmid, NULL, 0);
    if (shm == (int *)-1) {
        perror("shmat");
        exit(1);
    }
    
    // Initialize shared memory
    shm[TIME_INDEX] = 0;  // Time is 11:00am
    shm[EMPTY_TABLES_INDEX] = 10;  // 10 empty tables
    shm[NEXT_WAITER_INDEX] = 0;  // First waiter is U (index 0)
    shm[PENDING_ORDERS_INDEX] = 0;  // No pending orders initially
    
    // Create semaphores (1 mutex, 1 cook, 5 waiters, and space for customer semaphores)
    int semid = semget(key, 207, IPC_CREAT | 0666);  // 7 + space for 200 customers
    if (semid == -1) {
        perror("semget");
        exit(1);
    }
    
    // Initialize semaphores
    union semun arg;
    arg.val = 1;  // Binary semaphore for mutex
    if (semctl(semid, MUTEX, SETVAL, arg) == -1) {
        perror("semctl mutex");
        exit(1);
    }
    
    arg.val = 0;  // Initially no cook or waiter is woken up
    if (semctl(semid, COOK_SEM, SETVAL, arg) == -1 ||
        semctl(semid, WAITER_U, SETVAL, arg) == -1 ||
        semctl(semid, WAITER_V, SETVAL, arg) == -1 ||
        semctl(semid, WAITER_W, SETVAL, arg) == -1 ||
        semctl(semid, WAITER_X, SETVAL, arg) == -1 ||
        semctl(semid, WAITER_Y, SETVAL, arg) == -1) {
        perror("semctl cook/waiter");
        exit(1);
    }
    
    // Initialize customer semaphores (if needed)
    for (int i = 0; i < 200; i++) {
        arg.val = 0;
        if (semctl(semid, CUSTOMER_START + i, SETVAL, arg) == -1) {
            perror("semctl customer");
            exit(1);
        }
    }
    
    // Detach from shared memory
    if (shmdt(shm) == -1) {
        perror("shmdt");
        exit(1);
    }
    
    // Create the two cooks
    pid_t pid_c, pid_d;
    
    pid_c = fork();
    if (pid_c == -1) {
        perror("fork cook C");
        exit(1);
    } else if (pid_c == 0) {
        // Child process for cook C
        cmain(0, shmid, semid);
        // Never returns
    }
    
    pid_d = fork();
    if (pid_d == -1) {
        perror("fork cook D");
        exit(1);
    } else if (pid_d == 0) {
        // Child process for cook D
        cmain(1, shmid, semid);
        // Never returns
    }
    
    // Parent waits for the cooks to terminate
    waitpid(pid_c, NULL, 0);
    waitpid(pid_d, NULL, 0);
    
    return 0;
}