#pragma once

#include "KernelSystem.hpp"
#include "HelperStructs.hpp"
#include "SsegControlBlock.hpp"
#include "VmDecl.hpp"

class KernelSystem;

// Temp:
int main(int, char**);

class KernelProcess {

    friend class KernelSystem;

    // Temp:
    friend int ::main(int, char**);

    private:

        const ProcessId pid;

        size_t seg_count;

        char *master_table;

        bool master_table_valid;

        ClusterNo mt_disk;

        KernelSystem *owner;

        // Synchronization:
        using RecMutex = std::recursive_mutex;
        using RaiiLock = std::lock_guard<std::recursive_mutex>;
        using UniqLock = std::unique_lock<std::recursive_mutex>;

        // Bonus:
        SsegControlBlock *sscb;

    public:
    
        static const size_t MAX_PAGE_TABLES_L1 = 64;
        static const size_t MAX_SEGMENTS = 192;
        static const size_t PAGE_TABLE_SIZE_L2 = 256;

        static const bool MODE_IN  = true;
        static const bool MODE_OUT = false;

        PageTableL1Entry *ptl1_ptr;
        SegTableEntry    *st_ptr;

        KernelProcess(KernelSystem *owner_, ProcessId pid_);

        ~KernelProcess();

        ProcessId get_pid() const;

        void page_fault(VirtualAddress addr);

        Status create_segment(VirtualAddress start_addr, PageNum size,
                              AccessType acc_type);

        Status load_segment(VirtualAddress startAddress, PageNum segmentSize,
                           AccessType accType, void* content);

        Status delete_segment(VirtualAddress startAddress);
        Status delete_segment_ind(size_t index, bool do_release_shared);

        void set_master_table(PageAnte *page, bool newly_created = false);

        void swap_master_table(bool mode, bool lock);
        void swap_page_table(bool mode, VirtualAddress addr);
        void init_page_table(PageAnte *page);

        static void page_table_evict_children(KernelSystem *system, PageAnte *page, bool destroy);
        void master_table_evict_children(bool destroy);

        size_t master_table_lock();
        size_t page_table_lock(VirtualAddress addr);

        PageTableL1Entry *access_ptl1(VirtualAddress addr, Status &status, bool visit = false);
        PageTableL2Entry *access_ptl2(VirtualAddress addr, PageTableL1Entry *ptl1e, Status &status, bool visit = false);

        char *access_phys(VirtualAddress addr, PageTableL2Entry *ptl2e, Status &status, bool visit = false);

        void *get_pa(VirtualAddress addr);

        // Bonus:
        Status create_shared_segment(VirtualAddress start_addr,
                                     PageNum size, const char* name, AccessType acc_type);

        Status disconnect_shared_segment(const char* name);

        Status delete_shared_segment(const char* name);

        void clone_segment_from(KernelProcess *source, size_t index);

    };
