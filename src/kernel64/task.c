#include "task.h"
#include "descriptors.h"
#include "util.h"
#include "asm_util.h"
#include "sync.h"
#include "console.h"
#include "multiprocessor.h"

static Scheduler g_schedulers[MAXPROCESSORCOUNT];
static TcbPoolManager g_tcbPoolManager;

static void k_initTcbPool(void) {
	int i;
	
	// initialize TCB pool manager.
	k_memset(&g_tcbPoolManager, 0, sizeof(g_tcbPoolManager));
	
	// initialize TCB pool area.
	g_tcbPoolManager.startAddr = (Tcb*)TASK_TCBPOOLADDRESS;
	k_memset((void*)TASK_TCBPOOLADDRESS, 0, sizeof(Tcb) * TASK_MAXCOUNT);
	
	// allocate ID (offset) to TCB.
	for(i = 0; i < TASK_MAXCOUNT; i++){
		g_tcbPoolManager.startAddr[i].link.id = i;
	}
	
	// initialize max count and allocated count of TCB.
	g_tcbPoolManager.maxCount = TASK_MAXCOUNT;
	g_tcbPoolManager.allocedCount = 1;
}

static Tcb* k_allocTcb(void) {
	Tcb* emptyTcb;
	int i;
	
	if (g_tcbPoolManager.useCount == g_tcbPoolManager.maxCount) {
		return null;
	}
	
	for (i = 0; i < g_tcbPoolManager.maxCount; i++) {
		// If high 32 bits of TCB ID (64 bits) == 0, it's not allocated.
		if ((g_tcbPoolManager.startAddr[i].link.id >> 32) == 0) {
			emptyTcb = &(g_tcbPoolManager.startAddr[i]);
			break;
		}
	}
	
	// set not-0 to high 32 bits of TCB ID to allocate.
	// TCB ID consists of TCB allocation count (high 32 bits) and TCB offset (low 32 bits).
	emptyTcb->link.id = ((qword)g_tcbPoolManager.allocedCount << 32) | i;
	g_tcbPoolManager.useCount++;
	g_tcbPoolManager.allocedCount++;
	
	if (g_tcbPoolManager.allocedCount == 0) {
		g_tcbPoolManager.allocedCount = 1;
	}
	
	return emptyTcb;
}

static void k_freeTcb(qword id) {
	int i;
	
	// get TCB offset (low 32 bits) of TCB ID.
	i = GETTCBOFFSET(id);
	
	// initialize context and TCB allocation count as 0.
	k_memset(&(g_tcbPoolManager.startAddr[i].context), 0, sizeof(Context));
	g_tcbPoolManager.startAddr[i].link.id = i;
	
	g_tcbPoolManager.useCount--;
}

Tcb* k_createTask(qword flags, void* memAddr, qword memSize, qword entryPointAddr, byte affinity) {
	Tcb* task;    // task to create (task means process or thread)
	Tcb* process; // process with current task in it (It means process which has created the task, or it means parent process.)
	void* stackAddr;
	
	k_lockSpin(&(g_schedulers.spinlock));
	
	// allocate task.
	task = k_allocTcb();
	
	if (task == null) {
		k_unlockSpin(&(g_schedulers.spinlock));
		return null;
	}
	
	// get process with current task in it.
	process = k_getProcessByThread(k_getRunningTask());
	
	if (process == null) {
		k_freeTcb(task->link.id);
		k_unlockSpin(&(g_schedulers.spinlock));
		return null;
	}
	
	//====================================================================================================
	// <Task Management>
	// 1. Create Task
	//   -------------------
	//   |        -------- |  -> Outer one is ready list.
	//   |P1->P2->|T3=>T4| |  -> Inner one is child thread list.
	//   |        -------- |
	//   -------------------
	//
	// 2. End P2 Process
	//   ------------
	//   |T3->T4->P2|  -> wait list
	//   ------------
	//
	// 3. End T3 Thread
	//   ----
	//   |T3|  -> wait list
	//   ----
	//
	//====================================================================================================
	
	// If creating thread.
	if (flags & TASK_FLAGS_THREAD) {
		
		// set ID of process which has created this thread to [thread.parent process ID],
		// and set memory area of process which has created this thread to [thread.memory area].
		task->parentProcessId = process->link.id;
		task->memAddr = process->memAddr;
		task->memSize = process->memSize;
		
		// add the created thread to [parent process.child thread list].
		k_addListToTail(&(process->childThreadList), &(task->threadLink));
		
	// If creating process.
	} else {
		// set ID of process which has created this process to [process.parent process ID],
		// and set parameter memory area to [process.memory area]
		task->parentProcessId = process->link.id;
		task->memAddr = memAddr;
		task->memSize = memSize;
	}
	
	// set thread ID equaled to task ID.
	task->threadLink.id = task->link.id;
	
	k_unlockSpin(&(g_schedulers.spinlock));
	
	// set stack address of task. (use TCB ID offset as stack pool offset.)
	stackAddr = (void*)(TASK_STACKPOOLADDRESS + (TASK_STACKSIZE * GETTCBOFFSET(task->link.id)));
	
	// set task.
	k_setupTask(task, flags, entryPointAddr, stackAddr, TASK_STACKSIZE);
	
	// initialize child thread list.
	k_initList(&(task->childThreadList));
	
	// initialize FPU used flag.
	task->fpuUsed = false;
	
	k_lockSpin(&(g_schedulers.spinlock));
	
	// add task to ready list in order to make it scheduled.
	k_addTaskToReadyList(task);
	
	k_unlockSpin(&(g_schedulers.spinlock));
	
	return task;
}

static void k_setupTask(Tcb* task, qword flags, qword entryPointAddr, void* stackAddr, qword stackSize) {
	
	// initialize context
	k_memset(task->context.registers, 0, sizeof(task->context.registers));
	
	// set RSP, RBP.
	task->context.registers[TASK_RSP_OFFSET] = (qword)stackAddr + stackSize - 8;
	task->context.registers[TASK_RBP_OFFSET] = (qword)stackAddr + stackSize - 8;
	
	// push the address of k_exitTask function to the top (8 bytes) of stack as the return address,
	// in order to move to ExitTask function when entry point function of task returns.
	*(qword*)((qword)stackAddr + stackSize - 8) = (qword)k_exitTask;
	
	// set segment selector.
	task->context.registers[TASK_CS_OFFSET] = GDT_KERNELCODESEGMENT;
	task->context.registers[TASK_DS_OFFSET] = GDT_KERNELDATASEGMENT;
	task->context.registers[TASK_ES_OFFSET] = GDT_KERNELDATASEGMENT;
	task->context.registers[TASK_FS_OFFSET] = GDT_KERNELDATASEGMENT;
	task->context.registers[TASK_GS_OFFSET] = GDT_KERNELDATASEGMENT;
	task->context.registers[TASK_SS_OFFSET] = GDT_KERNELDATASEGMENT;
	
	// set RIP, RFLAGS.
	task->context.registers[TASK_RIP_OFFSET] = entryPointAddr;
	task->context.registers[TASK_RFLAGS_OFFSET] |= 0x0200; // set IF(bit 9) of RFLAGS to 1, and enable interrupt.
	
	// set etc
	task->stackAddr = stackAddr;
	task->stackSize = stackSize;
	task->flags = flags;
}

void k_initScheduler(void) {
	int i;
	Tcb* task;
	
	// initialize TCB pool.
	k_initTcbPool();
	
	for (i = 0; i < TASK_MAXREADYLISTCOUNT; i++) {
		// initialize ready lists.
		k_initList(&(g_schedulers.readyLists[i]));
		
		// initialize task execute counts by task priority.
		g_schedulers.executeCounts[i] = 0;
	}
	
	// initialize wait list.
	k_initList(&(g_schedulers.waitList));
	
	// allocate TCB and set it as a running task. (This TCB is for the booting task.)
	// The booting task is the first process of kernel.
	task = k_allocTcb();
	g_schedulers.runningTask = task;
	task->flags = TASK_FLAGS_HIGHEST | TASK_FLAGS_PROCESS | TASK_FLAGS_SYSTEM; // set the highest priority to the booting task which becomes the console shell task later.
	task->parentProcessId = task->link.id; // set process itself ID to parent process ID, because the first kernel process dosen't have parent process.
	task->memAddr = (void*)0x100000;
	task->memSize = 0x500000;
	task->stackAddr = (void*)0x600000;
	task->stackSize = 0x100000;
	
	// initialize fields related with process load.
	g_schedulers.spendProcessorTimeInIdleTask = 0;
	g_schedulers.processorLoad = 0;
	
	// initialize last FPU-used task ID.
	g_schedulers.lastFpuUsedTaskId = TASK_INVALIDID;
	
	// initialize spinlock.
	k_initSpinlock(&(g_schedulers.spinlock));
}

void k_setRunningTask(byte apicId, Tcb* task) {
	k_lockSpin(&(g_schedulers.spinlock));
	
	g_schedulers.runningTask = task;
	
	k_unlockSpin(&(g_schedulers.spinlock));
}

Tcb* k_getRunningTask(byte apicId) {
	Tcb* runningTask;
	
	k_lockSpin(&(g_schedulers.spinlock));
	
	runningTask = g_schedulers.runningTask;
	
	k_unlockSpin(&(g_schedulers.spinlock));
	
	return runningTask;
}

static Tcb* k_getNextTaskToRun(byte apicId) {
	Tcb* target = null;
	int taskCount;
	int i, j;
	
	// If task is null, loop once more.
	// It's because tasks can't be selected even though they exist in ready lists,
	// when all tasks in ready lists are executed once and hand processor over to other tasks.
	for (j = 0; j < 2; j++) {
		// select next running task in searching tasks from the highest ready list to lowest ready list.
		for (i = 0; i < TASK_MAXREADYLISTCOUNT; i++) {
			// get task count by priority.
			taskCount = k_getListCount(&(g_schedulers.readyLists[i]));
			
			// If task execute count < task count, select task with current priority.
			if (g_schedulers.executeCounts[i] < taskCount) {
				target = (Tcb*)k_removeListFromHead(&(g_schedulers.readyLists[i]));
				g_schedulers.executeCounts[i]++;
				break;
				
			// If task execute count >= task count, select task with next priority.
			} else {
				g_schedulers.executeCounts[i] = 0;
			}
		}
		
		// If task is not null, break.
		if (target != null) {
			break;
		}
	}
	
	return target;
}

static bool k_addTaskToReadyList(byte apicId, Tcb* task) {
	byte priority;
	
	priority = GETPRIORITY(task->flags);
	
	if (priority >= TASK_MAXREADYLISTCOUNT) {
		return false;
	}
	
	k_addListToTail(&(g_schedulers.readyLists[priority]), task);
	return true;
}

static Tcb* k_removeTaskFromReadyList(byte apicId, qword taskId) {
	Tcb* target;
	byte priority;
	
	// check if task ID is valid.
	if (GETTCBOFFSET(taskId) >= TASK_MAXCOUNT) {
		return null;
	}
	
	// check if task ID matches task ID which is searched from task pool.
	target = &(g_tcbPoolManager.startAddr[GETTCBOFFSET(taskId)]);
	if (target->link.id != taskId) {
		return null;
	}
	
	priority = GETPRIORITY(target->flags);
	
	// remove task from ready list with the priority.
	target = k_removeList(&(g_schedulers.readyLists[priority]), taskId);
	
	return target;
}

static bool k_findSchedulerOfTaskAndLock(qword taskId, byte* apicId);

bool k_changePriority(qword taskId, byte priority) {
	Tcb* target;
	
	if (priority >= TASK_MAXREADYLISTCOUNT) {
		return false;
	}
	
	k_lockSpin(&(g_schedulers.spinlock));
	
	target = g_schedulers.runningTask;
	
	// If it's a current running task, only change priority.
	// It's because the task will move to ready list with the changed priority, when timer interrupt (IRQ 0) occurs and task switching happens.
	if (target->link.id == taskId) {
		SETPRIORITY(target->flags, priority);
		
	// If it's not a current running task, remove it from ready list, change priority, and move it to ready list with the changed priority.
	} else {
		target = k_removeTaskFromReadyList(taskId);
		
		// If the task dosen't exist in ready list, change priority in searching it from task pool.
		if (target == null) {
			target = k_getTcbInTcbPool(GETTCBOFFSET(taskId));
			if (target != null) {
				SETPRIORITY(target->flags, priority);
			}
			
		// If the task exists in ready list, move it ot ready list with the changed priority.
		} else {
			SETPRIORITY(target->flags, priority);
			k_addTaskToReadyList(target);
		}
	}
	
	k_unlockSpin(&(g_schedulers.spinlock));
	
	return true;
}

/**
  < Context Switching >
  [Ref] - interrupt switching: context switching between task and interrupt handler.
        - task switching: context switching between task and task.
  
  1. interrupt switching: It occurs when interrupts occur.
  
     registers  processor/ISR   IST
     -----         --->        -----
     | A |                     | A |
     -----         <---        -----
     
     - process task A.
     - save task A context from registers to IST by processor and ISR.
     - process interrupt handler.
     - restore task A context from IST to registers by processor and ISR.
     - process task A.
     
  2. task switching: It occurs when task requests such as waiting, sleeping, and etc.
  
     registers    k_schedule   TCB pool
     ----------     --->       -------
     | A -> B |                | ... |
     ----------     <---       |  A  |
                               |  B  |
                               | ... |
                               -------
     - process task A.
     - save task A context from registers to TCB pool by k_schedule.
     - restore task B context from TCB pool to registers by k_schedule.
     - process task B.
  
  3. interrupt switching and task switching: It occurs when timer interrupt occurs.
  
     registers    processor/ISR     IST      k_scheduleInInterrupt   TCB pool
     ----------      --->       ----------          --->             -------
     | A -> B |                 | A -> B |                           | ... |
     ----------      <---       ----------          <---             |  A  |
                                                                     |  B  |
                                                                     | ... |
                                                                     -------
     - process task A.
     - save task A context from registers to IST by processor and ISR.
     - process timer interrupt handler.
     - save task A context from IST to TCB pool by k_scheduleInInterrupt.
     - restore task B context from TCB pool to IST by k_scheduleInInterrupt.
     - restore task B context from IST to registers by processor and ISR.
     - process task B.
  
  
  < Scheduling in Multi-core Processor >
  
       core 0     processor/ISR    IST 0     k_scheduleInInterrupt   TCB pool
     ----------      --->       ----------          --->             -------
     | A -> B |                 | A -> B |                           | ... |
     ----------      <---       ----------          <---             |  A  |
                                                                     |  B  |
       core 1                      IST 1                             |  C  |
     ----------      --->       ----------          --->             |  D  |
     | C -> D |                 | C -> D |                           |  E  |
     ----------      <---       ----------          <---             |  F  |
                                                                     |  G  |
       core 2                      IST 2                             |  H  |
     ----------      --->       ----------          --->             |  I  |
     | F -> G |                 | F -> G |                           |  J  |
     ----------      <---       ----------          <---             | ... |
                                                                     -------
     
                   scheduler 0
     running task                    ready lists: task 1 -> 2
     ----------      <---       -----------------------------
     | A -> B |                 |         ...               |
     ----------      --->       | B (out) | A (in) | F (in) |
                                |         ...               |
                                -----------------------------
     
                   scheduler 1
     running task                    ready lists: task 2
     ----------      <---       ------------------------
     | C -> D |                 |         ...          |
     ----------      --->       | D (out) | E | C (in) |
                                |         ...          |
                                ------------------------
     
                   scheduler 2
     running task                    ready lists: task 4 -> 3
     ----------      <---       -----------------------
     | F -> G |                 |         ...         |
     ----------      --->       | G (out) | H | I | J |
                to scheduler 0  |         ...         |
                                -----------------------
*/

void k_schedule(void) {
	Tcb* runningTask, * nextTask;
	bool interruptFlag;
	
	if (k_getReadyTaskCount() < 1) {
		return;
	}
	
	// disable interrupt in order to prevent task switching while processing task switching.
	interruptFlag = k_setInterruptFlag(false);
	k_lockSpin(&(g_schedulers.spinlock));
	
	nextTask = k_getNextTaskToRun();
	if (nextTask == null) {
		k_unlockSpin(&(g_schedulers.spinlock));
		k_setInterruptFlag(interruptFlag);
		return;
	}
	
	runningTask = g_schedulers.runningTask;
	g_schedulers.runningTask = nextTask;
	
	// If it's switched from idle task, increase process time used by idle task.
	if ((runningTask->flags & TASK_FLAGS_IDLE) == TASK_FLAGS_IDLE) {
		g_schedulers.spendProcessorTimeInIdleTask += (TASK_PROCESSORTIME - g_schedulers.processorTime);
	}
	
	// If next task is not last FPU-used task, set CR0.TS=1.
	if (g_schedulers.lastFpuUsedTaskId != nextTask->link.id) {
		k_setTs();
		
	} else {
		k_clearTs();
	}
	
	/**
	  < Context Switching in Task >
	  - save context: registers -> running task (by k_switchContext)
	  - restore context: next task -> registers (by k_switchContext)
	 */
	
	// If it's switched from end task, move end task to wait list, switch task.
	if ((runningTask->flags & TASK_FLAGS_ENDTASK) == TASK_FLAGS_ENDTASK) {
		k_addListToTail(&(g_schedulers.waitList), runningTask);
		k_unlockSpin(&(g_schedulers.spinlock));
		// restore next task context from TCB pool to registers.
		k_switchContext(null, &(nextTask->context));
		
	// If it's switched from normal task, move normal task to ready list, switch task.
	} else {
		k_addTaskToReadyList(runningTask);
		k_unlockSpin(&(g_schedulers.spinlock));
		// save running task context from registers to TCB pool,
		// and restore next task context from TCB pool to registers.
		k_switchContext(&(runningTask->context), &(nextTask->context));
	}
	
	// update process time.
	g_schedulers.processorTime = TASK_PROCESSORTIME;
	
	k_setInterruptFlag(interruptFlag);
}

bool k_scheduleInInterrupt(void) {
	Tcb* runningTask, * nextTask;
	char* contextAddr;
	
	k_lockSpin(&(g_schedulers.spinlock));
	
	nextTask = k_getNextTaskToRun();
	if (nextTask == null) {
		k_unlockSpin(&(g_schedulers.spinlock));
		return false;
	}
	
	contextAddr = (char*)IST_STARTADDRESS + IST_SIZE - sizeof(Context);
	runningTask = g_schedulers.runningTask;
	g_schedulers.runningTask = nextTask;
	
	// If it's switched from idle task, increase processor time used by idle task.
	if ((runningTask->flags & TASK_FLAGS_IDLE) == TASK_FLAGS_IDLE) {
		g_schedulers.spendProcessorTimeInIdleTask += TASK_PROCESSORTIME;
	}
	
	/**
	  < Context Switching in Interrupt Handler >
	  - save context: registers -> IST (by processor and ISR) -> running task (by k_memcpy)
	  - restore context: next task -> IST (by k_memcpy) -> registers (by processor and ISR)
	 */
	
	// If it's switched from end task, move end task to wait list, switch task.
	if ((runningTask->flags & TASK_FLAGS_ENDTASK) == TASK_FLAGS_ENDTASK) {
		k_addListToTail(&(g_schedulers.waitList), runningTask);
		
	// If it's switched from normal task, move normal task to ready list, switch task.
	} else {
		// save running task context from IST to TCB pool.
		k_memcpy(&(runningTask->context), contextAddr, sizeof(Context));
		k_addTaskToReadyList(runningTask);
	}
	
	k_unlockSpin(&(g_schedulers.spinlock));
	
	// If next task is not last FPU-used task, set CR0.TS=1.
	if (g_schedulers.lastFpuUsedTaskId != nextTask->link.id) {
		k_setTs();
		
	} else {
		k_clearTs();
	}
	
	// restore next task context from TCB pool to IST.
	k_memcpy(contextAddr, &(nextTask->context), sizeof(Context));
	
	// update process time.
	g_schedulers.processorTime = TASK_PROCESSORTIME;
	
	return true;
}

void k_decreaseProcessorTime(void) {
	if (g_schedulers.processorTime > 0) {
		g_schedulers.processorTime--;
	}
}

bool k_isProcessorTimeExpired(void) {
	if (g_schedulers.processorTime <= 0) {
		return true;
	}
	
	return false;
}

bool k_endTask(qword taskId) {
	Tcb* target;
	byte priority;
	
	k_lockSpin(&(g_schedulers.spinlock));
	
	target = g_schedulers.runningTask;
	
	// If it's a current running task, set end task flag, and switch task
	if (target->link.id == taskId) {
		target->flags |= TASK_FLAGS_ENDTASK;
		SETPRIORITY(target->flags, TASK_FLAGS_WAIT);
		
		k_unlockSpin(&(g_schedulers.spinlock));
		
		// switch task
		k_schedule();
		
		// This code below will be never executed, because task switching has already done before.
		while (true);
		
	// If it's not a current running task, remove it from ready list, set end task flag, move it to wait list.
	} else {
		target = k_removeTaskFromReadyList(taskId);
		
		if (target == null) {
			target = k_getTcbInTcbPool(GETTCBOFFSET(taskId));
			if (target != null) {
				target->flags |= TASK_FLAGS_ENDTASK;
				SETPRIORITY(target->flags, TASK_FLAGS_WAIT);
			}
			
			k_unlockSpin(&(g_schedulers.spinlock));
			
			return true;
		}
		
		target->flags |= TASK_FLAGS_ENDTASK;
		SETPRIORITY(target->flags, TASK_FLAGS_WAIT);
		k_addListToTail(&(g_schedulers.waitList), target);
	}
	
	k_unlockSpin(&(g_schedulers.spinlock));
	
	return true;
}

void k_exitTask(void) {
	k_endTask(g_schedulers.runningTask->link.id);
}

int k_getReadyTaskCount(byte apicId) {
	int totalCount = 0;
	int i;
	
	k_lockSpin(&(g_schedulers.spinlock));
	
	for (i = 0; i < TASK_MAXREADYLISTCOUNT; i++) {
		totalCount += k_getListCount(&(g_schedulers.readyLists[i]));
	}
	
	k_unlockSpin(&(g_schedulers.spinlock));
	
	return totalCount;
}

int k_getTaskCount(byte apicId) {
	int totalCount;
	
	// total task count = ready task count + end-waiting task count + current running task count
	totalCount = k_getReadyTaskCount();
	
	k_lockSpin(&(g_schedulers.spinlock));
	
	totalCount += k_getListCount(&(g_schedulers.waitList)) + 1;
	
	k_unlockSpin(&(g_schedulers.spinlock));
	
	return totalCount;
}

Tcb* k_getTcbInTcbPool(int offset) {
	if ((offset <= -1) && (offset >= TASK_MAXCOUNT)) {
		return null;
	}
	
	return &(g_tcbPoolManager.startAddr[offset]);
}

bool k_isTaskExist(qword taskId) {
	Tcb* task;
	
	task = k_getTcbInTcbPool(GETTCBOFFSET(taskId));
	
	if ((task == null) || (task->link.id != taskId)) {
		return false;
	}
	
	return true;
}

qword k_getProcessorLoad(void) {
	return g_schedulers.processorLoad;
}

static Tcb* k_getProcessByThread(Tcb* thread) {
	Tcb* process;
	
	// If parameter is process, return process itself.
	if (thread->flags & TASK_FLAGS_PROCESS) {
		return thread;
	}
	
	// If parameter is thread, return parent process.
	process = k_getTcbInTcbPool(GETTCBOFFSET(thread->parentProcessId));
	if ((process == null) || (process->link.id != thread->parentProcessId)) {
		return null;
	}
	
	return process;
}

void k_addTaskToSchedulerWithLoadBalancing(Tcb* task);
static byte k_findSchedulerOfMinTaskCount(const Tcb* task);
void k_setTaskLoadBalancing(byte apicId, bool loadBalancing);
bool k_changeProcessorAffinity(qword taskId, byte affinity);

void k_idleTask(void) {
	Tcb* task, * childThread, * process;
	qword lastMeasureTickCount, lastSpendTickInIdleTask;
	qword currentMeasureTickCount, currentSpendTickInIdleTask;
	int i, count;
	qword taskId;
	void* threadLink;
	
	lastMeasureTickCount = k_getTickCount();
	lastSpendTickInIdleTask = g_schedulers.spendProcessorTimeInIdleTask;
	
	// infinite loop
	while (true) {
		
		//----------------------------------------------------------------------------------------------------
		// 1. calculate processor load
		//----------------------------------------------------------------------------------------------------
		
		currentMeasureTickCount = k_getTickCount();
		currentSpendTickInIdleTask = g_schedulers.spendProcessorTimeInIdleTask;
		
		// processor load (%) = 100 - (processor time used by idle task * 100 / processor time used by whole system)
		if ((currentMeasureTickCount - lastMeasureTickCount) == 0) {
			g_schedulers.processorLoad = 0;
			
		} else {
			g_schedulers.processorLoad = 100 - ((currentSpendTickInIdleTask - lastSpendTickInIdleTask) * 100 / (currentMeasureTickCount - lastMeasureTickCount));
		}
		
		lastMeasureTickCount = currentMeasureTickCount;
		lastSpendTickInIdleTask = currentSpendTickInIdleTask;
		
		//----------------------------------------------------------------------------------------------------
		// 2. halt processor by process load
		//----------------------------------------------------------------------------------------------------
		
		// halt processor by process load
		k_haltProcessorByLoad();
		
		//----------------------------------------------------------------------------------------------------
		// 3. end the end tasks in wait list
		//----------------------------------------------------------------------------------------------------
		
		// If end task exists in wait list, remove end task from wait list, free memory of end task.
		if (k_getListCount(&(g_schedulers.waitList)) > 0) {
			while (true) {
				k_lockSpin(&(g_schedulers.spinlock));
				
				// remove end task from wait list.
				task = k_removeListFromHead(&(g_schedulers.waitList));
				if (task == null) {
					k_unlockSpin(&(g_schedulers.spinlock));
					break;
				}
				
				/**
				  [Code Block 1] If end task is process, end all child threads of process which means to move them from ready list to wait list,
				                 wait until child threads in wait list are totally ended, and finally totally end process itself.
				                 (End means to move tasks from ready list to wait list.)
				                 (Totally end means to free memory of tasks in wait list.)
				  [Reference] free memory of process: free code/data area, TCB, stack.
				              free memory of thread: free TCB, stack.
				 */
				if (task->flags & TASK_FLAGS_PROCESS) {
					count = k_getListCount(&(task->childThreadList));
					
					for (i = 0; i < count; i++) {
						threadLink = (Tcb*)k_removeListFromHead(&(task->childThreadList));
						if (threadLink == null) {
							break;
						}
						
						childThread = GETTCBFROMTHREADLINK(threadLink);
						
						// the reasons why re-put child thread to child thread list, after removing it from child thread list.
						// - first reason: When thread ends, it removes itself from child thread list in [Code Block 2].
						// - second reason: Under multi-core processor, If child thread is running on the other cores, it can't be moved to wait list immediately.
						k_addListToTail(&(task->childThreadList), &(childThread->threadLink));
						
						// end all child threads (move them from ready list to wait list.)
						k_endTask(childThread->link.id);
					}
					
					// If child thread remains, it waits until all child thread will be totally ended by idle task.
					if (k_getListCount(&(task->childThreadList)) > 0) {
						k_addListToTail(&(g_schedulers.waitList), task);
						
						k_unlockSpin(&(g_schedulers.spinlock));
						continue;
						
					// If all child threads are totally ended, end process itself totally.
					} else {
						// [Todo] free code/data area of end process.
					}
					
				/**
				  [Code Block 2] If end task is thread, remove it from child thread list of process with thread in it,
				                 and totally end thread itself.
				 */
				} else if (task->flags & TASK_FLAGS_THREAD) {
					process = k_getProcessByThread(task);
					
					if (process != null) {
						k_removeList(&(process->childThreadList), task->link.id);
					}
				}
				
				// free TCB of end task (If TCB is freed, then also stack is freed automatically.)
				taskId = task->link.id;
				k_freeTcb(taskId);
				
				k_unlockSpin(&(g_schedulers.spinlock));
				
				//k_printf("IDLE: Task (0x%q) has completely ended.\n", taskId);
			}
		}
		
		// switch task.
		k_schedule();
	}
}

void k_haltProcessorByLoad(byte apicId) {
	
	if (g_schedulers.processorLoad < 40) {
		k_halt();
		k_halt();
		k_halt();
		
	} else if (g_schedulers.processorLoad < 80) {
		k_halt();
		k_halt();
		
	} else if (g_schedulers.processorLoad < 95) {
		k_halt();
	}
}

qword k_getLastFpuUsedTaskId(byte apicId) {
	return g_schedulers.lastFpuUsedTaskId;
}

void k_setLastFpuUsedTaskId(byte apicId, qword taskId) {
	g_schedulers.lastFpuUsedTaskId = taskId;
}
