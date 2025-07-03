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

// For waiter area in shared memory
#define FR_INDEX 0
#define PO_INDEX 1
#define QUEUE_START 2

char waiter_name_gb;

int last_time;
int last_idx = 1999;

// Semaphore operations
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

int *global_shm = NULL; // Global pointer to shared memory

void print_space(char waiter_name){
    if(waiter_name == 'V') printf("  ");
    else if(waiter_name == 'W') printf("    ");
    else if(waiter_name == 'X') printf("      ");
    else if(waiter_name == 'Y') printf("        ");
}


// Function to display current time
void print_time(int minutes) {
    int hour = 11 + minutes / 60;
    int minute = minutes % 60;
    char am_pm = (hour < 12) ? 'a' : 'p';
    if (hour > 12) hour -= 12;
    printf("[%d:%02d %cm] ", hour, minute, am_pm);
}

void sema_wait(int semid, int sem_num) {
    struct sembuf sb = {sem_num, -1, 0};
    if (semop(semid, &sb, 1) == -1) {
        // Get the current time from shared memory before exiting
        if (global_shm != NULL) {
            last_time = global_shm[last_idx];
            print_time(last_time);
            printf("Waiter %c: Leaving (no more customer to serve)\n", waiter_name_gb);
        } else {
            printf("Waiter %c: Leaving (no more customer to serve))\n", waiter_name_gb);
        }
        exit(1);
    }
}

void sema_signal(int semid, int sem_num) {
    struct sembuf sb = {sem_num, 1, 0};
    if (semop(semid, &sb, 1) == -1) {
        // Get the current time from shared memory before exiting
        if (global_shm != NULL) {
            last_time = global_shm[last_idx];
            print_time(last_time);
            printf("Waiter %c: Leaving (no more customer to serve)\n", waiter_name_gb);
        } else {
            printf("Waiter %c: Leaving (no more customer to serve)\n", waiter_name_gb);
        }
        exit(1);
    }
}

// Function to implement waiter behavior
void wmain(int waiter_id, int shmid, int semid) {
    int *shm = (int *)shmat(shmid, NULL, 0);
    if (shm == (int *)-1) {
        perror("shmat");
        exit(1);
    }
    

    char waiter_name = 'U' + waiter_id;
    int waiter_area_start = WAITER_U_START + waiter_id * 200;
    waiter_name_gb = waiter_name;
    
    // Print waiter is ready
    sema_wait(semid, MUTEX);
    int curr_time = shm[TIME_INDEX];
    print_time(curr_time);
    print_space(waiter_name);
    printf("Waiter %c is ready\n", waiter_name);
    sema_signal(semid, MUTEX);

    while (1) {
        // Wait until woken up by a cook or a customer
        sema_wait(semid, WAITER_U + waiter_id);
        sema_wait(semid, MUTEX);
        curr_time = shm[TIME_INDEX];
        
        // Check if it's after 3:00pm and no more customers
        if (curr_time > 240 && shm[waiter_area_start + PO_INDEX] == 0 && shm[waiter_area_start + FR_INDEX] == 0) {
            print_time(curr_time);
            print_space(waiter_name);
            printf("Waiter %c: Time is after 3:00pm, no pending orders, shift ending\n", waiter_name);
            sema_signal(semid, MUTEX);
            break;
        }
        
        // If a signal from a cook is pending (FR is not 0)
        if (shm[waiter_area_start + FR_INDEX] != 0) {
            int customer_id = shm[waiter_area_start + FR_INDEX];
            shm[waiter_area_start + FR_INDEX] = 0;  // Reset FR
            if(shm[last_idx] < curr_time){
                shm[last_idx] = curr_time;
                last_time = curr_time;
            }
            
            print_time(curr_time);
            print_space(waiter_name);
            printf("Waiter %c: Serving food to customer %d\n", waiter_name, customer_id);
            sema_signal(semid, MUTEX);
            
            // Signal the customer that food is ready
            sema_signal(semid, CUSTOMER_START + customer_id - 1);
        }
        // If a signal from a new customer is pending (PO is not 0)
        else if (shm[waiter_area_start + PO_INDEX] > 0) {
            // Read details of the customer from the waiter's queue
            int queue_index = waiter_area_start + QUEUE_START;
            int customer_id = shm[queue_index];
            int customer_count = shm[queue_index + 1];
            
            // Remove this customer from the queue by shifting the queue
            for (int i = 0; i < (shm[waiter_area_start + PO_INDEX] - 1) * 2; i++) {
                shm[queue_index + i] = shm[queue_index + i + 2];
            }
            
            shm[waiter_area_start + PO_INDEX]--;
            sema_signal(semid, MUTEX);
            
            // Take order from the customer (1 minute)
            usleep(1 * TIME_SCALE);
            
            // Update time after taking order
            sema_wait(semid, MUTEX);
            int new_time = curr_time + 1;
            if (new_time > shm[TIME_INDEX]) {
                shm[TIME_INDEX] = new_time;
            }
            curr_time = shm[TIME_INDEX];
            
            // Add the order to the cooks' queue
            int order_index = COOK_QUEUE_START + shm[PENDING_ORDERS_INDEX] * 3;
            shm[order_index] = waiter_id;
            shm[order_index + 1] = customer_id;
            shm[order_index + 2] = customer_count;
            shm[PENDING_ORDERS_INDEX]++;
            
            print_time(curr_time);
            print_space(waiter_name);
            waiter_name_gb = waiter_name;
            printf("Waiter %c: Placed order for customer %d\n", waiter_name, customer_id);
            last_time = curr_time;
            sema_signal(semid, MUTEX);
            
            // Signal a cook that a new order is available
            sema_signal(semid, COOK_SEM);
            
            // Signal the customer that the order has been placed
            sema_signal(semid, CUSTOMER_START + customer_id - 1);
        } else {
            // No work to do, just release mutex
            sema_signal(semid, MUTEX);
        }
    }
    
    print_time(shm[TIME_INDEX]);
    print_space(waiter_name);
    printf("Waiter %c: Shift ended\n", waiter_name);
    
    // Detach from shared memory
    if (shmdt(shm) == -1) {
        perror("shmdt");
        exit(1);
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
        printf("Hui\n");
        perror("shmget");
        exit(1);
    }
    
    // Get the semaphores
    int semid = semget(key, 207, 0666);
    if (semid == -1) {
        perror("semget");
        exit(1);
    }
    
    // Create the five waiters
    pid_t pid_u, pid_v, pid_w, pid_x, pid_y;
    
    pid_u = fork();
    if (pid_u == -1) {
        perror("fork waiter U");
        exit(1);
    } else if (pid_u == 0) {
        // Child process for waiter U
        wmain(0, shmid, semid);
        // Never returns
    }
    
    pid_v = fork();
    if (pid_v == -1) {
        perror("fork waiter V");
        exit(1);
    } else if (pid_v == 0) {
        // Child process for waiter V
        wmain(1, shmid, semid);
        // Never returns
    }
    
    pid_w = fork();
    if (pid_w == -1) {
        perror("fork waiter W");
        exit(1);
    } else if (pid_w == 0) {
        // Child process for waiter W
        wmain(2, shmid, semid);
        // Never returns
    }
    
    pid_x = fork();
    if (pid_x == -1) {
        perror("fork waiter X");
        exit(1);
    } else if (pid_x == 0) {
        // Child process for waiter X
        wmain(3, shmid, semid);
        // Never returns
    }
    
    pid_y = fork();
    if (pid_y == -1) {
        perror("fork waiter Y");
        exit(1);
    } else if (pid_y == 0) {
        // Child process for waiter Y
        wmain(4, shmid, semid);
        // Never returns
    }
    
    // Parent waits for all waiters to terminate
    waitpid(pid_u, NULL, 0);
    waitpid(pid_v, NULL, 0);
    waitpid(pid_w, NULL, 0);
    waitpid(pid_x, NULL, 0);
    waitpid(pid_y, NULL, 0);
    
    return 0;
}