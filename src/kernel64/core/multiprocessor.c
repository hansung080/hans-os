#include "multiprocessor.h"
#include "mp_config_table.h"
#include "asm_util.h"
#include "local_apic.h"
#include "pit.h"
#include "../utils/util.h"

volatile int g_awakeApCount = 0; // awake AP count
volatile qword g_apicIdAddr = 0; // Local APIC ID Register address

bool k_startupAp(void) {
	
	// analyze MP configuration table.
	if (k_analyzeMpConfigTable() == false) {
		return false;
	}
	
	// enable local APIC of all processors.
	k_enableGlobalLocalApic();
	
	// enable local APIC of BSP.
	k_enableSoftwareLocalApic();
	
	// wake up AP.
	if (k_wakeupAp() == false) {
		return false;
	}
	
	return true;
}

byte k_getApicId(void) {
	MpConfigTableHeader* mpHeader;
	qword localApicBaseAddr;
	
	if (g_apicIdAddr == 0) {
		mpHeader = k_getMpConfigManager()->mpConfigTableHeader;
		
		if (mpHeader == null) {
			return 0;
		}
		
		localApicBaseAddr = mpHeader->memMapIoAddrOfLocalApic;
		g_apicIdAddr = localApicBaseAddr + LAPIC_REGISTER_APICID;
	}
	
	// local APIC ID field (bit 24~31) of Local APIC ID Register.
	return *(dword*)g_apicIdAddr >> 24;
}

static bool k_wakeupAp(void) {
	MpConfigManager* mpManager;
	MpConfigTableHeader* mpHeader;
	qword localApicBaseAddr;
	bool interruptFlag;
	int i;
	
	interruptFlag = k_setInterruptFlag(false);
	
	mpManager = k_getMpConfigManager();
	mpHeader = mpManager->mpConfigTableHeader;
	localApicBaseAddr = mpHeader->memMapIoAddrOfLocalApic;
	
	// save Local APIC ID Register address.
	g_apicIdAddr = localApicBaseAddr + LAPIC_REGISTER_APICID;
	
	/* send Init IPI to AP */
	*(dword*)(localApicBaseAddr + LAPIC_REGISTER_ICRLOWER) = LAPIC_DESTINATIONSHORTHAND_ALLEXCLUDINGSELF |
			                                                 LAPIC_TRIGGERMODE_EDGE |
															 LAPIC_LEVEL_ASSERT |
															 LAPIC_DESTINATIONMODE_PHYSICAL |
															 LAPIC_DELIVERYMODE_INIT;
	
	// wait for 10 milliseconds.
	k_waitUsingDirectPit(MSTOCOUNT(10));
	
	// check if it's sent successfully.
	if (*(dword*)(localApicBaseAddr + LAPIC_REGISTER_ICRLOWER) & LAPIC_DELIVERYSTATUS_PENDING) {
		
		// re-set in order to make timer interrupt occur 1000 times per 1 second.
		k_initPit(MSTOCOUNT(1), true);
		
		k_setInterruptFlag(interruptFlag);
		
		return false;
	}
	
	/* send Start-up IPI to AP (2 times): set AP start address as kernel32 start address. */
	for (i = 0; i < 2; i++) {
		*(dword*)(localApicBaseAddr + LAPIC_REGISTER_ICRLOWER) = LAPIC_DESTINATIONSHORTHAND_ALLEXCLUDINGSELF |
																 LAPIC_TRIGGERMODE_EDGE |
																 LAPIC_LEVEL_ASSERT |
																 LAPIC_DESTINATIONMODE_PHYSICAL |
																 LAPIC_DELIVERYMODE_STARTUP |
																 LAPIC_VECTOR_KERNEL32STARTADDRESS;
		
		// wait for 200 microseconds.
		k_waitUsingDirectPit(USTOCOUNT(200));
		
		// check if it's sent successfully.
		if (*(dword*)(localApicBaseAddr + LAPIC_REGISTER_ICRLOWER) & LAPIC_DELIVERYSTATUS_PENDING) {
			
			// re-set in order to make timer interrupt occur 1000 times per 1 second.
			k_initPit(MSTOCOUNT(1), true);
			
			k_setInterruptFlag(interruptFlag);
			
			return false;
		}
	}
	
	// re-set in order to make timer interrupt occur 1000 times per 1 second.
	k_initPit(MSTOCOUNT(1), true);
	
	k_setInterruptFlag(interruptFlag);
	
	// wait until all APs are awaked.
	while (g_awakeApCount < (mpManager->processorCount - 1)) {
		k_sleep(50);
	}
	
	return true;
}
