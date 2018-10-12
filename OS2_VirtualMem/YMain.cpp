
#include <vector>
#include <iostream>
#include <chrono>

#include "IntegralTypes.hpp"
#include "System.h"
#include "Process.h"

#include "ConsVec.hpp"
#include "Part.h"
#include "Macros.hpp"
#include "PSpecFunc.hpp"

#include <chrono>
#include <cstdlib>
#include <random>
#include <windows.h>
#include <new>

#include "VmDecl.hpp"
#include "KernelProcess.hpp"

#ifndef PAGE_SIZE
#define PAGE_SIZE 1024
#endif

PRINT_INIT(false);

using namespace std::chrono;

std::default_random_engine generator3;

int main2(int, char**) {

    const int US_SIZE = 2;
    const int KS_SIZE = 128;

    Uint8 *us = (Uint8*)_aligned_malloc(US_SIZE * PAGE_SIZE, 1024);
    Uint8 *ks = (Uint8*)_aligned_malloc(KS_SIZE * PAGE_SIZE, 1024);

    Partition part("partition1.ini");

    System sys(us, US_SIZE, ks, KS_SIZE, &part);

    Process *p1 = sys.createProcess(), *p2;

    ProcessId pid1 = p1->getProcessId(), pid2;
    
    Status status1, status2;

    PRINT_SET_ACTIVE(true);

    p1->createSegment(0, 4, READ_WRITE);

    PRINTLN(" ===================================== ");

    VirtualAddress addr = 4;

    status1 = sys.access(pid1, addr, WRITE);

    if (status1 == PAGE_FAULT)
        status1 = p1->pageFault(addr);

    void *ptr1 = p1->getPhysicalAddress(addr);

    new (ptr1) Uint8(1);

    // ---

    addr = 512;

    status1 = sys.access(pid1, addr, WRITE);

    if (status1 == PAGE_FAULT)
        status1 = p1->pageFault(addr);

    ptr1 = p1->getPhysicalAddress(addr);

    new (ptr1) Uint8(2);

    // ---

    addr = 1536;

    status1 = sys.access(pid1, addr, WRITE);

    if (status1 == PAGE_FAULT)
        status1 = p1->pageFault(addr);

    ptr1 = p1->getPhysicalAddress(addr);

    new (ptr1) Uint8(3);

    PRINTLN(" ===================================== ");

    p2 = sys.cloneProcess(pid1);

    pid2 = p2->getProcessId();

    PRINTLN(" ===================================== ");

    addr = 4;

    status2 = sys.access(pid2, addr, READ);

    if (status2 == PAGE_FAULT)
        status2 = p2->pageFault(addr);

    void *ptr2 = p2->getPhysicalAddress(addr);

    std::cout << "Read = " << (int)*(Uint8*)ptr2 << "\n";

    // ---

    addr = 512;

    status2 = sys.access(pid2, addr, READ);

    if (status2 == PAGE_FAULT)
        status2 = p2->pageFault(addr);

    ptr2 = p2->getPhysicalAddress(addr);

    std::cout << "Read = " << (int)*(Uint8*)ptr2 << "\n";

    // ---

    addr = 1536;

    status2 = sys.access(pid2, addr, WRITE);

    if (status2 == PAGE_FAULT)
        status2 = p2->pageFault(addr);

    ptr2 = p2->getPhysicalAddress(addr);

    std::cout << "Read = " << (int)*(Uint8*)ptr2 << "\n";

    PRINTLN(" ===================================== ");

    delete p1;
    delete p2;

    _aligned_free(us);
    _aligned_free(ks);

    std::cout << "Test completed!\n";

    getchar();

    return 0;

    }