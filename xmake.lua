add_rules("mode.debug", "mode.release")

add_requires("conan::fmt/11.0.2", {alias = "fmt"})
add_requires("conan::catch2/3.7.0", {alias = "catch2"})

target("tests", function()
    set_kind("binary")
    add_files(
        "./src/wave_vpi.cc", 
        "./tests/unit_test.cc"
    )

    add_includedirs(
        "./src", 
        "./tests"
    )

    add_cxflags("-DNO_VLOG_STARTUP")

    add_links("wave_vpi_wellen_impl")
    add_linkdirs("./target/release")
    
    add_packages("fmt", "catch2")

    add_runenvs("LD_LIBRARY_PATH", os.getenv("PWD") .. "/target/release")
    add_runenvs("PRJ_DIR", os.getenv("PWD"))
end)
