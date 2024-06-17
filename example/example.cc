#include "wave_vpi.h"
#include "vpi_user.h"
#include <signal.h>
#include <iostream>
#include <boost/stacktrace.hpp>

int main(int argc, const char *argv[]) {
    signal(SIGABRT, [](int sig) {
        std::cerr << boost::stacktrace::stacktrace() << std::endl;
        exit(1);
    });

    signal(SIGSEGV, [](int sig) {
        std::cerr << boost::stacktrace::stacktrace() << std::endl;
        exit(1);
    });

    // auto vcdFile = std::string(std::getenv("PRJ_DIR")) + "/wellen/wellen/inputs/vcs/Apb_slave_uvm_new.vcd";
    auto vcdFile = std::string(std::getenv("PRJ_DIR")) + "/example/test.vcd";
    fmt::println("vcdFile => {}", vcdFile);
    wave_vpi_init(vcdFile.c_str());
    wave_vpi_main();
}