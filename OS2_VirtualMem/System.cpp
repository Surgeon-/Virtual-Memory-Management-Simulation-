
#include "System.h"
#include "KernelSystem.hpp"
#include "VmDecl.hpp"

System::System(PhysicalAddress processVMSpace, PageNum processVMSpaceSize,
               PhysicalAddress pmtSpace, PageNum pmtSpaceSize,
               Partition* partition) {
    
    pSystem = new KernelSystem(processVMSpace, processVMSpaceSize,
                               pmtSpace, pmtSpaceSize,
                               partition);
    
    }

System::~System() {
    
    delete pSystem;
    
    }

Process *System::createProcess() {
    
    return pSystem->create_process();

    }

Time System::periodicJob() {
    
    return pSystem->periodic_job();
    
    }

Status System::access(ProcessId pid, VirtualAddress address, AccessType type) {

    return pSystem->access(pid, address, type);

    }

Process *System::cloneProcess(ProcessId pid) {

    return pSystem->clone_process(pid);

    }

void System::test() {
    
    pSystem->test();
    
    }

void System::diag() {
    
    pSystem->diag();
    
    }