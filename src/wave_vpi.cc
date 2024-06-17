#include "wave_vpi.h"
#include "fmt/core.h"
#include "vpi_user.h"
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

WaveCursor cursor{0, 0, 0, 0};

std::unique_ptr<s_cb_data> startOfSimulationCb = NULL;
std::unique_ptr<s_cb_data> endOfSimulationCb = NULL;

std::queue<std::pair<uint64_t, std::shared_ptr<t_cb_data>>> timeCbQueue;

std::unordered_map<vpiHandleRaw, ValueCbInfo> valueCbMap;
std::vector<std::pair<vpiHandleRaw, ValueCbInfo>> willAppendValueCb;
std::vector<vpiHandleRaw> willRemoveValueCb;
vpiHandleRaw vpiHandleAllcator = 0;

extern "C" void vlog_startup_routines_bootstrap();

void wave_vpi_init(const char *filename) {
    wellen_wave_init(filename);

    cursor.maxIndex = wellen_get_max_index();
    cursor.maxTime = wellen_get_time_from_index(cursor.maxIndex);
}

void wave_vpi_main() {
#ifndef NO_VLOG_STARTUP
    vlog_startup_routines_bootstrap();
#endif

    if(startOfSimulationCb) {
        startOfSimulationCb->cb_rtn(startOfSimulationCb.get());
    }

    // 
    // evaluation loop
    // 
    ASSERT(cursor.maxIndex != 0);
    fmt::println("cursor.maxIndex => {} time => {} cursor.maxTime => {}", cursor.maxIndex, wellen_get_time_from_index(cursor.maxIndex), cursor.maxTime);
    
    if(!willAppendValueCb.empty()) {
        for(auto &cb : willAppendValueCb) {
            valueCbMap[cb.first] = cb.second;
            // fmt::println("append {}", cb.first);
        }
        willAppendValueCb.clear();
    }
    
    while(cursor.index < cursor.maxIndex) {
        // 
        // time callback
        // 
        if(!timeCbQueue.empty()) {
            bool again = cursor.index >= timeCbQueue.front().first;
            while(again) {
                auto cb = timeCbQueue.front().second;
                cb->cb_rtn(cb.get());
                timeCbQueue.pop();
                again = !timeCbQueue.empty() && cursor.index >= timeCbQueue.front().first;
            }
        }

        // 
        // value callback
        // 
        for(auto &cb : valueCbMap) {
            if (cb.second.cbData->cb_rtn != nullptr) {
                ASSERT(cb.second.cbData->obj != nullptr);
                ASSERT(cb.second.cbData->cb_rtn != nullptr);
                // fmt::println("hello 0");
                auto newValueStr = _wellen_get_value_str(&cb.second.handle);
                // fmt::println("hello 1 newValueStr => {}", newValueStr);
                if(newValueStr != cb.second.valueStr) {
                    // fmt::println("hello 2 => {}", cb.second.cbData->value->format);
                    // fmt::println("valueCbMap => vpiHandle: {} last:{} =/= new:{}", cb.first, cb.second.valueStr, newValueStr);
                    cb.second.cbData->cb_rtn(cb.second.cbData.get());
                    // fmt::println("hello 3");
                    cb.second.valueStr = newValueStr;
                }
            }
        }

        if(!willRemoveValueCb.empty()) {
            for(auto &handle : willRemoveValueCb) {
                valueCbMap.erase(handle);
            }
            willRemoveValueCb.clear();
        }

        if(!willAppendValueCb.empty()) {
            for(auto &cb : willAppendValueCb) {
                valueCbMap[cb.first] = cb.second;
            }
            willAppendValueCb.clear();
        }

        cursor.index++;
    }

    if(endOfSimulationCb) {
        endOfSimulationCb->cb_rtn(endOfSimulationCb.get());
    }
}


PLI_INT32 vpi_free_object(vpiHandle object) {
    if(object != nullptr) {
        ASSERT(false, "TODO:");
        if(valueCbMap.find(*object) != valueCbMap.end()) {
            valueCbMap.erase(*object);
        }
        delete object;
    }
    return 0;
}

PLI_INT32 vpi_release_handle(vpiHandle object) {
    vpi_free_object(object);
}

vpiHandle vpi_put_value(vpiHandle object, p_vpi_value value_p, p_vpi_time time_p, PLI_INT32 flags) {
    ASSERT(false, "Unsupported in wave_vpi, all signals are read-only!");
    return 0;
}

vpiHandle vpi_handle_by_name(PLI_BYTE8 *name, vpiHandle scope) {
    // TODO: scope
    ASSERT(scope == nullptr);
    return reinterpret_cast<vpiHandle>(wellen_vpi_handle_by_name(name));
}

void vpi_get_value(vpiHandle object, p_vpi_value value_p) {
    // wellen_vpi_get_value(reinterpret_cast<void *>(object), cursor.time, value_p);
    wellen_vpi_get_value_from_index(reinterpret_cast<void *>(object), cursor.index, value_p);
    // if(value_p->format == vpiVectorVal) {
    //     fmt::println("[vpi_get_value] vectorVal => {}", value_p->value.vector[1].aval);
    // }
}

PLI_BYTE8 *vpi_get_str(PLI_INT32 property, vpiHandle object) {
    return reinterpret_cast<PLI_BYTE8 *>(wellen_vpi_get_str(property, reinterpret_cast<void *>(object)));
};

PLI_INT32 vpi_get(PLI_INT32 property, vpiHandle object) {
    return wellen_vpi_get(property,reinterpret_cast<void *>(object));
}

vpiHandle vpi_iterate(PLI_INT32 type, vpiHandle refHandle) {
    // TODO: consider slang
    // return reinterpret_cast<vpiHandle>(wellen_vpi_iterate(type, reinterpret_cast<void *>(refHandle)));
    return nullptr;
}

vpiHandle vpi_scan(vpiHandle iterator) {
    // TODO: consider slang
    return nullptr;
}

vpiHandle vpi_handle_by_index(vpiHandle object, PLI_INT32 indx) {
    ASSERT(false, "TODO:");
    return nullptr;
}

PLI_INT32 vpi_control(PLI_INT32 operation, ...) {
    switch (operation) {
        case vpiStop:
        case vpiFinish:
            exit(0);
            break;
        default:
            ASSERT(false, "Unsupported operation", operation);
            break;
    }
    return 0;
}

std::string _wellen_get_value_str(vpiHandle object) {
    ASSERT(object != nullptr);
    return std::string(wellen_get_value_str(reinterpret_cast<void *>(object), cursor.index));
}

vpiHandle vpi_register_cb(p_cb_data cb_data_p) {
    switch (cb_data_p->reason) {
        case cbStartOfSimulation:
            ASSERT(startOfSimulationCb == nullptr);
            startOfSimulationCb = std::make_unique<s_cb_data>(*cb_data_p);
            break;
        case cbEndOfSimulation:
            ASSERT(endOfSimulationCb == nullptr);
            endOfSimulationCb = std::make_unique<s_cb_data>(*cb_data_p);
            break;
        case cbValueChange: {
            ASSERT(cb_data_p->obj != nullptr);
            ASSERT(cb_data_p->cb_rtn != nullptr);
            if(cb_data_p->time != nullptr) {
                ASSERT(cb_data_p->time->type == vpiSuppressTime);
            }
            auto t = *cb_data_p;
            willAppendValueCb.push_back(std::make_pair(vpiHandleAllcator, ValueCbInfo{
                .cbData = std::make_shared<t_cb_data>(*cb_data_p), 
                .handle = *cb_data_p->obj,
                .valueStr = _wellen_get_value_str(cb_data_p->obj), 
            }));
            break;
        }
        case cbAfterDelay: {
            ASSERT(cb_data_p->time->type == vpiSimTime);
            
            uint64_t time = (((uint64_t) cb_data_p->time->high << 32) | (cb_data_p->time->low));
            uint64_t targetTime = cursor.time + time;
            uint64_t targetIndex = wellen_get_index_from_time(targetTime);            
            ASSERT(targetTime <= cursor.maxTime);

            timeCbQueue.push(std::make_pair(targetIndex, std::make_shared<t_cb_data>(*cb_data_p)));
            break;
        }
        default:
            ASSERT(false, "TODO:", cb_data_p->reason);
            break;
    }

    if(cb_data_p->reason == cbValueChange) {
        vpiHandleRaw handle = vpiHandleAllcator;
        vpiHandleAllcator++;
        return new vpiHandleRaw(handle);
    } else {
        return nullptr;
    }
}

PLI_INT32 vpi_remove_cb(vpiHandle cb_obj) {
    ASSERT(cb_obj != nullptr);
    if(valueCbMap.find(*cb_obj) != valueCbMap.end()) {
        willRemoveValueCb.push_back(*cb_obj);
    }
    delete cb_obj;
    return 0;
}

// Unsupport:
//      vpi_put_value(handle, &v, NULL, vpiNoDelay);
// 
// Supported:
//      OK => vpi_get_value(handle, &v);
//      OK => vpi_get_str(vpiType, actual_handle);
//      OK => vpi_get(vpiSize, actual_handle);
//      OK => vpi_handle_by_name(name)
//      OK => vpi_release_handle()
//      OK => vpi_free_object()
//      vpi_register_cb()
//          -> cbStartOfSimulation OK
//          -> cbEndOfSimulation   OK
//          -> cbValueChange       OK
//          -> cbAfterDelay        OK
//      vpi_remove_cb()            OK
// 
// TODO:
//      vpi_iterate
//      vpi_scan
// 
