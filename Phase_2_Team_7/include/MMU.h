#ifndef MMU_H
#define MMU_H
#define USE_LRU 0   // set to 0 to use Second Chance 1 to use LRU

#include "../include/headers.h"
#include <string.h>

#define FRAME_COUNT 32   // 512 / 16 = 32 frames
#define PAGE_SIZE   16
#define VPN_COUNT   64

typedef struct FrameInfo {
    struct PCB* pcb;
    int pageNum;         
    int referenceBit;    // second chance bit
    bool dirty;         
} FrameInfo;


static bool MemoryMap[FRAME_COUNT];
static FrameInfo RamInfo[FRAME_COUNT];
static int pointer = 0;

static void MMU_init(void)
{
    memset(MemoryMap, 0, sizeof(MemoryMap));
    memset(RamInfo, 0, sizeof(RamInfo));

    for (int i = 0; i < FRAME_COUNT; ++i) {
        RamInfo[i].pcb = NULL;
        RamInfo[i].pageNum = -1;
        RamInfo[i].referenceBit = 0;
        RamInfo[i].dirty = false;
    }
    pointer = 0;
}

static void initPageTable(struct PCB* process)
{
    for (int i = 0; i < VPN_COUNT; i++) {
        process->pageTable[i].frameNumber = -1;
        process->pageTable[i].valid = false;
        process->pageTable[i].dirty = false;
        process->pageTable[i].lastAccessTime = -1;
    }
}


static int translateAddress(struct PCB *process, int virtualAddress, bool isWrite)
{
    int pageNum = virtualAddress / PAGE_SIZE;
    int offset  = virtualAddress % PAGE_SIZE;

    if (pageNum < 0 || pageNum >= VPN_COUNT) return -1;
    if (process->pageTable[pageNum].valid == false) return -1;

    int frame = process->pageTable[pageNum].frameNumber;
    if (frame < 0 || frame >= FRAME_COUNT) return -1;

    process->pageTable[pageNum].lastAccessTime = getClk();
    RamInfo[frame].referenceBit = 1;

    if (isWrite) {
        process->pageTable[pageNum].dirty = true;
        RamInfo[frame].dirty = true;
    }

    return frame * PAGE_SIZE + offset;
}


static int ExecuteSecondChance(bool *victimWasDirty)
{
    while (1) {
        if (RamInfo[pointer].referenceBit == 1) {
            RamInfo[pointer].referenceBit = 0;
            pointer = (pointer + 1) % FRAME_COUNT;
            continue;
        }

        int victim = pointer;
        *victimWasDirty = RamInfo[victim].dirty;

        if (RamInfo[victim].pcb != NULL && RamInfo[victim].pageNum >= 0) {
            struct PCB *vp = RamInfo[victim].pcb;
            int vpn = RamInfo[victim].pageNum;

            if (vpn >= 0 && vpn < VPN_COUNT) {
                vp->pageTable[vpn].valid = false;
                vp->pageTable[vpn].frameNumber = -1;
            }
        }

        pointer = (pointer + 1) % FRAME_COUNT;
        return victim;
    }
}


static int ExecuteLRU(bool *victimWasDirty)
{
    int victim = -1;
    int oldestTime = INT_MAX;

    for (int i = 0; i < FRAME_COUNT; i++)
    {
        if (RamInfo[i].pcb != NULL)
        {
            struct PCB *p = RamInfo[i].pcb;
            int vpn = RamInfo[i].pageNum;

            if (p->pageTable[vpn].lastAccessTime < oldestTime)
            {
                oldestTime = p->pageTable[vpn].lastAccessTime;
                victim = i;
            }
        }
    }

    *victimWasDirty = RamInfo[victim].dirty;

    // Invalidate victim page
    struct PCB *vp = RamInfo[victim].pcb;
    int vpn = RamInfo[victim].pageNum;
    vp->pageTable[vpn].valid = false;
    vp->pageTable[vpn].frameNumber = -1;

    return victim;
}


static int AllocatePage(struct PCB* process, int pageNum, bool isWrite,
                        bool *usedFreeFrame, int *victimFrame, bool *victimWasDirty)
{
    *usedFreeFrame = false;
    *victimFrame = -1;
    *victimWasDirty = false;

    // 1) Try first-fit free frame
    for (int i = 0; i < FRAME_COUNT; i++) {
        if (MemoryMap[i] == 0) {
            MemoryMap[i] = 1;
            *usedFreeFrame = true;

            process->pageTable[pageNum].frameNumber = i;
            process->pageTable[pageNum].valid = true;
            process->pageTable[pageNum].lastAccessTime = getClk();
            process->pageTable[pageNum].dirty = (isWrite ? true : false);

            RamInfo[i].pcb = process;
            RamInfo[i].pageNum = pageNum;
            RamInfo[i].referenceBit = 1;
            RamInfo[i].dirty = (isWrite ? true : false);

            return i;
        }
    }

    // 2) No free frame LRU aw execute 2nd chance 3la 7asab 
    #if USE_LRU
        *victimFrame = ExecuteLRU(victimWasDirty);
    #else
        *victimFrame = ExecuteSecondChance(victimWasDirty);
    #endif

    int frame = *victimFrame;

    // overwrite frame with new mapping
    process->pageTable[pageNum].frameNumber = frame;
    process->pageTable[pageNum].valid = true;
    process->pageTable[pageNum].lastAccessTime = getClk();
    process->pageTable[pageNum].dirty = (isWrite ? true : false);

    RamInfo[frame].pcb = process;
    RamInfo[frame].pageNum = pageNum;
    RamInfo[frame].referenceBit = 1;
    RamInfo[frame].dirty = (isWrite ? true : false);

    MemoryMap[frame] = 1;
    return frame;
}

#endif
