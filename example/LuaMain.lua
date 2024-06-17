local format = string.format

local cycles = dut.cycles:chdl()
local clock = dut.clock:chdl()
local bundle_a = ([[
    | valid
    | ready
    | address
    | source
]]):bundle {hier = cfg.top .. ".u_TestTop.l2_nodes", prefix = "auto_in_a_"}

local bundle_c = ([[
    | valid
    | ready
    | address
    | source
    | data
]]):bundle {hier = cfg.top .. ".u_TestTop.l2_nodes", prefix = "auto_in_c_"}

local a_count = 0
local c_count = 0

verilua "appendTasks" {
    main_task = function()
        print("before await_time cycles is ", cycles:get())
        await_time(1000 * 10)
        print("after await_time cycles is ", cycles:get())


        clock:posedge(100 * 10000, function (c)
            if bundle_a:fire() then
                a_count = a_count + 1
                print(cycles:get(), "A Channel is fire! =>", "address: " .. string.format("0x%x", bundle_a.bits.address:get()), "source: " .. bundle_a.bits.source:get())
            end
        end)
    end,

    another_task = function()
        clock:posedge(100 * 10000, function (c)
            if bundle_c:fire() then
                c_count = c_count + 1
                print(cycles:get(), "C Channel is fire! =>", "address: " .. string.format("0x%x", bundle_c.bits.address:get()), "source: " .. bundle_c.bits.source:get(), "data: " .. bundle_c.bits.data:get()[1])
            end
        end)
    end
}

verilua "appendFinishTasks" {
    function ()
        print(format("a => %d  c => %d  c_percent => %.2f", a_count, c_count, (c_count * 100) / (a_count + c_count)) .. "%")
    end
}

