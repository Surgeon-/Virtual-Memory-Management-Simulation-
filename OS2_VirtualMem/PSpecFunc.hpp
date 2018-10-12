#pragma once

#include <string>
#include "VmDecl.hpp"

inline std::string STATUS_STR(Status status) {
    
    switch (status) {
        
        case OK: return "OK";

        case PAGE_FAULT: return "PAGE_FAULT";

        case TRAP: return "TRAP";
        
        }

    return "";

    }

inline void PAUSE() {
    
    }