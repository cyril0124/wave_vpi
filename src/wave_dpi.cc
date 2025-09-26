#include "svdpi.h"
#include "wave_vpi.h"

extern "C" svScope svSetScope(const svScope scope) {
    VL_FATAL(false, "svSetScope is not supported");
    return nullptr;
}

extern "C" svScope svGetScopeFromName(const char *scopeName) {
    VL_FATAL(false, "svGetScopeFromName is not supported");
    return nullptr;
}
