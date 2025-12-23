#include "../include/headers.h"
#include <signal.h>

int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        fprintf(stderr, "Usage: %s <remaining_time> <process_id>\n", argv[0]);
        return -1;
    }

    int remainingtime = atoi(argv[1]);
    int proc_id = atoi(argv[2]);

    // Initialize clock
    initClk();

    // Open memory request file
    char filename[32];
    sprintf(filename, "request_%d.txt", proc_id);

    FILE *f = fopen(filename, "r");
    if (!f)
    {
        perror("Cannot open request file");
        destroyClk(false);
        return -1;
    }
    char buffer[100];
    if (fgets(buffer, sizeof(buffer), f) == NULL) 
    {
        fclose(f);
        destroyClk(false);
        return 0;
    }
    // Attach to memory queue
    key_t memkey = ftok("keyfile", 'm');
    int memQid = msgget(memkey, 0666 | IPC_CREAT);

    int req_time;
    int vaddr;
    char rw_type;
    while (fscanf(f, "%d %d %c", &req_time, &vaddr, &rw_type) != EOF)
    {
        
        struct msg_mem req;
        req.mtype = 1;
        req.virtual_addr = vaddr;
        req.pid_sender = getpid();
        req.req_type = rw_type; 

        
        msgsnd(memQid, &req, sizeof(req) - sizeof(long), 0);
        msgrcv(memQid, &req, sizeof(req) - sizeof(long), getpid(), 0);
        
    }

   printf("Process %d: Finished all memory requests.\n", proc_id);
    fclose(f);
    destroyClk(false);
    return 0;
}