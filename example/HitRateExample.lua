local clock            = dut.clock:chdl()
local mainPipe         = dut.u_TestTop.l2_nodes.slices_0.mainPipe
local task_s3_valid    = mainPipe.task_s3_valid
local task_s3_mshrTask = mainPipe.task_s3_bits_mshrTask
local task_s3_channel  = mainPipe.task_s3_bits_channel
local dirResp_hit      = mainPipe.io_dirResp_s3_hit

local hit_cnt = 0
local miss_cnt = 0
local CHANNEL_A = ("0b001"):number()

local bundle_a = ([[
    | valid
    | ready
    | address
    | source
]]):bundle {hier = cfg.top .. ".u_TestTop.l2_nodes", prefix = "auto_in_a_", name = "bundle_a"}

verilua "appendTasks" {
    main_task = function()
        repeat
            if task_s3_valid:get() == 1 and task_s3_mshrTask:get() == 0 and task_s3_channel:get() == CHANNEL_A then
                if dirResp_hit:get() == 1 then
                    hit_cnt = hit_cnt + 1
                else
                    miss_cnt = miss_cnt + 1
                end
            end
            clock:posedge()
        until false
    end,

    monitor_task = function ()
        repeat
            if bundle_a:fire() then
                bundle_a:dump()
            end
            clock:posedge()
        until false
    end
}

verilua "appendFinishTasks" {
    function ()
        print(("\nhit => %d  miss => %d  miss_rate => %.2f%%\n"):format(hit_cnt, miss_cnt, (miss_cnt * 100) / (hit_cnt + miss_cnt)))
    end
}
