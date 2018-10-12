#pragma once

#include "vm_declarations.h"

class KernelProcess;
class System;
class KernelSystem;

// Temp:
int main(int, char**);

class Process {

    // Temp:
    friend int ::main(int, char**);

    private:
        KernelProcess *pProcess;
        friend class System;
        friend class KernelSystem;

    public:

        Process(ProcessId pid);

        ~Process();

        ProcessId getProcessId() const;

        Status createSegment(VirtualAddress startAddress, PageNum segmentSize,
                             AccessType accType);

        Status loadSegment(VirtualAddress startAddress, PageNum segmentSize,
                           AccessType accType, void* content);

        Status deleteSegment(VirtualAddress startAddress);

        Status pageFault(VirtualAddress address);

        PhysicalAddress getPhysicalAddress(VirtualAddress address);

        void blockIfThrashing();

        // Bonus:
        Status createSharedSegment(VirtualAddress startAddress,
                                   PageNum segmentSize, const char* name, AccessType flags);

        Status disconnectSharedSegment(const char* name);

        Status deleteSharedSegment(const char* name);
    
    };