
#include "Process.h"
#include "KernelProcess.hpp"

Process::Process(ProcessId pid) {

    pProcess = nullptr;

    }

Process::~Process() {

    delete pProcess;

    }

ProcessId Process::getProcessId() const {
    
    return pProcess->get_pid();

    }

Status Process::createSegment(VirtualAddress startAddress, PageNum segmentSize, AccessType accType) {

    return pProcess->create_segment(startAddress, segmentSize, accType);

    }

Status Process::loadSegment(VirtualAddress startAddress, PageNum segmentSize, AccessType accType, void *content) {

    return pProcess->load_segment(startAddress, segmentSize, accType, content);

    }

Status Process::deleteSegment(VirtualAddress startAddress) {

    return pProcess->delete_segment(startAddress);

    }

Status Process::pageFault(VirtualAddress address) {

    pProcess->page_fault(address);

    return OK;

    }

PhysicalAddress Process::getPhysicalAddress(VirtualAddress address) {

    return pProcess->get_pa(address);

    }

void Process::blockIfThrashing() {

    return;

    }

Status Process::createSharedSegment(VirtualAddress startAddress, PageNum segmentSize, const char * name, AccessType flags) {

    return pProcess->create_shared_segment(startAddress, segmentSize, name, flags);

    }

Status Process::disconnectSharedSegment(const char * name) {

    return pProcess->disconnect_shared_segment(name);

    }

Status Process::deleteSharedSegment(const char * name)  {

    return pProcess->delete_shared_segment(name);

    }
