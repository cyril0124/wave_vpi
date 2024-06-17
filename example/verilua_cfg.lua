local pwd = os.getenv("PWD")

basic = {
    test_dir = os.getenv("$PWD"),
    top = "tb_top",
    simulator = "wave_vpi",
    mode = "normal",
    script = pwd .. "/LuaMain.lua",
    period = 10,
    unit = "ns",
    seed = 0,
    attach = false,
    enable_shutdown = true,
    shutdown_cycles = 10000,
    srcs = {
        "./?.lua",
    },
}

