#pragma once

#include <list>

class KernelProcess;
class Process;

struct PcbAndIndex {
    
    KernelProcess *pcb;
    size_t index;

    PcbAndIndex(KernelProcess *pcb_, size_t index_)
        : pcb(pcb_)
        , index(index_) { }
    
    };

struct SsegControlBlock {

    Process *dummy;

    size_t sseg_vec_index;

    std::list<PcbAndIndex> users;
    
    };