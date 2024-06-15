#include <cassert>
#include <cstddef>
#include <cstdio>
#include <iostream>
#include "vpi_user.h"

#define PRJ_DIR "/nfs/home/zhengchuyu/workspace/wellen"
// #define VCD_FILE "/nfs/home/zhengchuyu/workspace/wellen/test.vcd"
#define VCD_FILE "/nfs/home/zhengchuyu/workspace/wellen/wellen/inputs/vcs/Apb_slave_uvm_new.vcd"

extern "C" {
    void wellen_wave_init(const char *filename);
    void wellen_test(const char *filename);
    void wellen_test_1();

    void *wellen_vpi_handle_by_name(const char *name);
    void wellen_vpi_get_value(void *handle, uint64_t time, p_vpi_value value_p);
    PLI_INT32 wellen_vpi_get(PLI_INT32 property, void *handle);
    PLI_BYTE8 *wellen_vpi_get_str(PLI_INT32 property, void *object);
    void * wellen_vpi_iterate(PLI_INT32 type, void *refHandle);
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
//          -> cbStartOfSimulation
//          -> cbEndOfSimulation
//          -> cbValueChange
//          -> cbAfterDelay
//      vpi_remove_cb()
// 
// TODO:
//      vpi_iterate
//      vpi_scan
// 

#define _32BitSignal "tb_top.u_TestTop.l2_nodes.mmioBridge.entries_0.req_address"
#define _64BitSignal "tb_top.u_TestTop.l2_nodes.mmioBridge.entries_7.io_req_bits_data"
#define _256BitSignal "tb_top.u_TestTop.l2_nodes.linkMonitor_io_in_rx_dat_bits_data"
#define ClkSignal "top.masslav_if.clk"

uint32_t global_time = 0;

int main(int argc, const char *argv[]) {
    std::cout << "Hello, World!" << std::endl;
    wellen_wave_init(VCD_FILE);
    // wellen_test_1();
    // wellen_test(VCD_FILE);

    s_vpi_value v;
    v.format = vpiIntVal;

    auto signalHdl = vpi_handle_by_name(ClkSignal, NULL);
    for(int i = 0; i < 10; i++) {
        global_time = i * 8;
        vpi_get_value(signalHdl, &v);
        printf("[%d] #%d v => %d\n", i, global_time, v.value.integer);
    }

    auto signalHdl_1 = vpi_handle_by_name("top.masslav_if.Paddr", NULL);
    for(int i = 0; i < 10; i++) {
        global_time = i * 5;
        vpi_get_value(signalHdl_1, &v);
        printf("[%d] #%d v => %d\n", i, global_time, v.value.integer);
    }

    printf("vpiSize => %d\n", vpi_get(vpiSize, signalHdl));
    printf("vpiSize => %d\n", vpi_get(vpiSize, signalHdl_1));
    printf("vpiType => %s\n", vpi_get_str(vpiType, signalHdl));



    vpiHandle callback_handle;
    s_cb_data cb_data_s;

    cb_data_s.reason = cbStartOfSimulation;
    cb_data_s.cb_rtn = [](p_cb_data cb_data) {
        printf("Start of simulation callback\n");
        

        if(cb_data->user_data != NULL) {
            assert(false);
            free(cb_data->user_data);
        }
        return 0;
    };
    cb_data_s.obj = NULL; 
    cb_data_s.time = NULL; 
    cb_data_s.value = NULL; 
    cb_data_s.user_data = NULL; 
    callback_handle = vpi_register_cb(&cb_data_s);
    vpi_free_object(callback_handle);

    // vpi_iterate(vpiModule, NULL);
    return 0; 
}

PLI_INT32 vpi_free_object(vpiHandle object) {
    delete object;
    return 0;
}

PLI_INT32 vpi_release_handle(vpiHandle object) {
    delete object;
    return 0;
}

vpiHandle vpi_put_value(vpiHandle object, p_vpi_value value_p, p_vpi_time time_p, PLI_INT32 flags) {
    assert(false && "Unsupported");
    return 0;
}

vpiHandle vpi_handle_by_name(PLI_BYTE8 *name, vpiHandle scope) {
    // TODO: scope
    assert(scope == nullptr);
    return reinterpret_cast<vpiHandle>(wellen_vpi_handle_by_name(name));
}

void vpi_get_value(vpiHandle object, p_vpi_value value_p) {
    wellen_vpi_get_value(reinterpret_cast<void *>(object), global_time, value_p);
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

vpiHandle vpi_register_cb(p_cb_data cb_data_p) {
    switch (cb_data_p->reason) {
        case cbStartOfSimulation:
            cb_data_p->cb_rtn(cb_data_p);
            break;
        case cbEndOfSimulation:
            break;
        default:
            assert(false && "TODO");
            break;
    }
    // TODO:
    return nullptr;
}

PLI_INT32 vpi_remove_cb(vpiHandle cb_obj) {
    // TODO:
    return 0;
}