#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define null 0

struct processData
{
    int arrivaltime;
    int priority;
    int runningtime;
    int id;
    int dependencyId;
    int memsize;
};

void initializeProcessData(struct processData *processes, int *lastArrival, int i)
{
    processes[i].id = i + 1;
    processes[i].arrivaltime = *lastArrival + rand() % 11; // increasing arrival
    *lastArrival = processes[i].arrivaltime;
    processes[i].runningtime = 1 + rand() % 30;
    processes[i].priority = rand() % 11;
    processes[i].dependencyId = -1;
    processes[i].memsize = 256;
}

void assignProcessDependency(struct processData *processes, int i, int numberOfProcesses)
{
    int depend = rand() % 2;

    if (depend && i > 0)
    {
        int *possible = malloc(numberOfProcesses * sizeof(struct processData));
        int count = 0;

        for (int j = 0; j < i; j++)
        {
            int start = processes[j].arrivaltime;
            int end = processes[j].arrivaltime + processes[j].runningtime;

            if (processes[i].arrivaltime >= start && processes[i].arrivaltime <= end)
            {
                possible[count++] = processes[j].id;
            }
        }

        if (count > 0)
        {
            int randomIndex = rand() % count;
            processes[i].dependencyId = possible[randomIndex];
        }
        else
        {
            processes[i].dependencyId = -1;
        }
    }
    else
    {
        processes[i].dependencyId = -1;
    }
}

void writeProcessInFile(FILE *pFile, struct processData *processes, int i)
{
    fprintf(pFile, "%-5d %-10d %-10d %-10d %-10d %-10d %-20d\n",
            processes[i].id,
            processes[i].arrivaltime,
            processes[i].runningtime,
            processes[i].priority,
            processes[i].dependencyId,
            0, // Base Address (Dummy)
            processes[i].memsize);
}


int main(int argc, char *argv[])
{
    FILE *pFile;
    pFile = fopen("processes.txt", "w");
    if (!pFile)
    {
        printf("Error opening file.\n");
        return 1;
    }

    int no;
    printf("Please enter the number of processes you want to generate: ");
    scanf("%d", &no);

    srand(time(null));

    fprintf(pFile, "%-5s %-10s %-10s %-10s %-10s %-10s %-20s\n","#id", "arrival", "runtime", "priority", "dependency", "base", "memsize");

    struct processData *processes = malloc(no * sizeof(struct processData));
    if (!processes)
    {
        printf("Memory allocation failed.\n");
        fclose(pFile);
        return 1;
    }

    int lastArrival = 1;
    for (int i = 0; i < no; i++)
    {
        initializeProcessData(processes, &lastArrival, i);

        assignProcessDependency(processes, i, no);

        writeProcessInFile(pFile, processes, i);
    }
    fclose(pFile);
}