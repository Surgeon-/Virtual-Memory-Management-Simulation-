#pragma once

#include <mutex>
#include <memory>
#include <unordered_map>
#include <random>

#include "VmDecl.hpp"
#include "IntegralTypes.hpp"
#include "ConsVec.hpp"
#include "HelperStructs.hpp"
#include "Part.h"
#include "SsegControlBlock.hpp"

class Partition;
class Process;
class KernelProcess;

class KernelSystem {

    typedef KernelProcess PCB;
    
    private:

        char *userspc;
        char *krnlspc;

        PageNum userspc_size;      
        PageNum krnlspc_size;

        Partition *disk;

        // Empty page management:
        PageAnte *us_empty_lhead;
        PageAnte *us_empty_ltail;

        size_t us_empty_count;

        PageAnte *ks_empty_lhead;
        PageAnte *ks_empty_ltail;

        size_t ks_empty_count;

        size_t lst_link_empty_space(char *start_addr, PageNum n, PageAnte *&head, PageAnte *&tail);      
        PageAnte *lst_get_empty_page(PageAnte *&head, PageAnte *&tail, size_t &count);
        void lst_return_page(PageAnte *page, PageAnte *&head, PageAnte *&tail, size_t &count);

        // Page management - other:
        static const int BITSCAN_MAX_DIFFERENCE = 1;

        PageAnte *ks_acquire_page(PageType::TypeEnum new_type, void *new_owner, bool lock);
        void ks_free_page(size_t ordinal);
        Victim ks_get_victim();
        void ks_swap_out(Victim victim);       
        void ks_ft_update(PageNum entry, Uint8 flags, PageType::TypeEnum type, void *owner);        

        PageNum ks_reserved;

        PageAnte *us_acquire_page(PageType::TypeEnum new_type, void *new_owner, size_t to_ignore = ~(size_t)0);
        void us_free_page(size_t ordinal);
        Victim us_get_victim(size_t to_ignore = ~(size_t)0);
        void us_swap_out(Victim victim);       
        void us_ft_update(PageNum entry, Uint8 flags, PageType::TypeEnum type, void *owner);        

        PageNum us_reserved;

        // Frame tables:
        FrameTableEntry *us_ft_ptr;
        FrameTableEntry *ks_ft_ptr;

        PageNum us_ft_size;
        PageNum ks_ft_size;

        // Origin table:
        Uint16 *ot_ptr;

        size_t ot_entries;

        PageNum ot_size;

        // Disk vacancy table:
        // 1 = free, 0 = ocuppied
        static const size_t DVTE_SIZE = 32;
        
        Uint32 *dvt_ptr;

        ClusterNo disk_size;

        size_t dvt_entries;

        PageNum dvt_size;

        bool dvt_get(ClusterNo cluster);
        void dvt_mark(ClusterNo cluster, bool free);
        bool dvt_acquire_cluster(ClusterNo *n);

        void disk_put(ClusterNo n, const char *buffer);
        void disk_get(ClusterNo n, char *buffer);

        // User processes:
        gen::ConsVec<PCB*> pcb_vec;

        // Synchronization:
        using RecMutex = std::recursive_mutex;
        using RaiiLock = std::lock_guard<std::recursive_mutex>;
        using UniqLock = std::unique_lock<std::recursive_mutex>;

        RecMutex mutex_dvt;
        RecMutex mutex_databus;
        RecMutex mutex_kslst;
        RecMutex mutex_uslst;
        RecMutex mutex_pcbvec;
        RecMutex mutex_ksft;
        RecMutex mutex_usft;
        RecMutex mutex_sseg;

        // Other:
        std::default_random_engine *rng;

        void init_ft_entries();
        bool access_is_ok(Uint8 requested, Uint8 granted) const;

        // Bonus:
        std::unordered_map<std::string, std::unique_ptr<SsegControlBlock>> sseg_map;
        gen::ConsVec<PCB*> sseg_vec;

        size_t sseg_count;

    public:

        static const Uint8 FT_DIRTY  = (1 << FrameTableEntry::Dirty);
        static const Uint8 FT_SHARED = (1 << FrameTableEntry::Shared);
        static const Uint8 FT_LOCKED = (1 << FrameTableEntry::Locked);
        static const Uint8 FT_NONE   = (0);
    
        static const ClusterNo NULL_CLUSTER = 0xFFFF;
        static const ClusterNo MAX_CLUSTERS = 0x10000 - 1;

        static const bool DVT_FREE   = true;
        static const bool DVT_IN_USE = false;

        static const Uint16 SSEG_START_IND = 60000;
        static const size_t MAX_SHARED_SEGMENTS = 256;

        KernelSystem(PhysicalAddress userspc_, PageNum userspc_size_,
                     PhysicalAddress krnlspc_, PageNum krnlspc_size_,
                     Partition* disk_);

        ~KernelSystem();

        Process *create_process();
        void process_deleted(ProcessId pid);

        Time periodic_job();

        Status access(ProcessId pid, VirtualAddress address, AccessType type, bool ignore_access = false);

        void test();
        void diag();

        // Utility:
        size_t ks_page_ordinal(const void *page_ante) const;
        bool ks_page_validate(const void *page_ante) const;
        PageAnte *ks_page_addr(size_t ordinal) const;
        void ks_visited_page(void *page_ante);

        size_t us_page_ordinal(const void *page_ante) const;
        bool us_page_validate(const void *page_ante) const;
        PageAnte *us_page_addr(size_t ordinal) const;
        void us_visited_page(void *page_ante);

        PageAnte *ks_request_page(PageType::TypeEnum new_type, void *new_owner, ClusterNo cluster = NULL_CLUSTER, bool lock = false);
        PageAnte *us_request_page(PageType::TypeEnum new_type, void *new_owner, ClusterNo cluster = NULL_CLUSTER, bool clone_override = false);
        PageAnte *us_load_page(PageType::TypeEnum new_type, void *new_owner, void *content);

        void ks_evict_page(size_t ordinal, bool dirty);
        void us_evict_page(size_t ordinal, bool dirty);

        void ks_lock_page(size_t ordinal);
        void ks_unlock_page(size_t ordinal);

        void ks_relinquish_page(size_t ordinal);
        void us_relinquish_page(size_t ordinal);

        void relinquish_cluster(ClusterNo cluster);

        // Bonus:
        void   shared_segment_pf(size_t sseg_ind, VirtualAddress addr);
        bool   shared_segment_find(const char *name, size_t *index);
        Status create_shared_segment(PageNum size, const char *name, AccessType acc_type);
        Status delete_shared_segment(const char *name);
        void   connect_shared_segment(PCB *pcb, size_t sscb_index, size_t local_index);
        size_t disconnect_shared_segment(PCB *pcb, size_t sscb_index);
        void  *shared_segment_pa(size_t sseg_ind, VirtualAddress addr);

        Process  *clone_process(ProcessId pid);
        PageAnte *us_clone_page(size_t index, void *owner_of_clone);

    };

struct PageUnlocker {

    KernelSystem *sys;
    size_t pg;

    PageUnlocker(KernelSystem *system, size_t page_ordinal) {

        sys = system;
        pg  = page_ordinal;

        }

    PageUnlocker(KernelSystem *system, void *page_addr) {

        sys = system;
        pg  = sys->ks_page_ordinal(page_addr);

        }

    void reset(size_t page_ordinal, bool unlock_current = true) {
        
        if (unlock_current) unlock();

        pg = page_ordinal;
        
        }

    void reset(void *page_addr, bool unlock_current = true) {
        
        if (unlock_current) unlock();

        pg = sys->ks_page_ordinal(page_addr);
        
        }

    void release() {
        
        pg = KernelSystem::NULL_CLUSTER;
        
        }

    void unlock() {
        
        if (pg != KernelSystem::NULL_CLUSTER)
            sys->ks_unlock_page(pg);

        pg = KernelSystem::NULL_CLUSTER;

        }

    ~PageUnlocker() {

        unlock();

        }

    };