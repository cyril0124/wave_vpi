add_rules("mode.debug", "mode.release")

target("tests")
    set_kind("binary")
    add_files(
        "./src/wave_vpi.cc", 
        "./tests/unit_test.cc"
    )

    add_includedirs(
        "./src", 
        "./tests", 
        "./vcpkg/installed/x64-linux", 
        "./vcpkg/installed/x64-linux/include"
    )

    add_cxflags("-DNO_VLOG_STARTUP")

    add_links("wave_vpi")
    add_linkdirs("./target/release")
    
    add_links("fmt")
    add_links("assert", "cpptrace", "dwarf", "zstd", "z")
    add_links("Catch2")
    add_linkdirs("./vcpkg/installed/x64-linux/lib")

    add_runenvs("LD_LIBRARY_PATH", os.getenv("PWD") .. "/target/release")
    add_runenvs("LD_LIBRARY_PATH", os.getenv("PWD") .. "/vcpkg/installed/x64-linux/lib")
    add_runenvs("PRJ_DIR", os.getenv("PWD"))

target("example")
    set_kind("binary")
    add_files(
        "./src/wave_vpi.cc", 
        "./src/wave_dpi.cc", 
        "./example/example.cc"
    )

    add_includedirs(
        "./src", 
        "./vcpkg/installed/x64-linux", 
        "./vcpkg/installed/x64-linux/include"
    )

    if is_mode("debug") then
        add_defines("DEBUG")
        set_symbols("debug")
        set_optimize("none")
    end

    add_cxflags("-DBOOST_STACKTRACE_USE_BACKTRACE")

    add_links("fmt")
    add_links("assert", "cpptrace", "dwarf", "zstd", "z")
    add_links("backtrace", "boost_stacktrace_basic")
    add_linkdirs("./vcpkg/installed/x64-linux/lib")
    
    add_links("lua_vpi")
    add_links("luajit-5.1")
    add_linkdirs(os.getenv("VERILUA_HOME") .. "/shared")
    add_linkdirs(os.getenv("VERILUA_HOME") .. "/luajit2.1/lib")
    
    add_links("wave_vpi")
    add_linkdirs("./target/release")

    add_runenvs("LD_LIBRARY_PATH", os.getenv("PWD") .. "/vcpkg/installed/x64-linux/lib")
    add_runenvs("LD_LIBRARY_PATH", os.getenv("VERILUA_HOME") .. "/shared")
    add_runenvs("LD_LIBRARY_PATH", os.getenv("PWD") .. "/target/release")

    add_runenvs("PRJ_DIR", os.getenv("PWD"))
    add_runenvs("DUT_TOP", "tb_top")
    add_runenvs("VERILUA_CFG", os.getenv("PWD") .. "/example/verilua_cfg_unknown")

--
-- If you want to known more usage about xmake, please see https://xmake.io
--
-- ## FAQ
--
-- You can enter the project directory firstly before building project.
--
--   $ cd projectdir
--
-- 1. How to build project?
--
--   $ xmake
--
-- 2. How to configure project?
--
--   $ xmake f -p [macosx|linux|iphoneos ..] -a [x86_64|i386|arm64 ..] -m [debug|release]
--
-- 3. Where is the build output directory?
--
--   The default output directory is `./build` and you can configure the output directory.
--
--   $ xmake f -o outputdir
--   $ xmake
--
-- 4. How to run and debug target after building project?
--
--   $ xmake run [targetname]
--   $ xmake run -d [targetname]
--
-- 5. How to install target to the system directory or other output directory?
--
--   $ xmake install
--   $ xmake install -o installdir
--
-- 6. Add some frequently-used compilation flags in xmake.lua
--
-- @code
--    -- add debug and release modes
--    add_rules("mode.debug", "mode.release")
--
--    -- add macro definition
--    add_defines("NDEBUG", "_GNU_SOURCE=1")
--
--    -- set warning all as error
--    set_warnings("all", "error")
--
--    -- set language: c99, c++11
--    set_languages("c99", "c++11")
--
--    -- set optimization: none, faster, fastest, smallest
--    set_optimize("fastest")
--
--    -- add include search directories
--    add_includedirs("/usr/include", "/usr/local/include")
--
--    -- add link libraries and search directories
--    add_links("tbox")
--    add_linkdirs("/usr/local/lib", "/usr/lib")
--
--    -- add system link libraries
--    add_syslinks("z", "pthread")
--
--    -- add compilation and link flags
--    add_cxflags("-stdnolib", "-fno-strict-aliasing")
--    add_ldflags("-L/usr/local/lib", "-lpthread", {force = true})
--
-- @endcode
--

