#!/bin/bash

verilua_run \
    --lua-main HitRateExample.lua \
    --top TestTop \
    --tb-top tb_top \
    --sim wave_vpi \
    -p . \
    -wf ./test.vcd.fst


