
#include "KernelProcess.hpp"
#include "KernelSystem.hpp"

#include <new>
#include <iostream>
#include <cstring>
#include <mutex>

KernelProcess::KernelProcess(KernelSystem *owner_, ProcessId pid_)
    : owner(owner_)
    , pid(pid_) {

    seg_count = 0;

    sscb = nullptr;

    }

KernelProcess::~KernelProcess() {

    size_t mt = master_table_lock();

    for (size_t i = 0; i < MAX_SEGMENTS; i += 1) {
        
        if (st_ptr[i].get_kind() == SegTableEntry::Occupied) {

            delete_segment_ind(i, true);

            }
        
        }

    owner->ks_relinquish_page(mt);

    owner->ks_unlock_page(mt);

    owner->process_deleted(pid);

    }

ProcessId KernelProcess::get_pid() const {
    
    return pid;
    
    }

void KernelProcess::page_fault(VirtualAddress addr) {

    PageTableL1Entry *ptl1e;
    PageTableL2Entry *ptl2e;

    Status status;

    //PRINT("Process with PID " << pid << " called page_fault (addr = " << addr << ").\n");

    master_table_lock();

    PageUnlocker punl(owner, master_table);

    ptl1e = access_ptl1(addr, status, true);

    if (ptl1e->status == PageTableL1Entry::Unused) {
        HALT("KernelProcess::page_fault - Master table error.");
        }

    do {
        ptl2e = access_ptl2(addr, ptl1e, status, true);
        if (status == TRAP) { HALT("KernelProcess::page_fault - Accessed unused page table."); }
        if (status == PAGE_FAULT) { 
            swap_page_table(MODE_IN, addr);
            }
        }
        while (status != OK);  

    if (!ptl2e->get_shared()) { // Normal page

        if (ptl2e->get_tbc()) { // if page is to-be-created
        
            PageAnte *temp = owner->us_request_page(PageType::UsUserPage, ptl2e);

            ptl2e->block_disk = (Uint16)owner->us_page_ordinal(temp);

            ptl2e->set_valid(true);
            ptl2e->set_dirty(false);
            ptl2e->set_tbc(false);

            }
        else if (!ptl2e->get_valid()) { // if page is not in memory
        
            PageAnte *temp = owner->us_request_page(PageType::UsUserPage, ptl2e, ptl2e->block_disk);

            ptl2e->block_disk = (Uint16)owner->us_page_ordinal(temp);

            ptl2e->set_valid(true);
            ptl2e->set_dirty(false);

            }

        }
    else{ // Shared page
        
        VirtualAddress sseg_addr = ((VirtualAddress)ptl2e->block_disk * PAGE_SIZE) + (addr & 0x03FF); // PEP

        owner->shared_segment_pf(ptl2e->sseg_ind, sseg_addr);
        
        }

    }

void KernelProcess::swap_master_table(bool mode, bool lock) {

    if (mode == MODE_IN) {

        master_table = reinterpret_cast<char*>(
            owner->ks_request_page(PageType::KsSegTable, this, mt_disk, lock));

        master_table_valid = true;

        }
    else {
        
        master_table_evict_children(false);

        owner->ks_evict_page(owner->ks_page_ordinal(master_table), true); // PEP

        // Above method call clears valid flag and sets mt_disk

        }

    }

void KernelProcess::swap_page_table(bool mode, VirtualAddress addr) {

    Status status;

    if (mode == MODE_IN) {
        
        PageTableL1Entry *ptl1e = access_ptl1(addr, status, true);

        if (status != OK)
            HALT("KernelProcess::swap_page_table - Could not fetch descriptor.");

        PageAnte *page = owner->ks_request_page(PageType::KsPageTable, ptl1e, ptl1e->block_disk);

        ptl1e->block_disk = (Uint16)owner->ks_page_ordinal(page);
        ptl1e->status = PageTableL1Entry::Present;

        }
    else {
        
        HALT("KernelProcess::swap_page_table - Cannot swap out page tables with this method.");

        /*PageTableL1Entry *ptl1e = access_ptl1(addr, status, true);

        if (status != OK)
            HALT("KernelProcess::swap_page_table - Could not fetch descriptor.");

        owner->ks_evict_page(ptl1e->block_disk, true);*/
        
        }

    }

void KernelProcess::init_page_table(PageAnte *page) {

    /*auto *ptl2_ptr = reinterpret_cast<PageTableL2Entry*>(page);

    for (size_t i = 0; i < 256; i += 1) {
        
        new (ptl2_ptr + i) PageTableL2Entry();
        
        }*/

    std::memset(page, 0, PAGE_SIZE);

    }

void KernelProcess::page_table_evict_children(KernelSystem *system, PageAnte *page, bool destroy) {

    PRINTLN("Page table evicting children...");

    PageTableL2Entry *ptl2_ptr = reinterpret_cast<PageTableL2Entry*>(page);

    if (!destroy) {
        
        for (size_t i = 0; i < PAGE_TABLE_SIZE_L2; i += 1)  {
            
            if (ptl2_ptr[i].get_shared()) continue;

            if (ptl2_ptr[i].get_valid()) {

                system->us_evict_page(ptl2_ptr[i].block_disk, ptl2_ptr[i].get_dirty());

                // Above method call clears valid and dirty bits and sets block_disk

                }

            }

        }
    else {

        HALT("KernelProcess::page_table_evict_children - Method deprecated (destroy = 1).");

        /*for (size_t i = 0; i < PAGE_TABLE_SIZE_L2; i += 1) {
        
            if (ptl2_ptr[i].get_valid()) {
            
                system->us_relinquish_page(ptl2_ptr[i].block_disk);
            
                }
            else if (!ptl2_ptr[i].get_tbc()) { // PEP
            
                system->relinquish_cluster(ptl2_ptr[i].block_disk);

                }

            ptl2_ptr[i].reset();
        
            }*/

        }

    }

void KernelProcess::master_table_evict_children(bool destroy) {

    PRINTLN("Master table evicting children...");

    if (!destroy) {

        for (size_t i = 0; i < MAX_PAGE_TABLES_L1; i += 1) {

            if (ptl1_ptr[i].status == PageTableL1Entry::Present) {

                page_table_evict_children(owner, owner->ks_page_addr(ptl1_ptr[i].block_disk), destroy);

                //swap_page_table(MODE_OUT, 0);
                owner->ks_evict_page(ptl1_ptr[i].block_disk, true);

                }

            ptl1_ptr[i].status = PageTableL1Entry::PagedOut;

            }

        }
    else {

        HALT("KernelProcess::master_table_evict_children - Method deprecated (destroy = 1).");

        /*for (size_t i = 0; i < MAX_PAGE_TABLES_L1; i += 1) {

            if (ptl1_ptr[i].status == PageTableL1Entry::Present) {

                page_table_evict_children(owner, owner->ks_page_addr(ptl1_ptr[i].block_disk), destroy);

                }
            else if (ptl1_ptr[i].status = PageTableL1Entry::PagedOut) {
                
                // If not present, fetch then destory children - PEP

                PageAnte *page = owner->ks_request_page(PageType::KsPageTable, ptl1_ptr + i, ptl1_ptr[i].block_disk);

                ptl1_ptr[i].block_disk = (Uint16)owner->ks_page_ordinal(page);
                ptl1_ptr[i].status = PageTableL1Entry::Present;

                page_table_evict_children(owner, page, destroy);
                
                }

            
            ptl1_ptr[i].status = PageTableL1Entry::Unused;

            }*/

        }

    }

size_t KernelProcess::master_table_lock() {

    while (true) {

        if (!master_table_valid) {
            swap_master_table(MODE_IN, true);
            return owner->ks_page_ordinal(master_table);
            }
        else {
         
            size_t ordinal = owner->ks_page_ordinal(master_table);

            owner->ks_lock_page(ordinal);

            if (master_table_valid) return ordinal;

            owner->ks_unlock_page(ordinal);               
        
            }

        }

    }

size_t KernelProcess::page_table_lock(VirtualAddress addr) {

    auto slot  = (addr >> 18) & 0x03F;

    auto *ptl1e = ptl1_ptr + slot;

    while (true) {

        if (ptl1e->status == PageTableL1Entry::PagedOut) {

            PageAnte *page = owner->ks_request_page(PageType::KsPageTable, ptl1e, ptl1e->block_disk, true);

            ptl1e->block_disk = (Uint16)owner->ks_page_ordinal(page);
            ptl1e->status = PageTableL1Entry::Present;

            return ptl1e->block_disk;

            }
        else {

            size_t ordinal = ptl1e->block_disk;

            owner->ks_lock_page(ordinal);

            if (ptl1e->status == PageTableL1Entry::Present) return ordinal;

            owner->ks_unlock_page(ordinal); 
        
            }

        }

    }

Status KernelProcess::create_segment(VirtualAddress start_addr, PageNum size, AccessType acc_type) {

    size_t ordinal = (start_addr >> 10) & 0x03FFF;

    if (size > (1 << 14) || size == 0) return TRAP;

    if (seg_count == MAX_SEGMENTS) return TRAP;

    if ((start_addr & 0x03FF) != 0) return TRAP;

    master_table_lock();
    PageUnlocker punl0(owner, master_table);

    for (size_t i = 0; i < MAX_SEGMENTS; i += 1) {
        
        SegTableEntry ste = st_ptr[i];

        if (ste.get_kind() == SegTableEntry::Free) continue;

        if ((ste.start_page + ste.get_length() - 1 < ordinal) ||
            (ste.start_page > start_addr + size - 1))
            continue;

        return TRAP;

        }

    // Find suitable entry of segment table:
    size_t entry;

    for (size_t i = 0; i < MAX_SEGMENTS; i += 1) {
        
        if (st_ptr[i].get_kind() == SegTableEntry::Free) {
            
            entry = i;
            break;

            }
        
        }

    st_ptr[entry].start_page = (Uint16)ordinal;
    st_ptr[entry].set_kind(SegTableEntry::Occupied);
    st_ptr[entry].set_length(size);

    seg_count += 1;

    // Update page tables:
    for (size_t i = 0; i < size; i += 1) {
        
        PageTableL1Entry *ptl1e;
        PageTableL2Entry *ptl2e;
        Status status;
        PageUnlocker punl1(owner, KernelSystem::NULL_CLUSTER);

        ptl1e = access_ptl1(start_addr + i * PAGE_SIZE, status, true);

        // New way:
        if (ptl1e->status == PageTableL1Entry::Unused) {
            PageAnte *pa = owner->ks_request_page(PageType::KsPageTable, ptl1e, KernelSystem::NULL_CLUSTER, true);
            ptl1e->block_disk = (Uint16)owner->ks_page_ordinal(pa);
            ptl1e->status = PageTableL1Entry::Present;
            init_page_table(pa);
            punl1.reset(pa, true);
            }
        else {
            size_t ptind = page_table_lock(start_addr + i * PAGE_SIZE);
            punl1.reset(ptind, true);
            }

        ptl2e = access_ptl2(start_addr + i * PAGE_SIZE, ptl1e, status, true);

        // Old way:
        /*if (ptl1e->status == PageTableL1Entry::Unused) {
            PageAnte *pa = owner->ks_request_page(PageType::KsPageTable, ptl1e, KernelSystem::NULL_CLUSTER, true);
            ptl1e->block_disk = (Uint16)owner->ks_page_ordinal(pa);
            ptl1e->status = PageTableL1Entry::Present;
            init_page_table(pa);
            }

        do {
            ptl2e = access_ptl2(start_addr + i * PAGE_SIZE, ptl1e, status, true);
            if (status == TRAP) { HALT("KernelProcess::create_segment - Acessed unused page table."); }
            if (status == PAGE_FAULT) { swap_page_table(MODE_IN, start_addr + i * PAGE_SIZE); }
            }
            while (status != OK);*/

        /*
        ptl2e->set_valid(false);
        ptl2e->set_dirty(true);
        ptl2e->set_inseg(true);
        ptl2e->set_tbc(true);
        ptl2e->set_shared(false);
        ptl2e->set_access(acc_type);
        // Manually inlined below
        */

        ptl2e->flags = static_cast<Uint8>(acc_type)
                     | (0 << PageTableL2Entry::Valid)
                     | (1 << PageTableL2Entry::Dirty)
                     | (1 << PageTableL2Entry::InSeg)
                     | (1 << PageTableL2Entry::TBC)
                     | (0 << PageTableL2Entry::Shared);

        //PRINTLN("Set ptl2e for address " << start_addr + i * PAGE_SIZE);

        }

    // All went as expected:
    return OK;
    
    }

Status KernelProcess::load_segment(VirtualAddress start_addr, PageNum size, AccessType acc_type, void *content) {

    size_t ordinal = (start_addr >> 10) & 0x03FF;

    if (size > (1 << 14) || size == 0) return TRAP;

    if (seg_count == MAX_SEGMENTS) return TRAP;

    if ((start_addr & 0x03FF) != 0) return TRAP;

    master_table_lock();
    PageUnlocker punl0(owner, master_table);

    for (size_t i = 0; i < MAX_SEGMENTS; i += 1) {

        SegTableEntry ste = st_ptr[i];

        /*if (i < 10) {
        std::cout << "Checking segment " << i << " (isFree = " << ste.get_kind();
        std::cout << ", Start = " << ste.start_page << ", End = " << ste.start_page + ste.get_length() - 1;
        std::cout << ").\n";
        }*/

        if (ste.get_kind() == SegTableEntry::Free) continue;

        if ((ste.start_page + ste.get_length() - 1 < ordinal) ||
            (ste.start_page > start_addr + size - 1))
            continue;

        return TRAP;

        }

    // Find suitable entry of segment table:
    size_t entry;

    for (size_t i = 0; i < MAX_SEGMENTS; i += 1) {

        if (st_ptr[i].get_kind() == SegTableEntry::Free) {

            entry = i;
            break;

            }

        }

    st_ptr[entry].start_page = (Uint16)ordinal;
    st_ptr[entry].set_kind(SegTableEntry::Occupied);
    st_ptr[entry].set_length(size);

    seg_count += 1;

    // Update page tables:
    for (size_t i = 0; i < size; i += 1) {

        PageTableL1Entry *ptl1e;
        PageTableL2Entry *ptl2e;
        Status status;
        PageUnlocker punl1(owner, KernelSystem::NULL_CLUSTER);

        ptl1e = access_ptl1(start_addr + i * PAGE_SIZE, status, true);

        // New way:
        if (ptl1e->status == PageTableL1Entry::Unused) {
            PageAnte *pa = owner->ks_request_page(PageType::KsPageTable, ptl1e, KernelSystem::NULL_CLUSTER, true);
            ptl1e->block_disk = (Uint16)owner->ks_page_ordinal(pa);
            ptl1e->status = PageTableL1Entry::Present;
            init_page_table(pa);
            punl1.reset(pa, true);
            }
        else {
            size_t ptind = page_table_lock(start_addr + i * PAGE_SIZE);
            punl1.reset(ptind, true);
            }

        ptl2e = access_ptl2(start_addr + i * PAGE_SIZE, ptl1e, status, true);

        // Old way:
        /*if (ptl1e->status == PageTableL1Entry::Unused) {
        PageAnte *pa = owner->ks_request_page(PageType::KsPageTable, ptl1e, KernelSystem::NULL_CLUSTER, true);
        ptl1e->block_disk = (Uint16)owner->ks_page_ordinal(pa);
        ptl1e->status = PageTableL1Entry::Present;
        init_page_table(pa);
        }

        do {
        ptl2e = access_ptl2(start_addr + i * PAGE_SIZE, ptl1e, status, true);
        if (status == TRAP) { HALT("KernelProcess::create_segment - Acessed unused page table."); }
        if (status == PAGE_FAULT) { swap_page_table(MODE_IN, start_addr + i * PAGE_SIZE); }
        }
        while (status != OK);*/

        /*
        ptl2e->set_valid(false);
        ptl2e->set_dirty(true);
        ptl2e->set_inseg(true);
        ptl2e->set_tbc(true);
        ptl2e->set_shared(false);
        ptl2e->set_access(acc_type);
        // Manually inlined below
        */

        PageAnte *upg = owner->us_load_page(PageType::UsUserPage, ptl2e, static_cast<char*>(content) + i * PAGE_SIZE);

        ptl2e->block_disk = (Uint16)owner->us_page_ordinal(upg);

        ptl2e->flags = static_cast<Uint8>(acc_type)
            | (1 << PageTableL2Entry::Valid)
            | (1 << PageTableL2Entry::Dirty)
            | (1 << PageTableL2Entry::InSeg)
            | (0 << PageTableL2Entry::TBC)
            | (0 << PageTableL2Entry::Shared);

        //PRINTLN("Set ptl2e for address " << start_addr + i * PAGE_SIZE);

        }

    // All went as expected:
    return OK;

    }

Status KernelProcess::delete_segment(VirtualAddress start_addr) {

    size_t ordinal = (start_addr >> 10) & 0x03FF;

    if (seg_count == 0) return TRAP;

    if ((start_addr & 0x03FF) != 0) return TRAP;

    master_table_lock();
    PageUnlocker punl(owner, master_table);

    // Find entry & size:
    size_t entry = MAX_SEGMENTS + 1;

    for (size_t i = 0; i < MAX_SEGMENTS; i += 1) {
        
        if (st_ptr[i].start_page == ordinal) {

            entry = i;
            break;

            }

        }

    if (entry == MAX_SEGMENTS + 1) return TRAP;

    // Work:
    delete_segment_ind(entry, true);    

    // Finalize:
    return OK;

    }

// Unsafe
Status KernelProcess::delete_segment_ind(size_t entry, bool do_release_shared) {

    size_t size = st_ptr[entry].get_length();
    VirtualAddress start_addr = (VirtualAddress)(st_ptr[entry].start_page) * PAGE_SIZE;
    bool released_shared = false;

    // Work:
    for (size_t i = 0; i < size; i += 1) {

        PageTableL1Entry *ptl1e;
        PageTableL2Entry *ptl2e;
        Status status;

        // Assume master table is present and locked

        ptl1e = access_ptl1(start_addr + i * PAGE_SIZE, status, true);

        if (ptl1e->status == PageTableL1Entry::Unused) {
            HALT("KernelProcess::delete_segment - How did this even happen?");
            }

        size_t pt = page_table_lock(start_addr);
        PageUnlocker punl(owner, pt);

        ptl2e = access_ptl2(start_addr + i * PAGE_SIZE, ptl1e, status, true);

        if (!ptl2e->get_inseg()) {
            HALT("KernelProcess::delete_segment - How did this even happen?");
            }

        if (!ptl2e->get_shared()) { // Normal page

            if (ptl2e->get_valid()) {

                owner->us_relinquish_page(ptl2e->block_disk);

                }
            else {

                if (!ptl2e->get_tbc()) {

                    owner->relinquish_cluster(ptl2e->block_disk);

                    }

                }

            }
        else { // Shared page
            
            if (!released_shared && do_release_shared) {
                
                owner->disconnect_shared_segment(this, ptl2e->sseg_ind);

                released_shared = true;

                }

            }

        /*
        ptl2e->flags = static_cast<Uint8>(0)
        | (0 << PageTableL2Entry::Valid)
        | (0 << PageTableL2Entry::Dirty)
        | (0 << PageTableL2Entry::InSeg)
        | (0 << PageTableL2Entry::TBC)
        | (0 << PageTableL2Entry::Shared);
        */

        ptl2e->flags = 0;

        }

    // Finalize:
    st_ptr[entry].set_kind(SegTableEntry::Free);
    seg_count -= 1;

    return OK;

    }

// Utility:
void KernelProcess::set_master_table(PageAnte *page, bool newly_created) {
    
    PRINTLN("Process setting master table.");

    master_table = reinterpret_cast<char*>(page);

    ptl1_ptr = reinterpret_cast<PageTableL1Entry*>(master_table);

    st_ptr = reinterpret_cast<SegTableEntry*>(
        master_table + MAX_PAGE_TABLES_L1 * sizeof(PageTableL1Entry));

    if (newly_created) {
        
        PRINTLN("It's newly created.");

        seg_count = 0;

        for (size_t i = 0; i < MAX_PAGE_TABLES_L1; i += 1) {
            
            ptl1_ptr[i].status = PageTableL1Entry::Unused;
            
            }

        for (size_t i = 0; i < MAX_SEGMENTS; i += 1) {
            
            st_ptr[i].set_kind(SegTableEntry::Free);

            }
        
        }   

    master_table_valid = true;

    }

PageTableL1Entry *KernelProcess::access_ptl1(VirtualAddress addr, Status &status, bool visit) {

    PRINTLN("Accessing page table L1 on page " << owner->ks_page_ordinal(master_table) 
            << " (slot = " << ((addr >> 18) & 0x03F) << ") with address " << addr << ".");

    if (!master_table_valid) { status = PAGE_FAULT; return nullptr; }

    if (visit) owner->ks_visited_page(master_table);

    status = OK;

    return ptl1_ptr + ((addr >> 18) & 0x03F);

    }

PageTableL2Entry *KernelProcess::access_ptl2(VirtualAddress addr, PageTableL1Entry *ptl1e, Status &status, bool visit) {

    PRINTLN("Accessing page table L2 on page " << ptl1e->block_disk 
            << " (slot = " << ((addr >> 10) & 0x0FF) << ") with address " << addr << ".");

    if (ptl1e->status == PageTableL1Entry::Unused)   { 
        
        status = TRAP; 
        
        return nullptr;
        
        }

    if (ptl1e->status == PageTableL1Entry::PagedOut) { status = PAGE_FAULT; return nullptr; }

    PageTableL2Entry *ptl2_ptr = reinterpret_cast<PageTableL2Entry*>(owner->ks_page_addr(ptl1e->block_disk));

    if (visit) owner->ks_visited_page(ptl2_ptr);

    status = OK;

    return ptl2_ptr + ((addr >> 10) & 0x0FF);
    
    }

char *KernelProcess::access_phys(VirtualAddress addr, PageTableL2Entry *ptl2e, Status &status, bool visit) {

    PRINTLN("Accessing physical memory " << ptl2e->block_disk 
            << " (offset = " << (addr & 0x03FF) << ") with address " << addr << ".");

    if (!ptl2e->get_inseg()) { status = TRAP; return nullptr; }
    if (!ptl2e->get_valid()) { status = PAGE_FAULT; return nullptr; }

    char *pp = reinterpret_cast<char*>(owner->us_page_addr(ptl2e->block_disk));

    if (visit) owner->us_visited_page(pp);

    status = OK;

    return pp + (addr & 0x03FF);

    }

void *KernelProcess::get_pa(VirtualAddress addr) {

    Status status;
    
    auto *ptl1e = access_ptl1(addr, status);
    auto *ptl2e = access_ptl2(addr, ptl1e, status);

    if (!ptl2e->get_shared()) { // Normal page
        
        char *pa = access_phys(addr, ptl2e, status);

        return pa;

        }
    else { // Shared page
        
        VirtualAddress sseg_addr = ((VirtualAddress)ptl2e->block_disk * PAGE_SIZE) + (addr & 0x03FF); // PEP

        return owner->shared_segment_pa(ptl2e->sseg_ind, sseg_addr);

        }

    /*
    // Inlined manually ... PEP

    auto * ptl1e = ptl1_ptr + ((addr >> 18) & 0x03F);
    auto * ptl2_ptr = reinterpret_cast<PageTableL2Entry*>(owner->ks_page_addr(ptl1e->block_disk));
    auto * ptl2e = ptl2_ptr + ((addr >> 10) & 0x0FF);
    char * pp = reinterpret_cast<char*>(owner->us_page_addr(ptl2e->block_disk));
    char * pa = pp + (addr & 0x03FF);

    return (void*)pa;
    */

    }

// Bonus:
Status KernelProcess::create_shared_segment(VirtualAddress start_addr, PageNum size, const char * name, AccessType acc_type) {

    size_t ordinal = (start_addr >> 10) & 0x03FFF;

    if (size > (1 << 14) || size == 0) return TRAP;

    if (seg_count == MAX_SEGMENTS) return TRAP;

    if ((start_addr & 0x03FF) != 0) return TRAP;

    master_table_lock();
    PageUnlocker punl0(owner, master_table);

    for (size_t i = 0; i < MAX_SEGMENTS; i += 1) {

        SegTableEntry ste = st_ptr[i];

        if (ste.get_kind() == SegTableEntry::Free) continue;

        if ((ste.start_page + ste.get_length() - 1 < ordinal) ||
            (ste.start_page > start_addr + size - 1))
            continue;

        return TRAP;

        }

    // Find shared segment:
    size_t sseg_ind;

    if (!owner->shared_segment_find(name, &sseg_ind)) {
        
        if (owner->create_shared_segment(size, name, acc_type) != OK) {
            return TRAP;     
            }

        owner->shared_segment_find(name, &sseg_ind);

        }

    // Find suitable entry of segment table:
    size_t entry;

    for (size_t i = 0; i < MAX_SEGMENTS; i += 1) {

        if (st_ptr[i].get_kind() == SegTableEntry::Free) {

            entry = i;
            break;

            }

        }

    st_ptr[entry].start_page = (Uint16)ordinal;
    st_ptr[entry].set_kind(SegTableEntry::OccShared);
    st_ptr[entry].set_length(size);

    seg_count += 1;

    // Connect to shared segment:
    owner->connect_shared_segment(this, sseg_ind, entry);

    // Update page tables:
    for (size_t i = 0; i < size; i += 1) {

        PageTableL1Entry *ptl1e;
        PageTableL2Entry *ptl2e;
        Status status;
        PageUnlocker punl1(owner, KernelSystem::NULL_CLUSTER);

        ptl1e = access_ptl1(start_addr + i * PAGE_SIZE, status, true);

        // New way:
        if (ptl1e->status == PageTableL1Entry::Unused) {
            PageAnte *pa = owner->ks_request_page(PageType::KsPageTable, ptl1e, KernelSystem::NULL_CLUSTER, true);
            ptl1e->block_disk = (Uint16)owner->ks_page_ordinal(pa);
            ptl1e->status = PageTableL1Entry::Present;
            init_page_table(pa);
            punl1.reset(pa, true);
            }
        else {
            size_t ptind = page_table_lock(start_addr + i * PAGE_SIZE);
            punl1.reset(ptind, true);
            }

        ptl2e = access_ptl2(start_addr + i * PAGE_SIZE, ptl1e, status, true);

        // Old way:
        /*if (ptl1e->status == PageTableL1Entry::Unused) {
        PageAnte *pa = owner->ks_request_page(PageType::KsPageTable, ptl1e, KernelSystem::NULL_CLUSTER, true);
        ptl1e->block_disk = (Uint16)owner->ks_page_ordinal(pa);
        ptl1e->status = PageTableL1Entry::Present;
        init_page_table(pa);
        }

        do {
        ptl2e = access_ptl2(start_addr + i * PAGE_SIZE, ptl1e, status, true);
        if (status == TRAP) { HALT("KernelProcess::create_segment - Acessed unused page table."); }
        if (status == PAGE_FAULT) { swap_page_table(MODE_IN, start_addr + i * PAGE_SIZE); }
        }
        while (status != OK);*/

        /*
        ptl2e->set_valid(false);
        ptl2e->set_dirty(true);
        ptl2e->set_inseg(true);
        ptl2e->set_tbc(true);
        ptl2e->set_shared(false);
        ptl2e->set_access(acc_type);
        // Manually inlined below
        */

        ptl2e->flags = static_cast<Uint8>(acc_type)
                     | (1 << PageTableL2Entry::Valid)
                     | (0 << PageTableL2Entry::Dirty)
                     | (1 << PageTableL2Entry::InSeg)
                     | (0 << PageTableL2Entry::TBC)
                     | (1 << PageTableL2Entry::Shared);

        ptl2e->sseg_ind = (Uint8)sseg_ind;

        ptl2e->block_disk = (Uint16)i;

        //PRINTLN("Set ptl2e for address " << start_addr + i * PAGE_SIZE);

        }

    // All went as expected:
    return OK;

    }

Status KernelProcess::disconnect_shared_segment(const char *name) {

    size_t sseg_ind;

    if (!owner->shared_segment_find(name, &sseg_ind)) {

        return TRAP;

        }
    
    size_t local_index = owner->disconnect_shared_segment(this, sseg_ind);

    delete_segment_ind(local_index, false);

    return OK;

    }

Status KernelProcess::delete_shared_segment(const char *name) {

    size_t sseg_ind;

    if (!owner->shared_segment_find(name, &sseg_ind)) {

        return TRAP;

        }

    return owner->delete_shared_segment(name);

    }

void KernelProcess::clone_segment_from(KernelProcess *source, size_t index) {

    // Assume both master tables are present and locked

    size_t size  = source->st_ptr[index].get_length();
    size_t start = source->st_ptr[index].start_page;
    int    kind  = source->st_ptr[index].get_kind();

    VirtualAddress start_addr = start * PAGE_SIZE;

    // Find suitable entry of segment table:
    size_t entry;

    for (size_t i = 0; i < MAX_SEGMENTS; i += 1) {

        if (st_ptr[i].get_kind() == SegTableEntry::Free) {

            entry = i;
            break;

            }

        }

    st_ptr[entry].start_page = (Uint16)start;
    st_ptr[entry].set_kind(static_cast<SegTableEntry::KindEnum>(kind));
    st_ptr[entry].set_length(size);

    seg_count += 1;

    // Update page tables:
    bool connect = true;

    for (size_t i = 0; i < size; i += 1) {

        Status status;

        // Get destination descriptors:
        PageTableL1Entry *ptl1e;
        PageTableL2Entry *ptl2e;       
        PageUnlocker punl1(owner, KernelSystem::NULL_CLUSTER);
      
        ptl1e = access_ptl1(start_addr + i * PAGE_SIZE, status, true);

        if (ptl1e->status == PageTableL1Entry::Unused) {
            PageAnte *pa = owner->ks_request_page(PageType::KsPageTable, ptl1e, KernelSystem::NULL_CLUSTER, true);
            ptl1e->block_disk = (Uint16)owner->ks_page_ordinal(pa);
            ptl1e->status = PageTableL1Entry::Present;
            init_page_table(pa);
            punl1.reset(pa, true);
            }
        else {
            size_t ptind = page_table_lock(start_addr + i * PAGE_SIZE);
            punl1.reset(ptind, true);
            }

        ptl2e = access_ptl2(start_addr + i * PAGE_SIZE, ptl1e, status, true);

        // Get source descriptors:
        PageTableL1Entry *ptl1e_src;
        PageTableL2Entry *ptl2e_src;
        PageUnlocker punl1_src(owner, KernelSystem::NULL_CLUSTER);

        ptl1e_src = source->access_ptl1(start_addr + i * PAGE_SIZE, status, true);

        if (ptl1e_src->status == PageTableL1Entry::Unused) {
            HALT("KernelProcess::clone_segment_from - ?????.");
            }
        else {
            size_t ptind = source->page_table_lock(start_addr + i * PAGE_SIZE);
            punl1_src.reset(ptind, true);
            }

        ptl2e_src = source->access_ptl2(start_addr + i * PAGE_SIZE, ptl1e_src, status, true);

        // Copy over:
        std::memcpy(ptl2e, ptl2e_src, sizeof(SegTableEntry));

        if (kind == SegTableEntry::OccShared || ptl2e_src->get_tbc() == true) {
            
            if (connect && kind == SegTableEntry::OccShared) { // Copy shared segment descriptor

                owner->connect_shared_segment(this, ptl2e_src->sseg_ind, entry);

                connect = false;

                }

            }
        else if (!ptl2e_src->get_valid()) { // Load from disk
            
            PageAnte *upg = owner->us_request_page(PageType::UsUserPage, ptl2e, ptl2e_src->block_disk, true);

            ptl2e->block_disk = (Uint16)owner->us_page_ordinal(upg);

            ptl2e->set_dirty(true);
            ptl2e->set_valid(true);

            }
        else { // Copy from OM
            
            PageAnte *upg = owner->us_clone_page(ptl2e_src->block_disk, ptl2e);

            ptl2e->block_disk = (Uint16)owner->us_page_ordinal(upg);

            ptl2e->set_dirty(true);
            ptl2e->set_valid(true);

            }

        //PRINTLN("Set ptl2e for address " << start_addr + i * PAGE_SIZE);

        }

    }

