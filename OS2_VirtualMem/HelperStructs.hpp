#pragma once

#include "IntegralTypes.hpp"
#include "Macros.hpp"
#include "VmDecl.hpp"

#include "Part.h"

struct PageAnte {

    PageAnte *next_empty = nullptr;

    };

struct PageType {

    enum TypeEnum {

        UsUnused,
        UsUserPage,
        UsReserved,

        KsUnused,
        KsSegTable,
        KsPageTable,
        KsReserved

        };

    };

#pragma pack(push, 1)

struct FrameTableEntry {

    enum FlagEnum {
        
        Dirty  = 7,
        Shared = 6,
        Locked = 5

        };

    static const Uint16 REF_TOP = 0x8000;

    Uint16 ref_history;

    Uint8 flags;

    Uint8 type;

    void *owner;

    FrameTableEntry() { // TEMP

        ref_history = 0;

        flags = type = 0;

        owner = nullptr;

        }

    // Flags
    void set_dirty(bool val) {

        flags = BIT_VAL(flags, Dirty, val);

        }

    bool get_dirty() const {

        return BIT_GET(flags, Dirty);

        }

    void set_shared(bool val) {

        flags = BIT_VAL(flags, Shared, val);

        }

    bool get_shared() const {

        return BIT_GET(flags, Shared);

        }

    void set_locked(bool val) {

        flags = BIT_VAL(flags, Locked, val);

        }

    bool get_locked() const {

        return BIT_GET(flags, Locked);

        }

    };

struct PageTableL1Entry {

    enum StatusEnum {
        
        Unused,
        PagedOut,
        Present
        
        };

    Uint8 status;

    Uint8 unused;

    Uint16 block_disk;

    };

struct PageTableL2Entry {
    
    enum FlagEnum {
        
        Valid  = 7,
        Dirty  = 6,
        InSeg  = 5,
        TBC    = 4,
        Shared = 3,
        COW    = 2,
        Acc_Hi = 1,
        Acc_Lo = 0
        
        };

    Uint16 block_disk;
    
    Uint8 sseg_ind;

    Uint8 flags;   

    // Flags
    void set_valid(bool val) {
        
        flags = BIT_VAL(flags, Valid, val);

        }

    bool get_valid() const {
        
        return BIT_GET(flags, Valid);

        }

    void set_dirty(bool val) {
        
        flags = BIT_VAL(flags, Dirty, val);

        }

    bool get_dirty() const {
        
        return BIT_GET(flags, Dirty);

        }  

    void set_inseg(bool val) {

        flags = BIT_VAL(flags, InSeg, val);
        
        }

    bool get_inseg() const {
        
        return BIT_GET(flags, InSeg);

        }

    void set_tbc(bool val) {

        flags = BIT_VAL(flags, TBC, val);

        }

    bool get_tbc() const {

        return BIT_GET(flags, TBC);

        }

    void set_shared(bool val) {

        flags = BIT_VAL(flags, Shared, val);

        }

    bool get_shared() const {

        return BIT_GET(flags, Shared);

        }

    // Access
    void set_access(AccessType type) {

        flags &= ~0x03; // Clear two lowest bits

        flags |= static_cast<Uint8>(type);

        }

    AccessType get_access() const {

        return static_cast<AccessType>(flags & 0x03);

        }

    // Other
    void reset() {
        
        block_disk = flags = 0;

        }

    };

struct SegTableEntry {

    enum KindEnum { // Max 4 elements
        
        Occupied,
        Free,
        OccShared
        
        };

    Uint16 len_kind; // length [14], kind [2]

    Uint16 start_page;

    void set_kind(KindEnum kind) {
        
        len_kind &= ~0x03;
        len_kind |= kind;

        }

    KindEnum get_kind() const {
        
        return static_cast<KindEnum>(len_kind & 0x03);

        }

    void set_length(size_t length) {
        
        KindEnum temp = get_kind();

        len_kind = (Uint16)length;
        len_kind <<= 2;
        len_kind |= temp;

        }

    size_t get_length() const {
        
        return (len_kind >> 2);

        }

    };

#pragma pack(pop)

struct Victim {
    
    size_t ordinal;

    ClusterNo cluster;

    bool dirty;

    Victim() = default;

    Victim(size_t ordinal_, bool dirty_, ClusterNo cluster_) {
        
        ordinal = ordinal_;
        dirty = dirty_;
        cluster = cluster_;
        
        }
    
    };
