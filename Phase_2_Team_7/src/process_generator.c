#include "../include/headers.h"
#include "../include/Defined_DS.h"
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <sys/msg.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h> 

// Function to clear resources on termination
void clearResources(int signum);
void ProcessCompleted(int signum);
void GenerateRequestFile(Process p);

// Global variables
int msgQid;
Process *processes;
int total_runtime = 0;
int scheduler_pid;
int clk_pid;
int CompletedByScheduler = 0;
int status = 0;

int main(int argc, char *argv[])
{
    signal(SIGINT, clearResources); // Handle termination signal
    srand(time(NULL)); // Initialize random seed for requests

    if (argc < 1)
    {
        fprintf(stderr, "Usage: %s <processes_file> \n", argv[0]);
        return EXIT_FAILURE;
    }

    // Read input file to count the number of processes
    FILE *file = fopen(argv[1], "r");
    if (!file)
    {
        perror("Error opening processes.txt");
        return EXIT_FAILURE;
    }

    int process_count = 0;
    size_t len = 0;
    char *line = NULL;

    while (getline(&line, &len, file) != -1)
    {
        if (line[0] == '#') // Ignore comments
            continue;
        process_count++;
    }

    free(line);
    fclose(file);

    if (process_count == 0)
    {
        fprintf(stderr, "No valid processes found in input file.\n");
        return EXIT_FAILURE;
    }

    // Allocate memory for processes
    processes = (Process *)malloc(process_count * sizeof(Process));
    if (!processes)
    {
        perror("Error allocating memory for processes");
        return EXIT_FAILURE;
    }

    // Populate process array from the input file
    file = fopen(argv[1], "r");
    if (!file)
    {
        perror("Error reopening processes.txt");
        free(processes);
        return EXIT_FAILURE;
    }

    int i = 0;
    line = NULL;
    len = 0;

    while (getline(&line, &len, file) != -1)
    {
        if (line[0] == '#')
            continue;

        int temp_base;
        if (sscanf(line, "%d %d %d %d %d %d %d",
                &processes[i].id,
                &processes[i].arrival_time,
                &processes[i].runtime,
                &processes[i].priority,
                &processes[i].dependencyId,
                &temp_base, 
                &processes[i].memsize) == 7)
        {
            total_runtime += processes[i].runtime;
            i++;
        }
    }

    if (process_count > 0)
    {
        total_runtime += processes[0].arrival_time;
    }

    free(line);
    fclose(file);

    // 2. Ask the user for the chosen scheduling algorithm
    int algo_num;
    fprintf(stdout, "Please enter the algorithm you want to use ([1] Round Robin [2] SRTN [3] HPF):\n");
    scanf("%d", &algo_num);

    while (algo_num > 3 || algo_num < 1) {
        fprintf(stdout, "Please enter a valid number([1] Round Robin [2] SRTN [3] HPF):\n");
        scanf("%d", &algo_num);
    }
        
    int quantum;
    if (algo_num == 1)
    {
        do
        {
            fprintf(stdout, "Please enter your preferred quantum:\n");
            scanf("%d", &quantum);
        } while (quantum < 0);
    }

    // 3. Initiate and create the scheduler and clock processes.
    char quantum_str[10];
    if (algo_num == 1)
        sprintf(quantum_str, "%d", quantum);

    char process_count_str[10];
    sprintf(process_count_str, "%d", process_count);

    char algo_num_str[10];
    sprintf(algo_num_str, "%d", algo_num);

    clk_pid = fork();
    if (clk_pid == -1)
    {
        perror("fork clk failed");
        exit(EXIT_FAILURE);
    }
    if (clk_pid == 0)
    {
        execl("./clk.out", "./clk.out", NULL);
        perror("Clock execution failed");
        exit(EXIT_FAILURE);
    }

    sleep(1); // Wait for clock to start

    scheduler_pid = fork();
    if (scheduler_pid == -1)
    {
        perror("fork scheduler failed");
        kill(clk_pid, SIGINT);
        exit(EXIT_FAILURE);
    }
    if (scheduler_pid == 0)
    {
        if (algo_num == 1)
            execl("./scheduler.out", "./scheduler.out", algo_num_str, process_count_str, quantum_str, NULL);
        else
            execl("./scheduler.out", "./scheduler.out", algo_num_str, process_count_str, NULL);
        perror("Scheduler execution failed");
        exit(EXIT_FAILURE);
    }

    // Message queue setup
    key_t msgkey = ftok("keyfile", 'R');
    msgQid = msgget(msgkey, 0666 | IPC_CREAT);
    if (msgQid == -1)
    {
        perror("Failed to create message queue");
        free(processes);
        destroyClk(true);
        return EXIT_FAILURE;
    }

    // 4. Initialize clock
    initClk();

    // 5. Send processes
    struct msgbuff newMessage;

    for (int k = 0; k < process_count; k++)
    {
        while (processes[k].arrival_time > getClk());
        
        GenerateRequestFile(processes[k]);
        
        newMessage.p = processes[k];
        newMessage.mtype = 1; // Ensure mtype is set if not set inside Process struct
        
        if (msgsnd(msgQid, &newMessage, sizeof(newMessage.p), 0) == -1)
        {
            perror("Error sending process to scheduler");
        }
        else
        {
            printf("Sent Process %d to scheduler at time %d\n", processes[k].id, getClk());
        }
    }

    // Wait for scheduler to finish its job completely
    waitpid(scheduler_pid, &status, 0);

    msgctl(msgQid, IPC_RMID, NULL);
    destroyClk(true);
    if (processes) free(processes);

    return EXIT_SUCCESS;
}

void clearResources(int signum)
{    
    printf("\nCleaning up resources as Process generator...\n");

    msgctl(msgQid, IPC_RMID, NULL);

    if (processes)
        free(processes);

    destroyClk(true);
    
    kill(scheduler_pid, SIGKILL);
    kill(clk_pid, SIGKILL);

    exit(EXIT_SUCCESS);
}

void ProcessCompleted(int signum)
{
    CompletedByScheduler++;
    printf("sigusr1 received\n");
}

void GenerateRequestFile(Process p)
{
    char Filename[32];
    sprintf(Filename, "request_%d.txt", p.id);
    FILE *f = fopen(Filename, "w");
    if (f == NULL) return;

    fprintf(f, "#time\t#addressInBinary\t#r/w\n");

    int NumOfRequests = 5 * p.runtime; 
    if(NumOfRequests > 200) NumOfRequests = 200; 

    for (int i = 0; i < NumOfRequests; i++)
    {
        int time = (rand() % p.runtime) + 1; 
        int address = rand() % p.memsize;
        char rw = (rand() % 2 == 0) ? 'r' : 'w';
        fprintf(f, "%d\t%d\t%c\n", time, address, rw);
    }
    fclose(f);
}