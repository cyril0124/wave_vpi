local LuaSimConfig = require "LuaSimConfig"
local cfg = require "LuaBasicConfig"

local VeriluaMode = LuaSimConfig.VeriluaMode

cfg.name = "unknown"
cfg.target = "unknown"
cfg.top = os.getenv("DUT_TOP") or "tb_top"
cfg.simulator = os.getenv("SIM") or "wave_vpi"
cfg.mode = VeriluaMode.NORMAL
cfg.clock = "tb_top.clock"
cfg.reset = "tb_top.reset"
cfg.seed = os.getenv("SEED") or 0
cfg.attach = false
cfg.script = os.getenv("LUA_SCRIPT") or "/nfs/home/zhengchuyu/workspace/tmp/wave_vpi/example/LuaMain.lua"
cfg.prj_dir = os.getenv("PRJ_TOP") or "/nfs/home/zhengchuyu/workspace/tmp/wave_vpi/example"
cfg.srcs = {"/nfs/home/zhengchuyu/workspace/tmp/wave_vpi/example/?.lua"}
cfg.period = 10
cfg.unit = "ns"
cfg.enable_shutdown = true
cfg.shutdown_cycles = os.getenv("SHUTDOWN_CYCLES") or 10000
cfg.luapanda_debug = false
cfg.vpi_learn = false


--
-- Mix with other config
--
local function configs_len(cfg)
    local len = 0
    if cfg.configs ~= nil then
        for key, value in pairs(cfg.configs) do
            len = len + 1
        end
        return len
    else
        return 0
    end
end

if configs_len(cfg) ~= 0 then
    local found = false
    for k, v in pairs(cfg.configs) do
        if k == cfg.target then
            assert(not found, "Duplicate target => " .. cfg.target)
            assert(cfg.configs_path[k] ~= nil)
            package.path = package.path .. ";" .. cfg.configs_path[k] .. "/?.lua"
            
            local _cfg = require(v)
            LuaSimConfig.CONNECT_CONFIG(_cfg, cfg)
            found = true
        end
    end
    assert(found == true, "Not found any configs match target => " .. cfg.target) 
end
    

return cfg
