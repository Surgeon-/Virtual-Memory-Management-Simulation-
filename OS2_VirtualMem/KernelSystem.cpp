
#include "KernelSystem.hpp"
#include "KernelProcess.hpp"
#include "IntegralTypes.hpp"
#include "VmDecl.hpp"
#include "Macros.hpp"
#include "PSpecFunc.hpp"
#include "SsegControlBlock.hpp"

#include "Part.h"
#include "Process.h"

#include <iostream> // Debug

#include <new>
#include <intrin.h>
#include <stdexcept>
#include <random>
#include <cstring>

// Thread safety: Not needed ('Structor)
KernelSystem::KernelSystem(PhysicalAddress userspc_, PageNum userspc_size_,
                           PhysicalAddress krnlspc_, PageNum krnlspc_size_,
                           Partition* disk_)
    : userspc(static_cast<char*>(userspc_))
    , krnlspc(static_cast<char*>(krnlspc_))
    , userspc_size(userspc_size_)
    , krnlspc_size(krnlspc_size_)
    , disk(disk_)
    , pcb_vec(256) {

    ks_reserved = 0;
    us_reserved = 0;

    // Frame Tables:
    us_ft_size = DIV_CEIL(userspc_size * sizeof(FrameTableEntry), PAGE_SIZE);
    ks_ft_size = DIV_CEIL(krnlspc_size * sizeof(FrameTableEntry), PAGE_SIZE);

    ks_ft_ptr = reinterpret_cast<FrameTableEntry*>(krnlspc);
    us_ft_ptr = ks_ft_ptr + (ks_ft_size * PAGE_SIZE / sizeof(FrameTableEntry));

    ks_reserved += us_ft_size + ks_ft_size;

    if (ks_reserved > krnlspc_size) 
        HALT("KernelSystem::KernelSystem - The system needs at least " << ks_reserved + 2 << " pages to be able to work.");

    init_ft_entries();

    // Disk vacancy table:
    dvt_ptr = reinterpret_cast<Uint32*>(krnlspc + ks_reserved * PAGE_SIZE);

    disk_size = MIN(disk->getNumOfClusters(), MAX_CLUSTERS);

    dvt_entries = DIV_CEIL(disk_size, DVTE_SIZE);

    dvt_size = DIV_CEIL(dvt_entries * sizeof(Uint32), PAGE_SIZE);

    ks_reserved += dvt_size;

    if (ks_reserved > krnlspc_size) 
        HALT("KernelSystem::KernelSystem - The system needs at least " << ks_reserved + 2 << " pages to be able to work.");

    for (size_t i = 0; i < dvt_entries; i += 1) {
        
        new (dvt_ptr + i) Uint32(0xFFFFFFFF);
        
        }

    if (disk_size % DVTE_SIZE != 0) {

        for (size_t t = 0; t < (DVTE_SIZE - disk_size % DVTE_SIZE); t += 1) {
        
            dvt_ptr[dvt_entries - 1] ^= (1 << t);

            }

        }

    // Origin table:
    ot_ptr = reinterpret_cast<Uint16*>(krnlspc + ks_reserved * PAGE_SIZE);

    ot_size = DIV_CEIL(userspc_size * sizeof(Uint16), PAGE_SIZE);

    ks_reserved += ot_size;

    if (ks_reserved > krnlspc_size) 
        HALT("KernelSystem::KernelSystem - The system needs at least " << ks_reserved + 2 << " pages to be able to work.");

    // Make linked list from empty pages:
    us_empty_count = lst_link_empty_space(userspc, userspc_size, us_empty_lhead, us_empty_ltail);

    ks_empty_count = lst_link_empty_space(krnlspc + ks_reserved * PAGE_SIZE,
                                          krnlspc_size - ks_reserved,
                                          ks_empty_lhead, ks_empty_ltail);
        /* Kernel space is reduced in order to house frame (and other) tables */

    // Other:
    rng = new std::default_random_engine();

    // Bonus:
    sseg_count = 0;

    // Debug:
    // diag();

    }

// Thread safety: Not needed ('Structor)
KernelSystem::~KernelSystem() {
    
    delete rng;

    if (!sseg_map.empty())
        HALT("KernelSystem::~KernelSystem - Not all shared segments were deleted; memory leak!");

    }

// Thread safety: Not needed (Debug)
bool KernelSystem::dvt_get(ClusterNo cluster) {

    size_t entry = cluster / DVTE_SIZE;

    size_t bit = DVTE_SIZE - cluster % DVTE_SIZE - 1;

    return BIT_GET(dvt_ptr[entry], bit);

    }

// Thread safety: Yes (mutex_dvt)
void KernelSystem::dvt_mark(ClusterNo cluster, bool free) {

    RaiiLock rl(mutex_dvt);

    size_t entry = cluster / DVTE_SIZE;

    size_t bit = DVTE_SIZE - cluster % DVTE_SIZE - 1;

    dvt_ptr[entry] = BIT_VAL(dvt_ptr[entry], bit, (Uint32)free);

    }

// Thread safety: Yes (mutex_dvt)
bool KernelSystem::dvt_acquire_cluster(ClusterNo *n) {

    RaiiLock rl(mutex_dvt);

    size_t i, found = false;

    for (i = 0; i < dvt_entries; i += 1) {
        
        if (dvt_ptr[i] != 0) {
            
            found = true;
            break;
            
            }
        
        }

    if (found == false) return false;

    unsigned long ind;

    _BitScanReverse(&ind, dvt_ptr[i]);

    *n = i * DVTE_SIZE + (DVTE_SIZE - ind - 1);

    // Mark as taken:
    //if (*n == 584)
    //    PAUSE();

    dvt_ptr[i] = BIT_CLR(dvt_ptr[i], ind);

    return true;

    }

// Thread safety: Yes (Wrapper)
void KernelSystem::disk_put(ClusterNo n, const char *buffer) {

    PRINT("Disk_put writing to cluster " << n);
    PRINTLN(" (ordinal(us/ks) = " << us_page_ordinal(buffer) << " / " << ks_page_ordinal(buffer) << ").");

    if (disk->writeCluster(n, buffer) != 1) {
        
        HALT("KernelSystem::disk_put - Partition failure.");

        }

    }

// Thread safety: Yes (Wrapper)
void KernelSystem::disk_get(ClusterNo n, char *buffer) {

    PRINT("Disk_get reading from cluster " << n);
    PRINTLN(" (ordinal(us/ks) = " << us_page_ordinal(buffer) << " / " << ks_page_ordinal(buffer) << ").");

    if (disk->readCluster(n, buffer) != 1) {
        
        HALT("KernelSystem::disk_get - Partition failure.");

        }

    }

// Thread safety: Not needed
void KernelSystem::init_ft_entries() {
    
    for (size_t i = 0; i < krnlspc_size; i += 1) {
        
        new (ks_ft_ptr + i) FrameTableEntry();
        
        }

    for (size_t i = 0; i < userspc_size; i += 1) {

        new (us_ft_ptr + i) FrameTableEntry();

        }
    
    }

// Thread safety: Yes (Const)
bool KernelSystem::access_is_ok(Uint8 requested, Uint8 granted) const {

    return ((0x8465 & (1 << ((requested << 2) | granted))) != 0);

    }

// Thread safety: Not needed (Constructor only)
size_t KernelSystem::lst_link_empty_space(char *start_addr, PageNum n, PageAnte *&head, PageAnte *&tail) {
    
    head = new (start_addr) PageAnte();

    PageAnte *curr = head;

    for (PageNum i = 1; i < n; i += 1) {

        PageAnte *prev = curr;

        //std::cout << curr << "\n";

        curr->next_empty = new (start_addr + i * PAGE_SIZE) PageAnte();

        curr = curr->next_empty;

        //std::cout << "Diff = " << ((char*)curr - (char*)prev) << "\n";

        }

    tail = curr;

    return n;
    
    }

// Thread safety: Not needed (Wrapped)
PageAnte *KernelSystem::lst_get_empty_page(PageAnte *&head, PageAnte *&tail, size_t &count) {
    
    if (count == 0) return nullptr;

    PageAnte *rv = head;

    if (head != tail) {

        if (reinterpret_cast<unsigned>(rv->next_empty) == 0xcdcdcdcd) {
            std::cout << "";
            }

        head = rv->next_empty;

        }
    else {

        head = nullptr;
        tail = nullptr;
        
        }

    count -= 1;

    return rv;

    }

// Thread safety: Not needed (Wrapped)
void KernelSystem::lst_return_page(PageAnte *page, PageAnte *&head, PageAnte *&tail, size_t &count) {
    
    if (head == nullptr) {
        
        head = page;
        tail = page;
        
        }
    else {
        
        tail->next_empty = page;

        tail = page;
        
        }

    page->next_empty = nullptr;

    count += 1;

    }

// Thread safety: Yes (mutex_kslst, Wrapper)
PageAnte *KernelSystem::ks_acquire_page(PageType::TypeEnum new_type, void *new_owner, bool lock) {

    UniqLock ul(mutex_kslst);

    PageAnte *rv = lst_get_empty_page(ks_empty_lhead, ks_empty_ltail, ks_empty_count);

    if (rv == nullptr) {

        ks_swap_out( ks_get_victim() );
        rv = lst_get_empty_page(ks_empty_lhead, ks_empty_ltail, ks_empty_count);

        }

    ul.unlock();

    if (rv == nullptr) {

        HALT("KernelSystem::ks_acquire_page - Could not acquire page.");

        }

    Uint8 flags = FT_NONE;

    if (lock) flags |= FT_LOCKED;

    ks_ft_update(ks_page_ordinal(rv), flags, new_type, new_owner);

    return rv;

    }

// Thread safety: Yes (mutex_kslst)
void KernelSystem::ks_free_page(size_t ordinal) {
    
    RaiiLock rl(mutex_kslst);

    lst_return_page(ks_page_addr(ordinal), ks_empty_lhead, ks_empty_ltail, ks_empty_count);

    }

// Thread safety: Yes (mutex_ksft)
Victim KernelSystem::ks_get_victim() {
   
    RaiiLock rl(mutex_ksft);

    size_t place;

    std::uniform_int_distribution<size_t> distribution(ks_reserved, krnlspc_size - 1);

    size_t dice_roll = distribution(*rng);

    place = dice_roll;

    PASS_ON:

    if (ks_ft_ptr[place].type == PageType::KsSegTable) {
        
        auto *ptl1_ptr = reinterpret_cast<PageTableL1Entry*>(ks_page_addr(place));

        for (size_t i = 0; i < KernelProcess::MAX_PAGE_TABLES_L1; i += 1) {
            
            size_t pg_tbl_l2_block_disk = ptl1_ptr[i].block_disk;

            if (ptl1_ptr[i].status == PageTableL1Entry::Present &&
                !ks_ft_ptr[pg_tbl_l2_block_disk].get_locked()) {
                
                place = pg_tbl_l2_block_disk;
                break;

                }
            
            }
        
        }

    if (ks_ft_ptr[place].get_locked()) {
        
        while (true) {
            
            place += 1;

            if (place >= krnlspc_size) place = ks_reserved;

            if (ks_ft_ptr[place].get_locked()) continue;

            if (ks_ft_ptr[place].type == PageType::KsSegTable) goto PASS_ON;

            break;
            
            }
        
        }

    return Victim(place, true, NULL_CLUSTER);

    }

// Thread safety: Yes (mutex_ksft, Wrapper)
void KernelSystem::ks_swap_out(Victim victim) {

    RaiiLock rl(mutex_ksft);

    size_t ordinal = victim.ordinal;
    ClusterNo cn = victim.cluster;

    // Prepare to write page to disk/pool:
    bool write_back = false;

    if (victim.dirty || cn == NULL_CLUSTER) {

        if (cn == NULL_CLUSTER) {

            if (!dvt_acquire_cluster(&cn)) {

                HALT("KernelSystem::ks_swap_out - Disk is full.");

                }

            }

        write_back = true;

        }

    // Update the state of the victim's owner:
    Uint8  type = ks_ft_ptr[ordinal].type;

    void *owner = ks_ft_ptr[ordinal].owner;

    switch (type) {  

            case PageType::KsPageTable: {
                // Swap out child pages - PEP
                KernelProcess::page_table_evict_children(this, ks_page_addr(ordinal), false);
                // Update owner:
                PageTableL1Entry *ptl1e = static_cast<PageTableL1Entry*>(owner);
                ptl1e->block_disk = (Uint16)cn;
                ptl1e->status = PageTableL1Entry::PagedOut;                
                }
                break;

            case PageType::KsSegTable: {
                PCB *pcb = static_cast<PCB*>(owner);
                // Swap out child pages - PEP
                pcb->master_table_evict_children(false);
                // Update owner:               
                pcb->mt_disk = cn;
                pcb->master_table_valid = false;           
                }
                break;

            case PageType::KsUnused:
            case PageType::KsReserved:
            case PageType::UsUserPage:
            case PageType::UsUnused:
            case PageType::UsReserved:
            default:
                HALT("KernelSystem::ks_swap_out - Page is of incorrect type (" << (int)type << ").");
                break;

        }

    // Write page to disk if needed:
    if (write_back)
        disk_put(cn, reinterpret_cast<char*>(ks_page_addr(ordinal)));

    // Update frame table:
    ks_ft_ptr[ordinal].type = PageType::KsUnused;

    // Insert into list of unused pages:
    PRINT("Swapped out page from frame " << ordinal << " (KS).\n");

    ks_free_page(ordinal);

    }

// Thread safety: Yes (Const)
size_t KernelSystem::ks_page_ordinal(const void *page) const {

    return ((reinterpret_cast<const char*>(page) - krnlspc) / PAGE_SIZE);

    }

// Thread safety: Yes (Const)
bool KernelSystem::ks_page_validate(const void *page_ante) const {

    return (((reinterpret_cast<const char*>(page_ante) - krnlspc) % PAGE_SIZE) == 0);

    }

// Thread safety: Yes (Const)
PageAnte *KernelSystem::ks_page_addr(size_t ordinal) const {

    return reinterpret_cast<PageAnte*>(krnlspc + ordinal * PAGE_SIZE);

    }

// Thread safety: Yes (mutex_ksft)
void KernelSystem::ks_ft_update(PageNum entry, Uint8 flags, PageType::TypeEnum type, void * owner) {

    RaiiLock rl(mutex_ksft);

    //ks_ft_ptr[entry].ref_history |= FrameTableEntry::REF_TOP;
    ks_ft_ptr[entry].flags = flags;
    ks_ft_ptr[entry].type = type;
    ks_ft_ptr[entry].owner = owner;

    }

// Thread safety: Not needed (Disabled method)
void KernelSystem::ks_visited_page(void *page_ante) {

    /*size_t ordinal = ks_page_ordinal(page_ante);

    ks_ft_ptr[ordinal].ref_history |= FrameTableEntry::REF_TOP;*/

    }

// Thread safety: Yes (mutex_uslst, Wrapper)
PageAnte *KernelSystem::us_acquire_page(PageType::TypeEnum new_type, void *new_owner, size_t to_ignore) {
    
    UniqLock ul(mutex_uslst);

    PageAnte *rv = lst_get_empty_page(us_empty_lhead, us_empty_ltail, us_empty_count);

    if (rv == nullptr) {
        
        us_swap_out( us_get_victim(to_ignore) ); // PEP
        rv = lst_get_empty_page(us_empty_lhead, us_empty_ltail, us_empty_count);

        }

    ul.unlock();

    if (rv == nullptr) {
        
        HALT("KernelSystem::us_acquire_page - Could not acquire page.");
        
        }

    us_ft_update(us_page_ordinal(rv), FT_NONE, new_type, new_owner);

    return rv;

    }

// Thread safety: Yes (mutex_uslst, Wrapper)
void KernelSystem::us_free_page(size_t ordinal) {

    RaiiLock rl(mutex_uslst);

    lst_return_page(us_page_addr(ordinal), us_empty_lhead, us_empty_ltail, us_empty_count);

    }

// Thread safety: Yes (mutex_usft)
Victim KernelSystem::us_get_victim(size_t to_ignore) {

    RaiiLock rl(mutex_usft);

    size_t place = us_reserved;

    std::uniform_int_distribution<size_t> distribution(us_reserved, userspc_size - 1);

    RETRY:

    place = distribution(*rng);

    if (place == to_ignore) goto RETRY;

    return Victim(place, us_ft_ptr[place].get_dirty(), ot_ptr[place]);

    }

// Thread safety: Yes (mutex_usft, Wrapper)
void KernelSystem::us_swap_out(Victim victim) {

    RaiiLock rl(mutex_usft);

    size_t ordinal = victim.ordinal;
    ClusterNo cn = victim.cluster;

    // Write page to disk/pool:
    if (victim.dirty || cn == NULL_CLUSTER) {

        if (cn == NULL_CLUSTER) {

            if (!dvt_acquire_cluster(&cn)) {

                HALT("KernelSystem::us_swap_out - Disk is full.");

                }

            }

        disk_put(cn, reinterpret_cast<char*>(us_page_addr(ordinal)));

        }

    // Update the state of the victim's owner:
    Uint8  type = us_ft_ptr[ordinal].type;

    void *owner = us_ft_ptr[ordinal].owner;

    switch (type) {       

        case PageType::UsUserPage: {
            PageTableL2Entry *pte = static_cast<PageTableL2Entry*>(owner);
            pte->block_disk = (Uint16)cn;
            pte->set_valid(false);
            pte->set_dirty(false);
            }
            break;

        case PageType::KsPageTable:
        case PageType::KsSegTable:
        case PageType::UsUnused:
        case PageType::UsReserved:
        case PageType::KsUnused:
        case PageType::KsReserved:
        default:
            HALT("KernelSystem::us_swap_out - Page is of incorrect type (" << (int)type << ").");
            break;
   
        }

    // Update frame table:
    us_ft_ptr[ordinal].type = PageType::UsUnused;

    // Insert into list of unused pages:
    PRINT("Swapped out page from frame " << ordinal << " (US).\n");

    us_free_page(ordinal);

    }

// Thread safety: Yes (Const)
size_t KernelSystem::us_page_ordinal(const void *page_ante) const {

    return ((reinterpret_cast<const char*>(page_ante) - userspc) / PAGE_SIZE);

    }

// Thread safety: Yes (Const)
bool KernelSystem::us_page_validate(const void *page_ante) const {

    return (((reinterpret_cast<const char*>(page_ante) - userspc) % PAGE_SIZE) == 0);

    }

// Thread safety: Yes (Const)
PageAnte *KernelSystem::us_page_addr(size_t ordinal) const {

    return reinterpret_cast<PageAnte*>(userspc + ordinal * PAGE_SIZE);

    }

// Thread safety: Yes (mutex_usft)
void KernelSystem::us_ft_update(PageNum entry, Uint8 flags, PageType::TypeEnum type, void *owner) {

    RaiiLock rl(mutex_usft);

    //us_ft_ptr[entry].ref_history |= FrameTableEntry::REF_TOP;
    us_ft_ptr[entry].flags = flags;
    us_ft_ptr[entry].type = type;
    us_ft_ptr[entry].owner = owner;

    }

// Thread safety: Not needed (Disabled method)
void KernelSystem::us_visited_page(void *page_ante) {

    /*size_t ordinal = us_page_ordinal(page_ante);

    us_ft_ptr[ordinal].ref_history |= FrameTableEntry::REF_TOP;*/

    }

// Thread safety: Yes (Wrapper)
PageAnte *KernelSystem::ks_request_page(PageType::TypeEnum new_type, void *new_owner, ClusterNo cluster, bool lock) {

    PageAnte *page = ks_acquire_page(new_type, new_owner, lock);

    if (cluster != NULL_CLUSTER) {
        
        disk_get(cluster, reinterpret_cast<char*>(page));

        dvt_mark(cluster, DVT_FREE);

        }

    PRINT( "System gave page " << ks_page_ordinal(page) << " (KS) to user process; Type = " << new_type << ".\n");

    return page;

    }

// Thread safety: Yes (Wrapper)
PageAnte *KernelSystem::us_request_page(PageType::TypeEnum new_type, void *new_owner, ClusterNo cluster, bool clone_override) {

    PageAnte *page = us_acquire_page(new_type, new_owner);

    size_t ordinal = us_page_ordinal(page);

    if (cluster == NULL_CLUSTER){
        ot_ptr[ordinal] = NULL_CLUSTER;
        }
    else {
        ot_ptr[ordinal] = (Uint16)cluster;
        disk_get(cluster, reinterpret_cast<char*>(page));
        }

    if (clone_override) ot_ptr[ordinal] = NULL_CLUSTER;
    
    PRINT( "System gave page " << ordinal << " (US) to user process; Type = " << new_type << ".\n");

    return page;

    }

// Thread safety: Yes (mutex_uslst, mutex_usft, Wrapper)
PageAnte *KernelSystem::us_load_page(PageType::TypeEnum new_type, void *new_owner, void *content) {

    RaiiLock rl1(mutex_uslst);
    RaiiLock rl2(mutex_usft);

    PageAnte *page = us_acquire_page(new_type, new_owner);

    size_t ordinal = us_page_ordinal(page);

    ot_ptr[ordinal] = NULL_CLUSTER;

    std::memcpy(page, content, PAGE_SIZE);

    PRINT( "System gave page " << ordinal << " (US) to user process; Type = " << new_type << ".\n");

    return page;

    }

// Thread safety: Yes (Wrapper)
void KernelSystem::ks_evict_page(size_t ordinal, bool dirty) {

    ClusterNo cluster;

    if (!dvt_acquire_cluster(&cluster)) {
        
        HALT("KernelSystem::ks_evict_pace - Disk is full.");

        }

    ks_swap_out( Victim(ordinal, dirty, cluster) );

    // swap_out sets block_disk to appropriate value

    }

// Thread safety: Yes (Wrapper)
void KernelSystem::us_evict_page(size_t ordinal, bool dirty) {

    ClusterNo cluster = ot_ptr[ordinal];

    us_swap_out( Victim(ordinal, dirty, cluster) );

    // swap_out sets block_disk to appropriate value

    }

// Thread safety: Yes (mutex_ksft)
void KernelSystem::ks_lock_page(size_t ordinal) {

    RaiiLock rl(mutex_ksft);

    ks_ft_ptr[ordinal].set_locked(true);

    }

// Thread safety: Yes (mutex_ksft)
void KernelSystem::ks_unlock_page(size_t ordinal) {

    RaiiLock rl(mutex_ksft);

    ks_ft_ptr[ordinal].set_locked(false);

    }

// Thread safety: Yes (Wrapper)
void KernelSystem::ks_relinquish_page(size_t ordinal) {

    ks_free_page(ordinal);

    }

// Thread safety: Yes (Wrapper)
void KernelSystem::us_relinquish_page(size_t ordinal) {

    if (ot_ptr[ordinal] != NULL_CLUSTER) {

        dvt_mark(ot_ptr[ordinal], DVT_FREE);

        }

    us_free_page(ordinal);

    }

// Thread safety: Yes (Wrapper)
void KernelSystem::relinquish_cluster(ClusterNo cluster) {

    if (cluster == NULL_CLUSTER || cluster >= disk_size)
        HALT("KernelSystem::relinquish_cluster - Invalid cluster index.");

    dvt_mark(cluster, DVT_FREE);

    }

// Thread safety: Yes (mutex_sseg, Wrapper)
void KernelSystem::shared_segment_pf(size_t sseg_ind, VirtualAddress addr) {

    RaiiLock rl(mutex_sseg);

    PCB *pcb = sseg_vec[sseg_ind];

    pcb->page_fault(addr);

    }

// Thread safety: Yes (mutex_sseg, Wrapper)
bool KernelSystem::shared_segment_find(const char *name, size_t *index) {

    RaiiLock rl(mutex_sseg);

    auto iter = sseg_map.find( std::string(name) );

    if (iter == sseg_map.end()) return false;

    *index = (*iter).second.get()->sseg_vec_index;

    return true;

    }

// Thread safety: Yes (mutex_sseg, Wrapper)
Status KernelSystem::create_shared_segment(PageNum size, const char *name, AccessType acc_type) {

    RaiiLock rl(mutex_sseg);

    // Assume segment with same name doesn't already exist ...

    if (sseg_count == MAX_SHARED_SEGMENTS) return TRAP;

    // Insert data into all relevant data structures:

    SsegControlBlock *sscb = new SsegControlBlock();

    sseg_map.emplace(std::string(name), sscb); // sseg_map

    Process *dummy = create_process(); // pcb_vec

    PCB *pcb = dummy->pProcess;  

    size_t index = sseg_vec.insert(pcb); // sseg_vec
    
    // Other:
    sscb->dummy = dummy;
    sscb->sseg_vec_index = index;

    pcb->sscb = sscb;

    sseg_count += 1;

    return dummy->createSegment(0, size, acc_type);

    }

// Thread safety: Yes (mutex_sseg, Wrapper)
Status KernelSystem::delete_shared_segment(const char *name) {

    RaiiLock rl(mutex_sseg);

    // Assume segment with the name exists ...

    SsegControlBlock *sscb = sseg_map[std::string(name)].get();

    for (auto iter = (sscb->users).begin(); iter != (sscb->users).end(); iter = std::next(iter)) {

        PCB *pcb = (*iter).pcb;
        size_t index = (*iter).index;

        pcb->delete_segment_ind(index, false);

        }

    sscb->users.clear();

    delete (sscb->dummy);

    sseg_vec.mark_empty( sscb->sseg_vec_index );

    sseg_map.erase(std::string(name));

    return OK;

    }

// Thread safety: Yes (mutex_sseg, Wrapper)
void KernelSystem::connect_shared_segment(PCB * pcb, size_t sscb_index, size_t local_index) {

    RaiiLock rl(mutex_sseg);

    SsegControlBlock *sscb = sseg_vec[sscb_index]->sscb;

    sscb->users.push_back(PcbAndIndex(pcb, local_index));

    }

// Thread safety: Yes (mutex_sseg, Wrapper)
size_t KernelSystem::disconnect_shared_segment(PCB *pcb, size_t sscb_index) {

    RaiiLock rl(mutex_sseg);

    SsegControlBlock *sscb = sseg_vec[sscb_index]->sscb;

    for (auto iter = (sscb->users).begin(); iter != (sscb->users).end(); iter = std::next(iter)) {

        if ((*iter).pcb == pcb) {

            size_t rv = (*iter).index;

            sscb->users.erase(iter);

            return rv;

            }

        }

    HALT("KernelSystem::disconnect_shared_segment - No match found.");

    }

// Thread safety: Yes (mutex_sseg, Wrapper)
void *KernelSystem::shared_segment_pa(size_t sseg_ind, VirtualAddress addr) {

    RaiiLock rl(mutex_sseg);

    PCB *pcb = sseg_vec[sseg_ind];

    return pcb->get_pa(addr);

    }

// Thread safety: Yes (mutex_pcbvec, mutex_uslst, mutex_usft, Wrapper)
Process *KernelSystem::clone_process(ProcessId pid) {

    RaiiLock rl1(mutex_pcbvec);
    RaiiLock rl2(mutex_uslst);
    RaiiLock rl3(mutex_usft);

    PCB *source = pcb_vec.at(pid);

    source->master_table_lock();

    // Make and connect PCB and Process objects:
    size_t index = pcb_vec.insert(nullptr);

    PCB *pcb = new PCB(this, index);

    pcb_vec[index] = pcb;

    Process *proc = new Process(0);

    proc->pProcess = pcb;

    // Set up the page/segment table for created process:
    PageAnte *page = ks_acquire_page(PageType::KsSegTable, pcb, true);

    pcb->set_master_table(page, true);

    // Copy segments:
    for (size_t i = 0; i < KernelProcess::MAX_SEGMENTS; i += 1) {
        
        if (source->st_ptr[i].get_kind() != SegTableEntry::Free) {

            pcb->clone_segment_from(source, i);
            
            }

        }

    // Update frame table:
    size_t ordinal = ks_page_ordinal(page);

    ks_ft_update(ordinal, FT_NONE, PageType::KsSegTable, pcb);

    // End:
    return proc;

    }

// Thread safety: Yes (mutex_uslst, mutex_usft, Wrapper)
PageAnte *KernelSystem::us_clone_page(size_t index, void *owner_of_clone) {

    RaiiLock rl1(mutex_uslst);
    RaiiLock rl2(mutex_usft);

    // Get new page while not swapping out the source
    PageAnte *page = us_acquire_page(PageType::UsUserPage, owner_of_clone, index);

    size_t   ordinal = us_page_ordinal(page);
    PageAnte *source = us_page_addr(index);

    ot_ptr[ordinal] = NULL_CLUSTER;

    std::memcpy(page, source, PAGE_SIZE);

    PRINT( "System gave page " << ordinal << " (US) to user process; Cloned page " << index <<".\n");

    return page;

    }

// Thread safety: Yes (mutex_pcbvec, Wrapper)
Process *KernelSystem::create_process() {    

    // Make and connect PCB and Process objects:
    UniqLock ul(mutex_pcbvec);

    size_t index = pcb_vec.insert(nullptr);

    ul.unlock();

    PCB *pcb = new PCB(this, index);

    pcb_vec[index] = pcb;

    Process *proc = new Process(0);

    proc->pProcess = pcb;

    // Set up the page/segment table for created process:
    PageAnte *page = ks_acquire_page(PageType::KsSegTable, pcb, true);

    pcb->set_master_table(page, true);

    // Update frame table
    size_t ordinal = ks_page_ordinal(page);

    ks_ft_update(ordinal, FT_NONE, PageType::KsSegTable, pcb);

    // End:
    return proc;
    
    }

// Thread safety: Yes (mutex_pcbvec)
void KernelSystem::process_deleted(ProcessId pid) {

    RaiiLock rl(mutex_pcbvec);

    pcb_vec.mark_empty(pid); // PEP

    }

// Thread safety: Not needed 'cause it doesn't do anything
Time KernelSystem::periodic_job() {

    return 0; 
    
    }

// Thread safety: Yes (mutex_databus)
Status KernelSystem::access(ProcessId pid, VirtualAddress address, AccessType type, bool ignore_access) {

    //PRINT("Process with PID " << pid << " called access (addr = " << address << ", type = " << type << "): ");

    RaiiLock rl(mutex_databus);

    PCB *pcb;

    if (pid < SSEG_START_IND) { // Normal access

        pcb = pcb_vec[pid];

        }
    else {
        
        pid -= SSEG_START_IND;

        pcb = sseg_vec[pid];

        }

    Status status;
    PageTableL1Entry *ptl1e;
    PageTableL2Entry *ptl2e;
    char *phys;

    pcb->master_table_lock();
    PageUnlocker punl(this, pcb->master_table);

    if ((ptl1e = pcb->access_ptl1(address, status, true)) == nullptr) {
        return status;
        }

    if ((ptl2e = pcb->access_ptl2(address, ptl1e, status, true)) == nullptr) {
        return status;
        }

    if (!ignore_access && !access_is_ok(type, ptl2e->get_access())) { return TRAP; }

    if (!ptl2e->get_shared()) { // Access to normal segment

        if ((phys = pcb->access_phys(address, ptl2e, status, true)) == nullptr) {
            return status;
            }

        if (type == WRITE) {
        
            ptl2e->set_dirty(true);

            mutex_usft.lock();

            us_ft_ptr[us_page_ordinal(phys)].set_dirty(true); // PEP

            mutex_usft.unlock();

            }

        return OK;

        }
    else { // Access to shared segment

        VirtualAddress sseg_addr = ((VirtualAddress)ptl2e->block_disk * PAGE_SIZE) + (address & 0x03FF); // PEP

        return access(SSEG_START_IND + ptl2e->sseg_ind, sseg_addr, type, true);
        
        }

    }

// Thread safety: Not needed (Debug method)
void KernelSystem::test() {
    
    diag();
    
    }

// Thread safety: Not needed (Debug method)
void KernelSystem::diag() {
    
    #pragma push_macro("PRINT")
    #pragma push_macro("PRINTLN")

    #define PRINT(text) std::cout << text
    #define PRINTLN(text) std::cout << text << "\n"

    PRINTLN("Kernel System diagnostics:");
    PRINTLN("");

    PRINTLN("Kernel Space:");
    PRINTLN("  Address: "  << (void*)krnlspc );
    PRINTLN("  Reserved: " << ks_reserved << " / " << krnlspc_size);
    PRINTLN("  In use: " << (krnlspc_size - ks_empty_count) << " / "  << krnlspc_size);
    PRINTLN("  Free: " << 100*(ks_empty_count)/krnlspc_size << "%");
    PRINTLN("");

    PRINTLN("User Space:");
    PRINTLN("  Address: "  << (void*)userspc );
    PRINTLN("  Reserved: " << us_reserved << " / " << userspc_size);
    PRINTLN("  In use: " << (userspc_size - us_empty_count) << " / "  << userspc_size);
    PRINTLN("  Free: " << 100*(us_empty_count)/userspc_size << "%");
    PRINTLN("");
    
    #pragma pop_macro("PRINTLN")
    #pragma pop_macro("PRINT") 

    }