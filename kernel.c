/*
Copyright (c) 2016, prpl Foundation

Permission to use, copy, modify, and/or distribute this software for any purpose with or without 
fee is hereby granted, provided that the above copyright notice and this permission notice appear 
in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE
INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE 
FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM 
LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, 
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

This code was written by Carlos Moratelli at Embedded System Group (GSE) at PUCRS/Brazil.

*/

#include <types.h>
#include <hal.h>
#include <config.h>
#include <kernel.h>
#include <tlb.h>
#include <malloc.h>
#include <libc.h>
#include <vcpu.h>
#include <dispatcher.h>
#include <globals.h>
#include <hypercall.h>

static uint32_t counttimerInt = 0;
static uint32_t guestexit = 0;

/** C code entry. Called from hal/boot.s after bss inicialization. */
int32_t main(uint32_t start_counter_cp0){
			
	/* First some paranoic checks!! */
	
	/* Verify if the processor implements the VZ module */
	if(!hasVZ()){
		/* panic */
		return 1;
	}

	/* is it in root mode ?  */
	if(!isRootMode()){
		/* panic */
		return 1;
	}
	
	/* This implementation relies on the GuestID field  */
	if(isRootASID()){ 
		return -1;
	}
	
	/* This implementation relies on the GuestID field */
	if(!hasGuestID()){
		/* panic */
		return 1;
	}
	
	if(has1KPageSupport()){
		/* Self Protection agains a variant that may implements 1K PageSupport. */
		Disable1KPageSupport();		
	}
	
	/* Now inialize the hardware */
	/* Processor inicialization */
	if(LowLevelProcInit()){
		//panic
		return 1;
	}
			
	/* Initialize memory */
	/* Register heap space on the allocator */ 
	 if(init_mem()){		
		return 1;
	}
	
	/*Initialize processor structure*/
	if(initProc()){
		return 1;
	} 
	
	if(initializeShedulers()){
		return 1;
	}
	
	/*Initialize vcpus and virtual machines*/
	initializeMachines();
	
	if(initializeRTMachines()){
		return 1;
	}
	
	/* successfull inicialization */
	Info("Successfully Initialized");
	
	return 0;
}

/** Hardware interrupt handle */
uint32_t InterruptHandler(){
	uint32_t flag = 1;
	uint32_t pending = getInterruptPending();

	if(pending & HARDWARE_INT_5){ /* timer interrupt */				
		clearInterruptMask(STATUS_HARDWARE_INT_5);
		counttimerInt++;
		setInterruptMask(STATUS_HARDWARE_INT_5);
		return RESCHEDULE;
	}
	
	if(pending & HARDWARE_INT_6){ /* timer interrupt */				
		clearInterruptMask(STATUS_HARDWARE_INT_6);
		Warning("int6");
		setInterruptMask(STATUS_HARDWARE_INT_6);
//		return SUCEEDED;
		flag = 0;
	}
	
	
	if(pending & HARDWARE_INT_4){ /* timer interrupt */				
		clearInterruptMask(STATUS_HARDWARE_INT_4);
		Warning("int4");
		setInterruptMask(STATUS_HARDWARE_INT_4);
		flag = 0;
		//return SUCEEDED;
	}
	
	if(pending & HARDWARE_INT_3){ /* timer interrupt */				
		clearInterruptMask(STATUS_HARDWARE_INT_3);
		Warning("int3");
		setInterruptMask(STATUS_HARDWARE_INT_3);
		flag = 0;
	//	return SUCEEDED;
	}

	if(pending & HARDWARE_INT_2){ /* timer interrupt */				
		clearInterruptMask(STATUS_HARDWARE_INT_2);
		Warning("int2");
		setInterruptMask(STATUS_HARDWARE_INT_2);
		flag = 0;
		//return SUCEEDED;
	}

	if(flag)
		Warning("Interrupt handle not implemented.");

	return SUCEEDED;
}

/** Handle guest exceptions */
uint32_t GuestExitException(){
	uint32_t guestcause = getGCauseCode();
	uint32_t epc = getEPC();
	uint32_t ret = SUCEEDED;
	
	//printf("epc 0x%x", epc);
	
	switch (guestcause) {
	    case 0x0:	
			ret =  InstructionEmulation(epc);
	    	break;
	    case 0x2:
			ret = HypercallHandler();			
			break;
		default:
			break;
	}
	
	/* Determines the next EPC */
	epc = CalcNextPC(epc);	
	curr_vcpu->pc = epc;
	return ret;
		
}

/** Determine the cause and invoke the correct handler */
uint32_t HandleExceptionCause(){
	uint32_t CauseCode = getCauseCode();	

	switch (CauseCode){
		/* Interrupt */
	case 	0:	
			return InterruptHandler();
			break;
		/* GuestExit */
	case	0x1b:	
			return GuestExitException();
			break;
	/* TLB load, store or fetch exception */
	case	0x3:						
	case 	0x2:
			Warning("TLB miss: VCPU: %d\n", curr_vcpu->id);
			return TLBDynamicMap(curr_vcpu);
			break;
	default:
		/* panic */
		Warning("VM will be stopped due to error Cause Code 0x%x, EPC 0x%x, VCPU ID 0x%x", CauseCode, getEPC(), curr_vcpu->id);
		return ERROR;
	}
}

void configureGuestExecution(uint32_t exCause){
	
	uint32_t count;
	uint32_t currentCount;
	uint32_t elapsedTime;

	if(exCause == RESCHEDULE || exCause == CHANGE_TO_TARGET_VCPU)
		dispatcher();

	if(exCause == RESCHEDULE){
		confTimer(QUANTUM);
	}
	
	if (!isEnteringGuestMode()){
		Warning("Conditions to enter in GuestMode not satisfied!");
	}
		
	contextRestore();
}


/** First routine executed after an exception or after the sucessfull execution of the main() routine.
    @param init	flag to signalize first execution after main();
*/
int32_t initialize_RT_services(int32_t init, uint32_t counter){
	uint32_t ret = 0;
	uint32_t runNextRtInitMachine = 0;
	
	if(!init){
		switch(HandleExceptionCause()){
			case PROGRAM_ENDED:
				//Remove vcpu from vm list
				ll_remove(curr_vm->vcpus.head);				
				free(curr_vcpu);				
				runNextRtInitMachine = 1;			
				break;
			case SUCEEDED:
				runNextRtInitMachine = 0;
				break;								
			case ERROR:
				//PANIC
				Critical("Error on RT services initialization.");
				Critical("Hypervisor execution stopped.");
				while(1);
				break;						
			default:
				break;
		}
	}else{
		setStatusReg(STATUS_EXL);
		runNextRtInitMachine = 1;	
	}
	
	//Check if there are RT machines to be initialized
	if(runNextRtInitMachine){		
		if(rt_services_init_vcpu_list.count>0){
			curr_vcpu = rt_services_init_vcpu_list.head->ptr;
			ll_remove(rt_services_init_vcpu_list.head);
			exceptionHandler_addr = initialize_RT_services;	
		}else{
			//If there`s no RT Init VM remaining			
			runScheduler();		
			exceptionHandler_addr = exceptionHandler;
		}
	}
	
	configureGuestExecution(RESCHEDULE);	

	return ret;
}

void printPerformanceCounters(){
	uint32_t pcounter0;
	uint32_t pcounter1;
	
	pcounter0 = hal_lr_perfcounter0();
	pcounter1 = hal_lr_perfcounter1();
	Info("PERF COUNTER ROOT %d",pcounter0);			
	Info("PERF COUNTER GUEST %d",pcounter1);
}

int32_t exceptionHandler(int32_t init, uint32_t counter, uint32_t guestcounter){
	uint32_t ret;
		
	contextSave(NULL, counter, guestcounter);	
	
	ret = HandleExceptionCause();
	
	switch(ret){
		case SUCEEDED:
			break;
		case RESCHEDULE:				
			runScheduler();
			break;
		case CHANGE_TO_TARGET_VCPU:
			curr_vcpu=target_vcpu;
			break;
		case ERROR:
		case PROGRAM_ENDED:		
			if(remove_vm_and_runBestEffortScheduler()<0){
				//printPerformanceCounters();		
				Warning("The last VM ended!");
				Warning("No more VMs to execute.");
			}
			ret = RESCHEDULE;
			
			break;
		default:
			break;
	}	
	
	Warning("*");
	
	configureGuestExecution(ret);

	return 0;
}
