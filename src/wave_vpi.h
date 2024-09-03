#pragma once

#include "vpi_user.h"
#include "fmt/core.h"
#include "libassert/assert.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <memory>
#include <queue>
#include <unordered_map>
#include <sys/types.h>
#include <utility>

using CursorTime_t = uint64_t;
using TaskId_t = uint64_t;

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


struct WaveCursor {
    CursorTime_t time;
    CursorTime_t maxTime;

    uint64_t index;
    uint64_t maxIndex;

    void updateTime(uint64_t time) {
        this->time = time;
        this->index = wellen_get_index_from_time(time);
    }

    void updateIndex(uint64_t index) {
        this->index = index;
        this->time = wellen_get_time_from_index(index);
    }
};

using vpiHandleRaw = PLI_UINT32;
using vpiCbFunc = PLI_INT32 (*)(struct t_cb_data *);

struct ValueCbInfo {
    std::shared_ptr<s_cb_data> cbData;
    vpiHandleRaw handle;
    std::string valueStr;
};

std::string _wellen_get_value_str(vpiHandle object);

void wave_vpi_init(const char *filename);
void wave_vpi_main();

