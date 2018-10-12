
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

//bool __do_print = false;

using namespace std::chrono;

std::default_random_engine generator1337;

int main3(int, char**) {

    const int US_SIZE = 64;
    const int KS_SIZE = 32;

    Uint8 *us = (Uint8*)_aligned_malloc(US_SIZE * PAGE_SIZE, 1024);
    Uint8 *ks = (Uint8*)_aligned_malloc(KS_SIZE * PAGE_SIZE, 1024);

    Partition part("partition1.ini");

    System s(us, US_SIZE, ks, KS_SIZE, &part);

    //s.diag();

    Process *p1 = s.createProcess();
    Process *p2 = s.createProcess();

    ProcessId pid1 = p1->getProcessId(),
              pid2 = p2->getProcessId();

    std::cout << "Creating segment: " << STATUS_STR(p1->createSegment(0000 * PAGE_SIZE, 1000, READ_WRITE)) << "\n";
    std::cout << "Creating segment: " << STATUS_STR(p1->createSegment(1000 * PAGE_SIZE, 1000, READ_WRITE)) << "\n";
    std::cout << "Creating segment: " << STATUS_STR(p1->createSegment(2000 * PAGE_SIZE, 1000, READ_WRITE)) << "\n";
    std::cout << "Creating segment: " << STATUS_STR(p1->createSegment(3000 * PAGE_SIZE, 1000, READ_WRITE)) << "\n";
    std::cout << "Creating segment: " << STATUS_STR(p1->createSegment(4000 * PAGE_SIZE, 1000, READ_WRITE)) << "\n";

    PRINT_SET_ACTIVE(false);

    /*Status status = s.access(pid1, 0, WRITE);

    if (status == PAGE_FAULT) {
        p1->pageFault(0);
        std::cout << "PF -> ";
        }

    std::cout << STATUS_STR( s.access(pid1, 0, WRITE)) << "\n";

    *static_cast<int*>(p1->getPhysicalAddress(0)) = 420;

    std::cout << *static_cast<int*>(p1->getPhysicalAddress(0)) << "\n";

    status = s.access(pid1, 300*PAGE_SIZE, READ);

    if (status == PAGE_FAULT) {
        p1->pageFault(300*PAGE_SIZE);
        std::cout << "PF -> ";
        }

    std::cout << STATUS_STR( s.access(pid1, 300*PAGE_SIZE, READ)) << "\n";

    status = s.access(pid1, 0, EXECUTE);

    status = s.access(pid1, 0, READ);

    if (status == PAGE_FAULT) {
        p1->pageFault(0);
        std::cout << "PF -> ";
        }

    std::cout << STATUS_STR( s.access(pid1, 0, READ) ) << "\n";

    std::cout << *static_cast<int*>(p1->getPhysicalAddress(0)) << "\n";*/

    // ===================================================================== //

    const int TEST_SIZE = 10000;

    VirtualAddress *va_arr = new VirtualAddress[TEST_SIZE];

    std::uniform_int_distribution<int> distribution(0, 5000 * PAGE_SIZE - 1);

    for (size_t i = 0; i < TEST_SIZE; i += 1) {

        int dice_roll = distribution(generator1337);

        va_arr[i] = dice_roll;

        Status status1 = s.access(pid1, dice_roll, WRITE);

        if (status1 == PAGE_FAULT) {
            p1->pageFault(dice_roll);
            //std::cout << "PF -> ";
            }
        else if (status1 == TRAP) {
            std::cout << "Wrong!\n";
            }

        status1 = s.access(pid1, dice_roll, WRITE);

        if (status1 != OK) {
            std::cout << "Wrong!\n";
            }

        Uint8 *ptr = static_cast<Uint8*>(p1->getPhysicalAddress(dice_roll));

        if (ptr < us || ptr >= (us + US_SIZE * PAGE_SIZE)) {        
            std::cout << "Wrong!\n";
            }

        new (ptr) Uint8(i % 256);

        s.test();

        PRINTLN(" ========================================================= ");

        if (i == 512) PRINT_SET_ACTIVE(false);

        }

    s.test();

    for (size_t i = 0; i < TEST_SIZE; i += 1) {

        VirtualAddress addr = va_arr[i];

        Status status1 = s.access(pid1, addr, READ);

        if (status1 == PAGE_FAULT) {
            p1->pageFault(addr);
            //std::cout << "PF -> ";
            }
        else if (status1 == TRAP) {
            std::cout << "Wrong!\n";
            }

        status1 =  s.access(pid1, addr, READ);

        if (status1 != OK) {
            std::cout << "Wrong!\n";
            }

        Uint8 *ptr = static_cast<Uint8*>(p1->getPhysicalAddress(addr));

        if (*ptr != (i % 256)) {
            
            bool duplicate = false;

            for (size_t t = 0; t < TEST_SIZE; t += 1) {
                
                if (t != i && va_arr[t] == va_arr[i]) {
                    
                    duplicate = true;
                    break;
                    
                    }
                
                }

            if (!duplicate) std::cout << "Wrong!\n";
            
            }

        s.test();

        if (i == 512) PRINT_SET_ACTIVE(false);

        }

    delete p1;

    std::cout << "Good job my nigga!\n";

    _aligned_free(us);
    _aligned_free(ks);

    getchar();

    return 0;

    /*
    std::cout << STATUS_STR( s.access(pid1, 5678, READ)) << "\n";
    p1->pageFault(5678);
    std::cout << STATUS_STR( s.access(pid1, 5678, READ)) << "\n";
    p1->pProcess->master_table_evict_children(false);
    std::cout << STATUS_STR( s.access(pid1, 5678, READ)) << "\n";
    p1->pageFault(5678);
    std::cout << STATUS_STR( s.access(pid1, 5678, READ)) << "\n";
    

    auto start = std::chrono::high_resolution_clock::now();
    
    //std::cout << "Creating segment: " << STATUS_STR(p1->createSegment( 0 * PAGE_SIZE, 16, READ_WRITE)) << "\n";
    //std::cout << "Creating segment: " << STATUS_STR(p1->createSegment(16 * PAGE_SIZE, 16, READ_WRITE)) << "\n";
    //std::cout << "Creating segment: " << STATUS_STR(p1->createSegment(20 * PAGE_SIZE, 16, READ_WRITE)) << "\n";

    //std::cout << "Deleting segment: " << STATUS_STR(p1->deleteSegment( 0 * PAGE_SIZE)) << "\n";
    //std::cout << "Deleting segment: " << STATUS_STR(p1->deleteSegment(16 * PAGE_SIZE)) << "\n";

    s.test();

    auto elapsed = std::chrono::high_resolution_clock::now() - start;
    long long microseconds = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
    std::cout << "Elapsed time = " << microseconds << " microsecs.\n";

    //s.diag();

    //std::cout << STATUS_STR( s.access(pid1, 4, READ)) << "\n";
    //std::cout << STATUS_STR( s.access(pid1, 5000, READ)) << "\n";
    //std::cout << STATUS_STR( s.access(pid1, 4, EXECUTE)) << "\n";

    //std::cout << STATUS_STR(p2->createSegment(0, 128, READ_WRITE)) << "\n";

    //s.test();

    getchar();

    std::vector<int> vec;

    return 0;*/

    }