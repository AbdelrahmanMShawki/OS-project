#include <math.h>
#include <limits.h>
#include "../include/headers.h"
#include "../include/MMU.h"
#include "../include/Defined_DS.h"

void closeScheduler(int sig_num);
void addToLog(struct PCB pbcblock, int time);
void addToPerf(struct PCB *pcb, int process_count);

void RR(int process_count, int quantum);

static void ToBin(int virtual_address, char *out, size_t out_sz);
static void logPageFault(int pid, const char *virtual_address);
static void logFreePage(int frame);
static void logPageFaultInt(int pid, int virtual_address);
static void logPageLoaded(int time, int page, int pid, int frame);
static void logSwapOut(int frame);


FILE *logFile;
FILE *perfFile;
FILE *memoryLog;

key_t msgkey;
int attach;
int memQid;

struct PCB *RR_pcb;

double util = 0;
int finish;

int main(int argc, char *argv[]) 
{
    initClk();
    signal(SIGINT, closeScheduler);

    key_t memkey = ftok("keyfile", 'm');
    memQid = msgget(memkey, 0666 | IPC_CREAT);

    msgkey = ftok("keyfile", 'R');
    attach = msgget(msgkey, 0666 | IPC_CREAT);
    if (attach == -1)
    {
        perror("message queue not attached");
        exit(1);
    }

    logFile = fopen("scheduler.log", "w");
    perfFile = fopen("scheduler.perf", "w");
    memoryLog = fopen("memory.log", "w"); 

    if (logFile == NULL || perfFile == NULL || memoryLog == NULL)
    {
        perror("Error opening output files");
        msgctl(attach, IPC_RMID, NULL);
        exit(1);
    }

    setbuf(memoryLog, NULL); 
    setbuf(logFile, NULL);

    MMU_init();

    fprintf(logFile, "#At time x process y state arr w total z remain y wait k\n");

    int algo = atoi(argv[1]);
    int processes_count = atoi(argv[2]);
    int quantum = atoi(argv[3]);

    switch (algo)
    {
    case 1:
        RR(processes_count, quantum);
        finish = getClk();
        addToPerf(RR_pcb, processes_count);
        break;
    default:
        return EXIT_FAILURE;
    }

    closeScheduler(0);
    return 0;
}


//////////////// Round Robin //////////////////
CircularQueue *RR_queue;
int RR_completed = 0;
struct Process RR_runningProcess = {0, 0, 0, 0};
bool Run = false;
int RR_startTime = 0;
int RR_current_pid = -1;
int pcbidx = 0; 

void RR(int process_count, int quantum)
{
    RR_queue = createCircularQueue(process_count + 100);
    RR_pcb = (struct PCB *)malloc(sizeof(struct PCB) * process_count);
    memset(RR_pcb, 0, sizeof(struct PCB) * process_count);

    for (int i = 0; i < process_count; i++)
        initPageTable(&RR_pcb[i]);
    
    struct msgbuff newMessage;

    while (RR_completed < process_count)
    {
        while (msgrcv(attach, &newMessage, sizeof(struct msgbuff) - sizeof(long), 0, IPC_NOWAIT) != -1)
        {
            int idx = newMessage.p.id - 1;
            RR_pcb[idx].p = newMessage.p;
            RR_pcb[idx].remainingtime = newMessage.p.runtime;
            RR_pcb[idx].processstate = ready;
            enqueueCircularQueue(RR_queue, newMessage.p);

            short usedFree = false; int victim = -1; short victimDirty = false;
            int frame = AllocatePage(&RR_pcb[idx], 0, false, &usedFree, &victim, &victimDirty);
            logPageLoaded(getClk(), 0, newMessage.p.id, frame);
        }

        if (Run) 
        {
            struct msg_mem memReq;
            if (msgrcv(memQid, &memReq, sizeof(memReq) - sizeof(long), 0, IPC_NOWAIT) != -1)
            {
                if (memReq.pid_sender == RR_pcb[pcbidx].current_pid)
                {
                    int virtual_address = memReq.virtual_addr;
                    bool isWrite = (memReq.req_type == 'w');
                    int physical_address = translateAddress(&RR_pcb[pcbidx], virtual_address, isWrite);
                    
                    if (physical_address == -1) {
                        logPageFaultInt(RR_runningProcess.id, virtual_address);
                        kill(RR_pcb[pcbidx].current_pid, SIGSTOP);
                        RR_pcb[pcbidx].processstate = stopped;

                        short usedFree = false; int victimFrame = -1; short victimDirty = false;
                        int pageNum = virtual_address / 16; 
                        int frame = AllocatePage(&RR_pcb[pcbidx], pageNum, isWrite, &usedFree, &victimFrame, &victimDirty);

                        if (usedFree) logFreePage(frame); else logSwapOut(victimFrame);

                        int delay = (victimDirty) ? 20 : 10;
                        int start_wait = getClk();
                        while (getClk() - start_wait < delay); 

                        logPageLoaded(getClk(), pageNum, RR_runningProcess.id, frame);
                        
                        struct msg_mem reply;
                        reply.mtype = memReq.pid_sender;
                        reply.virtual_addr = frame * 16 + (virtual_address % 16);
                        msgsnd(memQid, &reply, sizeof(reply) - sizeof(long), 0);

                        enqueueCircularQueue(RR_queue, RR_runningProcess);
                        Run = false;
                    }
                    else {
                        struct msg_mem reply;
                        reply.mtype = memReq.pid_sender;
                        reply.virtual_addr = physical_address;
                        msgsnd(memQid, &reply, sizeof(reply) - sizeof(long), 0);
                    }
                }
            }
        }

        if (Run) {
            int status;
            pid_t result = waitpid(RR_pcb[pcbidx].current_pid, &status, WNOHANG);
            if (result != 0) { 
                RR_pcb[pcbidx].finishedtime = getClk();
                RR_pcb[pcbidx].processstate = finished;
                RR_pcb[pcbidx].TA = RR_pcb[pcbidx].finishedtime - RR_runningProcess.arrival_time;
                RR_pcb[pcbidx].WTA = (double)RR_pcb[pcbidx].TA / RR_runningProcess.runtime;
                RR_pcb[pcbidx].remainingtime = 0;
                RR_completed++;
                addToLog(RR_pcb[pcbidx], getClk());
                Run = false;
                printf("Process %d finished. Total completed: %d/%d\n", RR_runningProcess.id, RR_completed, process_count);
            }
            else if (getClk() - RR_startTime >= quantum) {
                kill(RR_pcb[pcbidx].current_pid, SIGSTOP);
                RR_pcb[pcbidx].processstate = stopped;
                RR_pcb[pcbidx].remainingtime -= quantum;
                addToLog(RR_pcb[pcbidx], getClk());
                enqueueCircularQueue(RR_queue, RR_runningProcess);
                Run = false;
            }
        }

        if (!isCircularQueueEmpty(RR_queue) && !Run)
        {
            RR_runningProcess = dequeueCircularQueue(RR_queue);
            pcbidx = RR_runningProcess.id - 1;
            Run = true;
            RR_startTime = getClk();
            
            if (RR_pcb[pcbidx].processstate == stopped) {
                RR_pcb[pcbidx].processstate = resumed;
                kill(RR_pcb[pcbidx].current_pid, SIGCONT);
            } else {
                pid_t pid = fork();
                if (pid == 0) {
                    char rt[10], id[10];
                    sprintf(rt, "%d", RR_pcb[pcbidx].remainingtime);
                    sprintf(id, "%d", RR_runningProcess.id);
                    execl("./process.out", "./process.out", rt, id, NULL);
                    exit(1);
                }
                RR_pcb[pcbidx].current_pid = pid;
                RR_pcb[pcbidx].waitingtime = getClk() - RR_runningProcess.arrival_time;
                RR_pcb[pcbidx].processstate = running;
            }
            addToLog(RR_pcb[pcbidx], getClk());
        }
    }
}


void addToLog(struct PCB pcblock, int time)
{
    int process_id = pcblock.p.id;
    enum state state_num = pcblock.processstate;
    int arrival = pcblock.p.arrival_time;
    int runtime = pcblock.p.runtime;
    int remaining_time = pcblock.remainingtime;
    int wait_time = pcblock.waitingtime;
    double TA = pcblock.TA;
    double WTA = pcblock.WTA;
    char *state;
    switch (state_num)
    {
    case running:
        state = "Started";
        break;
    case stopped:
        state = "Stopped";
        break;
    case resumed:
        state = "Resumed";
        break;
    case finished:
        state = "Finished";
        break;
    default:
        return;
    }

    fprintf(logFile, "At time %d process %d %s arr %d total %d remain %d wait %d", time, process_id, state, arrival, runtime, remaining_time, wait_time);

    if (state == "Finished")
        fprintf(logFile, " TA %.0f WTA %.2f", TA, WTA);

    fprintf(logFile, "\n");
}

void addToPerf(struct PCB *pcb, int process_count)
{
    double avgWait = 0, avgWTA = 0, stdWTA = 0;
    int arrive;
    int totalrun;
    finish = pcb[0].finishedtime;
    arrive = pcb[0].p.arrival_time;
    for (int i = 0; i < process_count; i++)
    {
        avgWait += pcb[i].waitingtime;
        // totalexc += pcb[i].p.runtime; // Warning unused
        avgWTA += pcb[i].WTA;

        if (pcb[i].finishedtime > finish)
            finish = pcb[i].finishedtime;
    }

    avgWait /= (double)process_count;
    avgWTA /= (double)process_count;

    for (int i = 0; i < process_count; i++)
        stdWTA += pow(pcb[i].WTA - avgWTA, 2);

    stdWTA /= (double)process_count;
    stdWTA = sqrt(stdWTA);
    util = (util / finish) * 100;

    fprintf(perfFile, "CPU utilization = %.2f%%\nAvg WTA = %.2f\nAvg Waiting = %.2f\nStd WTA = %.2f\n", util, avgWTA, avgWait, stdWTA);
}

void closeScheduler(int sig_num)
{
    printf("Cleaning up resources as Scheduler\n");
    if (logFile) fclose(logFile);
    if (perfFile) fclose(perfFile);
    if (memoryLog) fclose(memoryLog);

    if (attach != -1)
        msgctl(attach, IPC_RMID, NULL);

    destroyClk(false);
    destroyCircularQueue(RR_queue);

    if (RR_pcb) free(RR_pcb);
    exit(0);
}

static void ToBin(int virtual_address, char *out, size_t out_sz)
{
    if (out_sz == 0) return;

    if (virtual_address <= 0) {
        out[0] = '0';
        out[1] = '\0';
        return;
    }

    char tmp[32];
    int idx = 0;
    unsigned int x = (unsigned int)virtual_address;

    while (x > 0 && idx < (int)sizeof(tmp) - 1) {
        tmp[idx++] = (char)('0' + (x & 1u));
        x >>= 1;
    }
    
    size_t n = (idx < (int)(out_sz - 1)) ? (size_t)idx : (out_sz - 1);
    for (size_t i = 0; i < n; ++i)
        out[i] = tmp[idx - 1 - (int)i];

    out[n] = '\0';
}

static void logPageFault(int pid, const char *virtual_address)
{
    fprintf(memoryLog, "PageFault upon VA %s from process %d\n", virtual_address, pid);
    fflush(memoryLog); 
}

static void logPageFaultInt(int pid, int virtual_address)
{
    char buf[32];
    ToBin(virtual_address, buf, sizeof(buf));
    logPageFault(pid, buf);
}

static void logFreePage(int frame)
{
    fprintf(memoryLog, "Free Physical page %d allocated\n", frame);
    fflush(memoryLog); 
}

static void logSwapOut(int frame)
{
    fprintf(memoryLog, "#Swapping out page %d to disk\n", frame); 
    fflush(memoryLog); 
}

void logPageLoaded(int time, int page, int pid, int frame)
{
    fprintf(memoryLog, "At time %d page %d for process %d is loaded into memory page %d.\n", time, page, pid, frame);
    fflush(memoryLog); 
}


//////////////Shortest Remaining Time Next //////////////
/*void SRTN(int process_count)
{
    Process_queue = createPriorityQueue(process_count);

    pcb = (struct PCB *)malloc(sizeof(struct PCB) * process_count);
    memset(pcb, 0, sizeof(struct PCB) * process_count);

    int completed_local = 0;
    int running_index = -1;
    bool isRunning = false;

    pausetime = 0;
    util = 0;

    while (completed_local < process_count)
    {
        // 1) Receive newly arrived processes
        while (msgrcv(attach, &newMessage, sizeof(newMessage.p), 0, IPC_NOWAIT) != -1)
        {
            Process p = newMessage.p;
            int idx = p.id - 1;
            printf("Process %d received at time %d\n", p.id, getClk());

            // initialize PCB for this process
            pcb[idx].p = p;
            pcb[idx].remainingtime = p.runtime;
            pcb[idx].processstate = ready;
            pcb[idx].current_pid = -1;
            pcb[idx].executiontime = 0;
            pcb[idx].waitingtime = 0;

            // insert keyed by runtime (remaining)
            insertRuntimePriorityQueue(Process_queue, p);
        }

        // 2) Check if the currently running process finished
        if (isRunning && running_index >= 0 && pcb[running_index].current_pid > 0)
        {
            int status;
            pid_t w = waitpid(pcb[running_index].current_pid, &status, WNOHANG);
            if (w > 0 && WIFEXITED(status))
            {
                // finished normally
                pcb[running_index].remainingtime = 0;
                pcb[running_index].finishedtime = getClk();
                pcb[running_index].processstate = finished;

                pcb[running_index].TA = pcb[running_index].finishedtime - pcb[running_index].p.arrival_time;
                pcb[running_index].WTA = (double)pcb[running_index].TA / (double)pcb[running_index].p.runtime;

                addToLog(pcb[running_index], getClk());

                completed_local++;
                isRunning = false;
                running_index = -1;
                // continue to next iteration to pick new process immediately
                continue;
            }
        }

        // 3) If there's someone in the queue, consider scheduling / preemption
        if (Process_queue->size > 0)
        {
            // get the candidate with smallest remaining (runtime field holds remaining when inserted)
            Process next = removeRuntimePriorityQueue(Process_queue);
            int next_idx = next.id - 1;

            if (!isRunning)
            {
                // No running process — start this candidate
                util += getClk() - pausetime;

                // If candidate was previously forked and stopped -> resume
                if (pcb[next_idx].processstate == stopped && pcb[next_idx].current_pid > 0)
                {
                    pcb[next_idx].processstate = resumed;
                    pcb[next_idx].executiontime = getClk();
                    kill(pcb[next_idx].current_pid, SIGCONT);
                    addToLog(pcb[next_idx], getClk());
                    isRunning = true;
                    running_index = next_idx;
                }
                else
                {
                    // Fork new child with remaining time argument
                    pid_t pid = fork();
                    if (pid == 0)
                    {
                        char rt[16], idstr[16];
                        sprintf(rt, "%d", pcb[next_idx].remainingtime);
                        sprintf(idstr, "%d", next.id);
                        execl("./process.out", "./process.out", rt, idstr, NULL);
                        // if exec fails:
                        perror("execl process.out");
                        exit(1);
                    }
                    else
                    {
                        pcb[next_idx].current_pid = pid;
                        pcb[next_idx].processstate = running;
                        pcb[next_idx].executiontime = getClk();
                        pcb[next_idx].waitingtime = getClk() - pcb[next_idx].p.arrival_time;
                        addToLog(pcb[next_idx], getClk());
                        isRunning = true;
                        running_index = next_idx;
                    }
                }
            }
            else
            {
                // There is a running process: update its remaining time based on elapsed simulated time
                int now = getClk();
                int elapsed = now - pcb[running_index].executiontime;
                if (elapsed > 0)
                {
                    pcb[running_index].remainingtime -= elapsed;
                    if (pcb[running_index].remainingtime < 0)
                        pcb[running_index].remainingtime = 0;
                    // update executiontime to now so next elapsed will be measured correctly
                    pcb[running_index].executiontime = now;
                }

                // Compare candidate's remaining (next.runtime) with running remaining
                int running_rem = pcb[running_index].remainingtime;
                int candidate_rem = next.runtime; // since we inserted next with runtime = remaining

                if (candidate_rem < running_rem)
                {
                    // Preempt running process
                    pausetime = getClk();
                    kill(pcb[running_index].current_pid, SIGSTOP);

                    int st;
                    waitpid(pcb[running_index].current_pid, &st, WUNTRACED); // wait for STOPPED

                    if (WIFSTOPPED(st))
                    {
                        // Update running pcb and requeue it
                        pcb[running_index].processstate = stopped;
                        // remaining already updated above by elapsed
                        addToLog(pcb[running_index], getClk());

                        Process prev = pcb[running_index].p;
                        prev.runtime = pcb[running_index].remainingtime; // push updated remaining
                        insertRuntimePriorityQueue(Process_queue, prev);
                    }
                    else if (WIFEXITED(st))
                    {
                        // It might have finished while stopping — mark finished
                        pcb[running_index].remainingtime = 0;
                        pcb[running_index].finishedtime = getClk();
                        pcb[running_index].processstate = finished;
                        pcb[running_index].TA = pcb[running_index].finishedtime - pcb[running_index].p.arrival_time;
                        pcb[running_index].WTA = (double)pcb[running_index].TA / (double)pcb[running_index].p.runtime;
                        addToLog(pcb[running_index], getClk());
                        completed_local++;
                    }

                    // If candidate was previously stopped (exists in pcb), resume; otherwise fork
                    if (pcb[next_idx].processstate == stopped && pcb[next_idx].current_pid > 0)
                    {
                        pcb[next_idx].processstate = resumed;
                        pcb[next_idx].executiontime = getClk();
                        kill(pcb[next_idx].current_pid, SIGCONT);
                        addToLog(pcb[next_idx], getClk());
                        isRunning = true;
                        running_index = next_idx;
                    }
                    else
                    {
                        pid_t pid = fork();
                        if (pid == 0)
                        {
                            char rt[16], idstr[16];
                            sprintf(rt, "%d", pcb[next_idx].remainingtime);
                            sprintf(idstr, "%d", next.id);
                            execl("./process.out", "./process.out", rt, idstr, NULL);
                            perror("execl process.out");
                            exit(1);
                        }
                        else
                        {
                            pcb[next_idx].current_pid = pid;
                            pcb[next_idx].processstate = running;
                            pcb[next_idx].executiontime = getClk();
                            pcb[next_idx].waitingtime = getClk() - pcb[next_idx].p.arrival_time;
                            addToLog(pcb[next_idx], getClk());
                            isRunning = true;
                            running_index = next_idx;
                        }
                    }
                }
                else
                {
                    // No preemption: put candidate back into the queue and continue
                    insertRuntimePriorityQueue(Process_queue, next);
                }
            }
        }
    }
    // All processes finished
    finish = getClk();
    addToPerf(pcb, process_count);
}*/

////////////// Highest Priority first ////////////

// PriorityQueue *Process_queue;
// int completed;
// struct Process runningProcess = {0, 0, 0, 0};
// int pcbidx = 0; // tracks the current running process index
// int pausetime = 0;

/*void HPF(int process_count)
{
    
    Process_queue = createPriorityQueue(process_count);

    pcb = (struct PCB *)malloc(sizeof(struct PCB) * process_count);
    memset(pcb, 0, sizeof(struct PCB) * process_count);

    for (int i = 0; i < process_count; i++)
    {
        pcb[i].current_pid = -1;
        pcb[i].remainingtime = 0;
        pcb[i].processstate = ready;
        pcb[i].waitingtime = 0;
    }

    int completed_local = 0;
    int running_index = -1;
    bool isRunning = false;

    util = 0;
    pausetime = 0;
    while (completed_local < process_count)
    {
       
        usleep(1000); 
        
        while (msgrcv(attach, &newMessage, sizeof(newMessage.p), 0, IPC_NOWAIT) != -1)
        {
            Process p = newMessage.p;
            int idx = p.id - 1;

            pcb[idx].p = p;
            pcb[idx].remainingtime = p.runtime;
            pcb[idx].processstate = ready;

            Process key = p;
            key.priority = -key.priority;

            insertPriorityPriorityQueue(Process_queue, key);
        }

      
        if (isRunning && running_index >= 0)
        {
            int status;
            pid_t w = waitpid(pcb[running_index].current_pid, &status, WNOHANG);
            if(w==0)
            {
                    struct msg_mem req;
                if (msgrcv(mem_queue_id, &req, sizeof(req) - sizeof(long), 1, IPC_NOWAIT) != -1) 
                {
                int virtualAdd=req.virtual_addr;
                int pageNum=virtualAdd/16;
                int PhysicalAdd=translateAddress(&pcb[running_index],virtualAdd);
                if(PhysicalAdd==-1)//page fault
                {
                    
                    kill(pcb[running_index].current_pid, SIGSTOP);
                    waitpid(pcb[running_index].current_pid, NULL, WUNTRACED);
                    pcb[running_index].processstate = stopped;

                    int start = getClk();
                    while (getClk() - start < 10);
                    int usedFrame=AllocatePage(&pcb[running_index],pageNum);
                    fprintf(mmuLog, "At time %d: Page Fault for Process %d, Page %d, Frame %d\n", getClk(), pcb[running_index].p.id, pageNum, usedFrame);
                    int offset = virtualAdd % 16;
                    int newPhysical = (usedFrame * 16) + offset;
                    struct msg_mem reply;
                    reply.mtype = req.pid_sender;
                    reply.virtual_addr = newPhysical; 
                    msgsnd(mem_queue_id, &reply, sizeof(reply) - sizeof(long), 0);
                    isRunning=false;
                    fflush(mmuLog);
                }
                else
                {
                fprintf(mmuLog, "At time %d: Successful access for Process %d, Page %d, Frame %d\n",getClk(), pcb[running_index].p.id, pageNum, pcb[running_index].pageTable[pageNum].frameNumber);
                struct msg_mem reply;
                reply.mtype = req.pid_sender; //To reply to same PID
                reply.virtual_addr = PhysicalAdd; 
                msgsnd(mem_queue_id, &reply, sizeof(reply) - sizeof(long), 0);
                fflush(mmuLog);
                }
                }
            }
            if (w > 0 && WIFEXITED(status))
            {
                pcb[running_index].remainingtime = 0;
                pcb[running_index].finishedtime = getClk();
                pcb[running_index].processstate = finished;

                pcb[running_index].TA = pcb[running_index].finishedtime - pcb[running_index].p.arrival_time;
                pcb[running_index].WTA = (double)pcb[running_index].TA / pcb[running_index].p.runtime;

                addToLog(pcb[running_index], getClk());

                completed_local++;
                isRunning = false;
                running_index = -1;
                pausetime = getClk();
                continue;
            }
        }

        
        if (Process_queue->size == 0)
        {
            continue;
        }

       
        Process chosen = removePriorityPriorityQueue(Process_queue);
        int next_idx = chosen.id - 1;

      
        if (!isRunning)
        {
            util += getClk() - pausetime;

            if (pcb[next_idx].processstate == stopped)
            {
                pcb[next_idx].processstate = resumed;
                pcb[next_idx].executiontime = getClk();
                kill(pcb[next_idx].current_pid, SIGCONT);
                addToLog(pcb[next_idx], getClk());
            }
            else
            {
                pid_t pid = fork();
                if (pid == 0)
                {
                    char rt[16], id[16];
                    sprintf(rt, "%d", pcb[next_idx].remainingtime);
                    sprintf(id, "%d", chosen.id);
                    execl("./process.out", "./process.out", rt, id, NULL);
                    exit(1);
                }

                pcb[next_idx].current_pid = pid;
                pcb[next_idx].processstate = running;
                pcb[next_idx].executiontime = getClk();
                pcb[next_idx].waitingtime = getClk() - pcb[next_idx].p.arrival_time;
                addToLog(pcb[next_idx], getClk());
            }

            isRunning = true;
            running_index = next_idx;
            continue;
        }

       
        int prio_running = pcb[running_index].p.priority;
        int prio_candidate = pcb[next_idx].p.priority;

        // Update running process remaining time
        int now = getClk();
        int elapsed = now - pcb[running_index].executiontime;
        if (elapsed > 0)
        {
            pcb[running_index].remainingtime -= elapsed;
            if (pcb[running_index].remainingtime < 0)
                pcb[running_index].remainingtime = 0;

            pcb[running_index].executiontime = now;
        }

        if (prio_candidate > prio_running)
        {
            // Preempt
            pausetime = getClk();
            kill(pcb[running_index].current_pid, SIGSTOP);

            int st;
            waitpid(pcb[running_index].current_pid, &st, WUNTRACED);

            if (WIFSTOPPED(st))
            {
                pcb[running_index].processstate = stopped;
                addToLog(pcb[running_index], getClk());

                if (pcb[running_index].remainingtime > 0)
                {
                    Process rekey = pcb[running_index].p;
                    rekey.priority = -rekey.priority;
                    insertPriorityPriorityQueue(Process_queue, rekey);
                }
            }

            util += getClk() - pausetime;

            if (pcb[next_idx].processstate == stopped)
            {
                pcb[next_idx].processstate = resumed;
                pcb[next_idx].executiontime = getClk();
                kill(pcb[next_idx].current_pid, SIGCONT);
                addToLog(pcb[next_idx], getClk());
            }
            else
            {
                pid_t pid = fork();
                if (pid == 0)
                {
                    char rt[16], id[16];
                    sprintf(rt, "%d", pcb[next_idx].remainingtime);
                    sprintf(id, "%d", chosen.id);
                    execl("./process.out", "./process.out", rt, id, NULL);
                    exit(1);
                }

                pcb[next_idx].current_pid = pid;
                pcb[next_idx].processstate = running;
                pcb[next_idx].executiontime = getClk();
                pcb[next_idx].waitingtime = getClk() - pcb[next_idx].p.arrival_time;
                addToLog(pcb[next_idx], getClk());
            }

            running_index = next_idx;
            isRunning = true;
        }
        else
        {
            insertPriorityPriorityQueue(Process_queue, chosen);
        }
    }

    finish = getClk();
}*/
