#pragma once

#include "vpi_user.h"
#include "fmt/core.h"
#include "libassert/assert.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <csignal>
#include <iostream>
#include <memory>
#include <queue>
#include <vector>
#include <string>
#include <sys/types.h>
#include <utility>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <chrono>
#include "sys/stat.h"

#define LAST_MODIFIED_TIME_FILE "last_modified_time.wave_vpi_fsdb"
#define TIME_TABLE_FILE "time_table.wave_vpi_fsdb"

#ifdef VL_DEF_OPT_USE_BOOST_UNORDERED
#warning "[wave_vpi] VL_DEF_OPT_USE_BOOST_UNORDERED is defined!"

#include "boost_unordered.hpp"
#define UNORDERED_MAP boost::unordered_flat_map
#else
#include <unordered_map>
#define UNORDERED_MAP std::unordered_map
#endif

#ifdef USE_FSDB
#warning "[wave_vpi] USE_FSDB is defined!"

#include "ffrAPI.h"
#include "fsdbShr.h"
#include <set>

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

#endif

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

#define VL_INFO(...) \
    do { \
        fmt::print("[{}:{}:{}] [{}INFO{}] ", __FILE__, __FUNCTION__, __LINE__, ANSI_COLOR_MAGENTA, ANSI_COLOR_RESET); \
        fmt::print(__VA_ARGS__); \
    } while(0)

#define VL_WARN(...) \
    do { \
        fmt::print("[{}:{}:{}] [{}WARN{}] ", __FILE__, __FUNCTION__, __LINE__, ANSI_COLOR_YELLOW, ANSI_COLOR_RESET); \
        fmt::print(__VA_ARGS__); \
    } while(0)

#define VL_FATAL(cond, ...) \
    do { \
        if (!(cond)) { \
            fmt::println("\n"); \
            fmt::print("[{}:{}:{}] [{}FATAL{}] ", __FILE__, __FUNCTION__, __LINE__, ANSI_COLOR_RED, ANSI_COLOR_RESET); \
            fmt::println(__VA_ARGS__ __VA_OPT__(,) "A fatal error occurred without a message.\n"); \
            fflush(stdout); \
            fflush(stderr); \
            abort(); \
        } \
    } while(0)

using CursorTime_t = uint64_t;
using TaskId_t = uint64_t;

#ifndef USE_FSDB
extern "C" {
    void wellen_wave_init(const char *filename);
    void wellen_test(const char *filename);
    void wellen_test_1();

    void *wellen_vpi_handle_by_name(const char *name);
    void wellen_vpi_get_value(void *handle, uint64_t time, p_vpi_value value_p);
    void wellen_vpi_get_value_from_index(void *handle, uint64_t time_table_idx, p_vpi_value value_p);

    PLI_INT32 wellen_vpi_get(PLI_INT32 property, void *handle);
    PLI_BYTE8 *wellen_vpi_get_str(PLI_INT32 property, void *object);
    void * wellen_vpi_iterate(PLI_INT32 type, void *refHandle);
    
    uint64_t wellen_get_max_index();
    uint64_t wellen_get_time_from_index(uint64_t index);
    uint64_t wellen_get_index_from_time(uint64_t time);

    char *wellen_get_value_str(void *handle, uint64_t time_table_idx);

    void wellen_vpi_finalize();
}
#endif

struct WaveCursor {
    CursorTime_t time;
    CursorTime_t maxTime;

    uint64_t index;
    uint64_t maxIndex;

#ifndef USE_FSDB
    void updateTime(uint64_t time) {
        this->time = time;
        this->index = wellen_get_index_from_time(time);
    }

    void updateIndex(uint64_t index) {
        this->index = index;
        this->time = wellen_get_time_from_index(index);
    }
#endif
};

using vpiHandleRaw = PLI_UINT32;
using vpiCbFunc = PLI_INT32 (*)(struct t_cb_data *);

#ifdef USE_FSDB
#define MAX_SCOPE_DEPTH 100
#define TIME_TABLE_MAX_INDEX_VAR_CODE 10
#define Xtag64ToUInt64(xtag64) (uint64_t)(((uint64_t)xtag64.H << 32) + xtag64.L)

class FsdbWaveVpi {
  public:
    std::string waveFileName;
    ffrObject *fsdbObj;
    ffrFSDBInfo fsdbInfo;
    fsdbVarIdcode maxVarIdcode;
    fsdbVarIdcode sigArr[TIME_TABLE_MAX_INDEX_VAR_CODE];
    ffrTimeBasedVCTrvsHdl tbVcTrvsHdl;

    uint32_t sigNum = TIME_TABLE_MAX_INDEX_VAR_CODE;
    std::vector<uint64_t> xtagU64Vec;
    std::vector<fsdbXTag> xtagVec;

    UNORDERED_MAP<std::string, fsdbVarIdcode> varIdCodeCache; // TODO: store into json file at the end of simulation and read back at the start of simulation

    FsdbWaveVpi(ffrObject *fsdbObj, std::string_view waveFileName);
    ~FsdbWaveVpi() {};
    fsdbVarIdcode getVarIdCodeByName(char *name);
    uint32_t findNearestTimeIndex(uint64_t time);
};
#endif

struct ValueCbInfo {
    std::shared_ptr<s_cb_data> cbData;
#ifdef USE_FSDB
    vpiHandle handle;
    size_t bitSize;
    uint32_t bitValue;
#else
    vpiHandleRaw handle;
#endif
    std::string valueStr;
};

#ifdef USE_FSDB
std::string fsdbGetBinStr(vpiHandle object);
uint32_t fsdbGetSingleBitValue(vpiHandle object);
#else
std::string _wellen_get_value_str(vpiHandle object);
#endif

void wave_vpi_init(const char *filename);
void wave_vpi_main();

