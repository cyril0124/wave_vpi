#include "catch2/matchers/catch_matchers.hpp"
#include "wave_vpi.h"
#include "vpi_user.h"
#include <catch.hpp>
#include "catch2/catch_test_macros.hpp"
#include "fmt/core.h"
#include <cstdlib>

TEST_CASE("vpi_register_cb", "[vpi_register_cb]") {
    s_cb_data cb_data;
    s_vpi_time vpi_time;
    s_vpi_value vpi_value;

    vpi_time.type = vpiSuppressTime;
    vpi_value.format = vpiIntVal;
    cb_data.reason = cbValueChange;
    cb_data.cb_rtn = [](p_cb_data cb_data) {
        fmt::println("hello from cbValueChange");
        return 0;
    };
}

extern WaveCursor cursor;

TEST_CASE("vpi_get_value", "[vpi_get_value]") {
    auto hdl = vpi_handle_by_name("top.masslav_if.clk", nullptr);
    auto hdl2 = vpi_handle_by_name("top.masslav_if.Paddr", nullptr);

    s_vpi_value v{.format = vpiIntVal};
    for(int i = 0; i < 10; i++) {
        cursor.updateTime(i * 8);
        vpi_get_value(reinterpret_cast<vpiHandle>(hdl), &v);
        fmt::println("[{}] #{} v => {}", i, cursor.time, v.value.integer);
    }

    for(int i = 0; i < 10; i++) {
        cursor.updateTime(i * 5);
        v.format = vpiIntVal;
        vpi_get_value(reinterpret_cast<vpiHandle>(hdl2), &v);
        fmt::println("[{}] #{} v => {}", i, cursor.time, v.value.integer);

        v.format = vpiBinStrVal;
        vpi_get_value(reinterpret_cast<vpiHandle>(hdl2), &v);
        fmt::println("[{}] #{} v => {}", i, cursor.time, v.value.str);
    }

    v.format = vpiHexStrVal;
    vpi_get_value(reinterpret_cast<vpiHandle>(hdl2), &v);
    fmt::println("v => {}", v.value.str);
}

TEST_CASE("vpi_get/vpi_get_str", "[vpi_get/vpi_get_str]") {
    auto hdl = vpi_handle_by_name("top.masslav_if.clk", nullptr);
    auto hdl2 = vpi_handle_by_name("top.masslav_if.Paddr", nullptr);

    REQUIRE(vpi_get(vpiSize, hdl) == 1);
    REQUIRE(vpi_get(vpiSize, hdl2) == 32);
    REQUIRE(std::string(vpi_get_str(vpiType, hdl)) == "vpiReg");
}
 

int main(int argc, const char *argv[]) {
    auto vcdFile = std::string(std::getenv("PRJ_DIR")) + "/wellen/wellen/inputs/vcs/Apb_slave_uvm_new.vcd";
    fmt::println("vcdFile => {}", vcdFile);
    wave_vpi_init(vcdFile.c_str());

    int result = Catch::Session().run(argc, argv);

    if (result != 0) {
        fmt::println("Tests failed with return code {}", result);
    } else {
        fmt::println("All tests passed!");
    }
}