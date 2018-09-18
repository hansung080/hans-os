#ifndef __DYNAMIC_MEM_H__
#define __DYNAMIC_MEM_H__

#include "types.h"
#include "task.h"
#include "sync.h"

// dynamic memory area start address (0x1100000, 17MB)
// - aligned with 1MB unit (set it to the multiple of 1MB, rounding up)
// - It's 17MB if task size <= 1KB.
#define DMEM_START_ADDRESS ((TASK_STACKPOOLADDRESS + (TASK_STACKSIZE * TASK_MAXCOUNT) + 0xfffff) & 0xfffffffffff00000)

// smallest block size (1KB)
#define DMEM_MIN_SIZE (1 * 1024)

// bitmap flag
#define DMEM_EXIST 0x01 // block EXIST: block can be allocated.
#define DMEM_EMPTY 0x00 // block EMPTY: block can't be allocated, because it's already allocated or combined.

#pragma pack(push, 1)

typedef struct k_Bitmap {
	byte* bitmap;        // address of real bitmap: A bit in bitmap represents a block in block list.
	                     //                         - 1: exist
	                     //                         - 0: not exist
	qword existBitCount; // exist bit count: bit 1 count in bitmap
} Bitmap;

typedef struct k_DynamicMemManager {
	Spinlock spinlock;             // spinlock
	int maxLevelCount;             // block list count (level count)
	int smallestBlockCount;        // smallest block count
	qword usedSize;                // used memory size
	qword startAddr;               // block pool start address
	qword endAddr;                 // block pool end address
	byte* allocatedBlockListIndex; // address of index area (address of area saving allocated block list index)
	Bitmap* bitmapOfLevel;         // address of bitmap structure
} DynamicMemManager;

#pragma pack(pop)

void k_initDynamicMem(void);
void* k_allocMem(qword size);
bool k_freeMem(void* addr);
void k_getDynamicMemInfo(qword* dynamicMemStartAddr, qword* dynamicMemTotalSize, qword* metaDataSize, qword* usedMemSize);
DynamicMemManager* k_getDynamicMemManager(void);
static qword k_calcDynamicMemSize(void);
static int k_calcMetaBlockCount(qword dynamicRamSize);
static int k_allocBuddyBlock(qword alignedSize);
static qword k_getBuddyBlockSize(qword size);
static int k_getBlockListIndexByMatchSize(qword alignedSize);
static int k_findFreeBlockInBitmap(int blockListIndex);
static void k_setFlagInBitmap(int blockListIndex, int offset, byte flag);
static bool k_freeBuddyBlock(int blockListIndex, int blockOffset);
static byte k_getFlagInBitmap(int blockListIndex, int offset);

#endif // __DYNAMIC_MEM_H__