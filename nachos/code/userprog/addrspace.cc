// addrspace.cc 
//	Routines to manage address spaces (executing user programs).
//
//	In order to run a user program, you must:
//
//	1. link with the -N -T 0 option 
//	2. run coff2noff to convert the object file to Nachos format
//		(Nachos object code format is essentially just a simpler
//		version of the UNIX executable object code format)
//	3. load the NOFF file into the Nachos file system
//		(if you haven't implemented the file system yet, you
//		don't need to do this last step)
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "system.h"
#include "addrspace.h"
#include "noff.h"

//----------------------------------------------------------------------
// SwapHeader
// 	Do little endian to big endian conversion on the bytes in the 
//	object file header, in case the file was generated on a little
//	endian machine, and we're now running on a big endian machine.
//----------------------------------------------------------------------

static void 
SwapHeader (NoffHeader *noffH)
{
	noffH->noffMagic = WordToHost(noffH->noffMagic);
	noffH->code.size = WordToHost(noffH->code.size);
	noffH->code.virtualAddr = WordToHost(noffH->code.virtualAddr);
	noffH->code.inFileAddr = WordToHost(noffH->code.inFileAddr);
	noffH->initData.size = WordToHost(noffH->initData.size);
	noffH->initData.virtualAddr = WordToHost(noffH->initData.virtualAddr);
	noffH->initData.inFileAddr = WordToHost(noffH->initData.inFileAddr);
	noffH->uninitData.size = WordToHost(noffH->uninitData.size);
	noffH->uninitData.virtualAddr = WordToHost(noffH->uninitData.virtualAddr);
	noffH->uninitData.inFileAddr = WordToHost(noffH->uninitData.inFileAddr);
}

//----------------------------------------------------------------------
// ProcessAddressSpace::ProcessAddressSpace
// 	Create an address space to run a user program.
//	Load the program from a file "executable", and set everything
//	up so that we can start executing user instructions.
//
//	Assumes that the object code file is in NOFF format.
//
//	First, set up the translation from program memory to physical 
//	memory.  For now, this is really simple (1:1), since we are
//	only uniprogramming, and we have a single unsegmented page table
//
//	"executable" is the file containing the object code to load into memory
//----------------------------------------------------------------------

ProcessAddressSpace::ProcessAddressSpace(OpenFile *executable)
{
    NoffHeader noffH;
    unsigned int i, size;
    unsigned vpn, offset;
    TranslationEntry *entry;
    unsigned int pageFrame;

    executable->ReadAt((char *)&noffH, sizeof(noffH), 0);
    if ((noffH.noffMagic != NOFFMAGIC) && 
		(WordToHost(noffH.noffMagic) == NOFFMAGIC))
    	SwapHeader(&noffH);
    ASSERT(noffH.noffMagic == NOFFMAGIC);

// how big is address space?
    size = noffH.code.size + noffH.initData.size + noffH.uninitData.size 
			+ UserStackSize;	// we need to increase the size
						// to leave room for the stack
    numVirtualPages = divRoundUp(size, PageSize);
    size = numVirtualPages * PageSize;

    ASSERT(numVirtualPages+numPagesAllocated <= NumPhysPages);		// check we're not trying
										// to run anything too big --
										// at least until we have
										// virtual memory

    DEBUG('a', "Initializing address space, num pages %d, size %d\n", 
					numVirtualPages, size);

    backup = new char[size];

// first, set up the translation 
    KernelPageTable = new TranslationEntry[numVirtualPages];
    for (i = 0; i < numVirtualPages; i++) {
	KernelPageTable[i].virtualPage = i;
	//KernelPageTable[i].physicalPage = i+numPagesAllocated;
	int newPage = replace_with_next_physpage(-1);
	KernelPageTable[i].valid = TRUE;
	KernelPageTable[i].use = FALSE;
	KernelPageTable[i].physicalPage = newPage;
	physpage_owner[newPage] = currentThread;
	KernelPageTable[i].dirty = FALSE;
	KernelPageTable[i].readOnly = FALSE;  // if the code segment was entirely on 
					// a separate page, we could set its 
					// pages to be read-only
    KernelPageTable[i].shared = FALSE;
    KernelPageTable[i].backed_up =  FALSE;
    }
// zero out the entire address space, to zero the unitialized data segment 
// and the stack segment
    bzero(&machine->mainMemory[numPagesAllocated*PageSize], size);
 
    //numPagesAllocated += numVirtualPages;

// then, copy in the code and data segments into memory
    if (noffH.code.size > 0) {
        DEBUG('a', "Initializing code segment, at 0x%x, size %d\n", 
			noffH.code.virtualAddr, noffH.code.size);
        vpn = noffH.code.virtualAddr/PageSize;
        offset = noffH.code.virtualAddr%PageSize;
        entry = &KernelPageTable[vpn];
        pageFrame = entry->physicalPage;
        executable->ReadAt(&(machine->mainMemory[pageFrame * PageSize + offset]),
			noffH.code.size, noffH.code.inFileAddr);
    }
    if (noffH.initData.size > 0) {
        DEBUG('a', "Initializing data segment, at 0x%x, size %d\n", 
			noffH.initData.virtualAddr, noffH.initData.size);
        vpn = noffH.initData.virtualAddr/PageSize;
        offset = noffH.initData.virtualAddr%PageSize;
        entry = &KernelPageTable[vpn];
        pageFrame = entry->physicalPage;
        executable->ReadAt(&(machine->mainMemory[pageFrame * PageSize + offset]),
			noffH.initData.size, noffH.initData.inFileAddr);
    }

}

//----------------------------------------------------------------------
// ProcessAddressSpace::ProcessAddressSpace (ProcessAddressSpace*) is called by a forked thread.
//      We need to duplicate the address space of the parent.
//----------------------------------------------------------------------
ProcessAddressSpace::ProcessAddressSpace(char* file){

    NoffHeader noffH;
    execFile = file;
    Executable = fileSystem->Open(execFile);
    if (Executable == NULL)
    {
        printf("Error\n");

    }

    Executable->ReadAt((char*) &noffH,sizeof(noffH),0);
    if(noffH.noffMagic != NOFFMAGIC ){
        if(WordToHost(noffH.noffMagic)==NOFFMAGIC){
            SwapHeader(&noffH);
        }
    }

    unsigned int size;
    size = noffH.code.size + noffH.initData.size + noffH.uninitData.size + UserStackSize; 
    numVirtualPages = divRoundUp(size, PageSize);
    size = numVirtualPages * PageSize;
    backup=new char[size];

    KernelPageTable = new TranslationEntry[numVirtualPages];

    for(int i =0; i < numVirtualPages; i++){
        KernelPageTable[i].physicalPage = -1;
        KernelPageTable[i].virtualPage = i;
        KernelPageTable[i].shared = FALSE;
        KernelPageTable[i].valid = FALSE;
        KernelPageTable[i].dirty = FALSE;
        KernelPageTable[i].use = FALSE;
        KernelPageTable[i].readOnly = FALSE;
        KernelPageTable[i].backed_up = FALSE;

    }

}

ProcessAddressSpace::ProcessAddressSpace(ProcessAddressSpace *parentSpace)
{
    if(pageReplaceAlgo > 0)
    {
        execFile = parentSpace->execFile;
        Executable = fileSystem->Open(execFile);
        // printf("%s\n", );
    }

    numVirtualPages = parentSpace->GetNumPages();
    unsigned i, size = numVirtualPages * PageSize;
    unsigned count = 0;

    ASSERT(numVirtualPages+numPagesAllocated <= NumPhysPages);                // check we're not trying to run anything too big -

    DEBUG('a', "Initializing address space, num pages %d, size %d\n",
                                        numVirtualPages, size);
    // first, set up the translation
    TranslationEntry* parentPageTable = parentSpace->GetPageTable();
    KernelPageTable = new TranslationEntry[numVirtualPages];
    backup = new char[size];
    for (i = 0; i < numVirtualPages; i++) {
        KernelPageTable[i].virtualPage = i;
        if (parentPageTable[i].shared){
            //KernelPageTable[i].physicalPage = i+numPagesAllocated;
            KernelPageTable[i].physicalPage = parentPageTable[i].physicalPage;
        }
        else{
            if (parentPageTable[i].valid){
                KernelPageTable[i].physicalPage = count + numPagesAllocated;
                count++;
            }
            else{
                KernelPageTable[i].physicalPage = -1;
            }
        }
        KernelPageTable[i].valid = parentPageTable[i].valid;
        KernelPageTable[i].use = parentPageTable[i].use;
        KernelPageTable[i].dirty = parentPageTable[i].dirty;
        KernelPageTable[i].readOnly = parentPageTable[i].readOnly;      // if the code segment was entirely on
                                                    // a separate page, we could set its
                                                    // pages to be read-only
        KernelPageTable[i].shared = parentPageTable[i].shared;
        KernelPageTable[i].backed_up = parentPageTable[i].backed_up;

    }

    // Copy the contents
    unsigned startAddrParent = parentPageTable[0].physicalPage*PageSize;
    unsigned startAddrChild = numPagesAllocated*PageSize;
    for (i=0; i<size; i++) {
       machine->mainMemory[startAddrChild+i] = machine->mainMemory[startAddrParent+i];
    }

    // numPagesAllocated += numVirtualPages;
    numPagesAllocated += count;
}





void ProcessAddressSpace::manageChildParentTable(ProcessAddressSpace *parentSpace , int childpid , void * childthread){
    TranslationEntry* parentTable = parentSpace->GetPageTable();
    unsigned size = numVirtualPages* PageSize;

    for(int i=0; i<numVirtualPages;i++){
        KernelPageTable[i].virtualPage = i;

        if(parentTable[i].shared == TRUE){
            // case when parent's address space is shared with the child
            KernelPageTable[i].physicalPage=parentTable[i].physicalPage;
            KernelPageTable[i].valid = parentTable[i].valid;
            KernelPageTable[i].readOnly = parentTable[i].readOnly;
            KernelPageTable[i].use = parentTable[i].use;
            KernelPageTable[i].shared = parentTable[i].shared;
            KernelPageTable[i].dirty = parentTable[i].dirty;
            KernelPageTable[i].backed_up = parentTable[i].backed_up;
        }
        else{
            // case when parent's address space is not shared with the child
            if(parentTable[i].valid == TRUE){
                // case when space is not shared but it is valid.
                IntStatus oldlevel = interrupt->SetLevel(IntOff);

                // TO DO: calculate next physical page to be assigned to KernelPageTable
                KernelPageTable[i].physicalPage = replace_with_next_physpage(parentTable[i].physicalPage);

                pid_of_physpage[KernelPageTable[i].physicalPage] = childpid;
                vpn_of_physpage[KernelPageTable[i].physicalPage] = i;
                physpage_owner[KernelPageTable[i].physicalPage] = (NachOSThread*) childthread;

                physpage_FIFO[KernelPageTable[i].physicalPage] = stats->totalTicks;
                physpage_LRU[KernelPageTable[i].physicalPage] = stats->totalTicks;
                physpage_LRUclock[KernelPageTable[i].physicalPage] = 1;

                for(int j = 0;j<PageSize;j++){
                    machine->mainMemory[KernelPageTable[i].physicalPage*PageSize + j] = machine->mainMemory[parentTable[i].physicalPage*PageSize + j];
                }

                

                physpage_FIFO[parentTable[i].physicalPage] = stats->totalTicks + 1;
                physpage_LRUclock[parentTable[i].physicalPage] = 1;

                physpage_LRU[parentTable[i].physicalPage] = stats->totalTicks + 1;
                

                stats->totalPageFaults = stats->totalPageFaults + 1;

                KernelPageTable[i].valid = parentTable[i].valid;
                KernelPageTable[i].readOnly = parentTable[i].readOnly;
                KernelPageTable[i].use = parentTable[i].use;
                KernelPageTable[i].shared = parentTable[i].shared;
                KernelPageTable[i].dirty = parentTable[i].dirty;
                KernelPageTable[i].backed_up = parentTable[i].backed_up;
                
                (void) interrupt->SetLevel(oldlevel);
                


                   currentThread->SortedInsertInWaitQueue(stats->totalTicks + 1000);
            }

            else 
             {  
                KernelPageTable[i].physicalPage = -1;     //because parent's table is invalid we put child's physical page as -1. 

                KernelPageTable[i].valid = parentTable[i].valid;
                KernelPageTable[i].readOnly = parentTable[i].readOnly;
                KernelPageTable[i].use = parentTable[i].use;
                KernelPageTable[i].shared = parentTable[i].shared;
                KernelPageTable[i].dirty = parentTable[i].dirty;
                KernelPageTable[i].backed_up = parentTable[i].backed_up;

             } 

        }

    }

    for(int j=0; j<size; j++){
    	backup[j] = parentSpace->backup[j];
    }




}




//----------------------------------------------------------------------
// ProcessAddressSpace::~ProcessAddressSpace
// 	Dealloate an address space.  Nothing for now!
//----------------------------------------------------------------------






ProcessAddressSpace::~ProcessAddressSpace()
{

    cleanPages();
}

void ProcessAddressSpace::cleanPages(){
   if(KernelPageTable == NULL)return;
   for(int i=0;i<numVirtualPages;i++){
    if(KernelPageTable[i].valid == TRUE){
        if(KernelPageTable[i].shared == FALSE){
            vpn_of_physpage[KernelPageTable[i].physicalPage]=-1;
            pid_of_physpage[KernelPageTable[i].physicalPage]=-1;
            physpage_owner[KernelPageTable[i].physicalPage] = NULL;
        }
    }
   }
   delete KernelPageTable;
   if(Executable == NULL)return;
   if(pageReplaceAlgo>0)delete Executable;    
}

//----------------------------------------------------------------------
// ProcessAddressSpace::InitUserModeCPURegisters
// 	Set the initial values for the user-level register set.
//
// 	We write these directly into the "machine" registers, so
//	that we can immediately jump to user code.  Note that these
//	will be saved/restored into the currentThread->userRegisters
//	when this thread is context switched out.
//----------------------------------------------------------------------

void
ProcessAddressSpace::InitUserModeCPURegisters()
{
    int i;

    for (i = 0; i < NumTotalRegs; i++)
	machine->WriteRegister(i, 0);

    // Initial program counter -- must be location of "Start"
    machine->WriteRegister(PCReg, 0);	

    // Need to also tell MIPS where next instruction is, because
    // of branch delay possibility
    machine->WriteRegister(NextPCReg, 4);

   // Set the stack register to the end of the address space, where we
   // allocated the stack; but subtract off a bit, to make sure we don't
   // accidentally reference off the end!
    machine->WriteRegister(StackReg, numVirtualPages * PageSize - 16);
    DEBUG('a', "Initializing stack register to %d\n", numVirtualPages * PageSize - 16);
}

//----------------------------------------------------------------------
// ProcessAddressSpace::SaveContextOnSwitch
// 	On a context switch, save any machine state, specific
//	to this address space, that needs saving.
//
//	For now, nothing!
//----------------------------------------------------------------------

void ProcessAddressSpace::SaveContextOnSwitch() 
{}

//----------------------------------------------------------------------
// ProcessAddressSpace::RestoreContextOnSwitch
// 	On a context switch, restore the machine state so that
//	this address space can run.
//
//      For now, tell the machine where to find the page table.
//----------------------------------------------------------------------

void ProcessAddressSpace::RestoreContextOnSwitch() 
{
    machine->KernelPageTable = KernelPageTable;
    machine->KernelPageTableSize = numVirtualPages;
}

unsigned
ProcessAddressSpace::GetNumPages()
{
   return numVirtualPages;
}

TranslationEntry*
ProcessAddressSpace::GetPageTable()
{
   return KernelPageTable;
}

unsigned
ProcessAddressSpace::AllocateSharedMemory(unsigned int size){
    unsigned int num_shared_pages = divRoundUp(size, PageSize);
    unsigned int i, prev_numVirtualPages = numVirtualPages;
    numVirtualPages += num_shared_pages;

    TranslationEntry* newKernelPageTable = new TranslationEntry[numVirtualPages];
    //Copy into new page table
    for (i=0; i<prev_numVirtualPages; i++){
        newKernelPageTable[i].virtualPage = KernelPageTable[i].virtualPage;
        newKernelPageTable[i].physicalPage = KernelPageTable[i].physicalPage;
        newKernelPageTable[i].valid = KernelPageTable[i].valid;
        newKernelPageTable[i].use = KernelPageTable[i].use;
        newKernelPageTable[i].backed_up = KernelPageTable[i].backed_up;
        newKernelPageTable[i].dirty = KernelPageTable[i].dirty;
        newKernelPageTable[i].readOnly = KernelPageTable[i].readOnly;
        newKernelPageTable[i].shared = KernelPageTable[i].shared;
    }

    //set up virtual to physical for shared memory region
    for (i=prev_numVirtualPages; i<numVirtualPages; i++){
        newKernelPageTable[i].virtualPage = i;
        newKernelPageTable[i].physicalPage = i - prev_numVirtualPages + numPagesAllocated;
        newKernelPageTable[i].valid = TRUE;
        newKernelPageTable[i].use = FALSE;
        newKernelPageTable[i].dirty = FALSE;
        newKernelPageTable[i].readOnly = FALSE;
        newKernelPageTable[i].shared = TRUE;
        newKernelPageTable[i].backed_up = FALSE;

        pid_of_physpage[newKernelPageTable[i].physicalPage] = currentThread->GetPID();
        physpage_owner[newKernelPageTable[i].physicalPage] = currentThread;
        physpage_shared[newKernelPageTable[i].physicalPage] = TRUE;
        vpn_of_physpage[newKernelPageTable[i].physicalPage] = i;

    }

    numPagesAllocated += num_shared_pages;

    TranslationEntry *oldKernelPageTable = KernelPageTable;
    KernelPageTable = newKernelPageTable;
    RestoreContextOnSwitch();
    delete oldKernelPageTable;
    stats->sharedPageFaults += num_shared_pages;
    stats->totalPageFaults += num_shared_pages; 
    // printf("Debugging: %d\n", prev_numVirtualPages*PageSize);
    return prev_numVirtualPages * PageSize;
}

bool ProcessAddressSpace::DemandPageAllocation(unsigned BadVAddr){
    int vpn = BadVAddr/PageSize, ppn = replace_with_next_physpage(-1);
    bzero(&machine->mainMemory[ppn*PageSize],PageSize);

    physpage_FIFO[ppn] = stats->totalTicks;

    if(KernelPageTable[vpn].backed_up == TRUE)
    {
        for(int j=0;j<PageSize;j++)
            machine->mainMemory[ppn*PageSize + j] = backup[vpn*PageSize+j];
    }
    else
    {
        NoffHeader noffH;
        if(Executable != NULL)
        {
            Executable->ReadAt((char*)&noffH,sizeof(noffH),0);
            if(noffH.noffMagic != NOFFMAGIC)
            {
                if(WordToHost(noffH.noffMagic) == NOFFMAGIC)
                    SwapHeader(&noffH);
            }
            
            
                Executable->ReadAt(&(machine->mainMemory[ppn*PageSize]),PageSize,noffH.code.inFileAddr + vpn*PageSize);
            
        }
    }
    vpn_of_physpage[ppn] = vpn;
    pid_of_physpage[ppn] = currentThread->GetPID();

    KernelPageTable[vpn].valid = TRUE;
    KernelPageTable[vpn].dirty = FALSE;
    KernelPageTable[vpn].physicalPage = ppn;

    return TRUE;
}


int replace_with_next_physpage(int parent_physpage)
{
    if(pageReplaceAlgo == 0)
        return numPagesAllocated++;
    
    else{
    for(int i=0;i<NumPhysPages;i++)
    {
        if(pid_of_physpage[i] == -1)
            return i;
    }
    }
    // Page fault will occur now
    int page_val;
    if(pageReplaceAlgo == 1)
        page_val = get_random_physpage(parent_physpage);
    // else if(pageReplaceAlgo == 2)
    //     page_val = get_physpage_FIFO(parent_physpage);
    // else if(pageReplaceAlgo == 3)
    //     page_val = get_physpage_LRU(parent_physpage);
    // else
    //     page_val = get_physpage_LRUclock(parent_physpage);

    int pid = pid_of_physpage[page_val], vpn = vpn_of_physpage[page_val];

    if(physpage_shared[page_val] == FALSE)
    {
        if(threadArray[pid]->space->KernelPageTable[vpn].dirty == TRUE)
        {
            // need to backup
            for(int i=0;i<PageSize;i++)
            {
                threadArray[pid]->space->backup[vpn*PageSize+i] = machine->mainMemory[page_val*PageSize+i];
                threadArray[pid]->space->KernelPageTable[vpn].backed_up = TRUE;
            }
        }
        threadArray[pid]->space->KernelPageTable[vpn].valid = FALSE;
        pid_of_physpage[page_val] = -1;
        physpage_owner[page_val] = NULL;
        vpn_of_physpage[page_val] = -1;

        physpage_LRUclock[page_val] = 1;
        physpage_LRU[page_val] = stats->totalTicks;
        
        return page_val;
    }
    return -1;
}

int get_random_physpage(int parent_physpage)
{
    int page_val = Random()%NumPhysPages;
    int vpn = vpn_of_physpage[page_val];

    while(physpage_shared[page_val] == TRUE)
    {
        page_val = Random()%NumPhysPages;
        vpn = vpn_of_physpage[page_val];
    }

    ASSERT(page_val >= 0 && page_val < NumPhysPages);
    return page_val;
}  

// NEED TO COMPLETE THIS!!!!

// int get_physpage_FIFO(int parent_physpage)
// {

// }

// int get_physpage_LRU(int parent_physpage)
// {

// }

// int get_physpage_LRUclock(int parent_physpage)
// {

// }