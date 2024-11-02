#include "wave_vpi.h"

#ifdef USE_FSDB
std::unique_ptr<FsdbWaveVpi> fsdbWaveVpi;

// Used by <ffrReadScopeVarTree2>
typedef struct {
    int desiredDepth;
    std::string_view fullName;
    fsdbVarIdcode retVarIdCode;
} FsdbTreeCbContext;

uint32_t currentDepth = 0;
char currentScope[MAX_SCOPE_DEPTH][256];

static bool_T fsdbTreeCb(fsdbTreeCBType cbType, void *cbClientData, void *cbData) {
    switch (cbType) {
        case FSDB_TREE_CBT_SCOPE: {
            fsdbTreeCBDataScope *scopeData = (fsdbTreeCBDataScope *)cbData;
            if (currentDepth < MAX_SCOPE_DEPTH - 1) {
                strcpy(currentScope[currentDepth], scopeData->name);
                currentDepth++;
            }
            break;
        }
        case FSDB_TREE_CBT_VAR: {
            auto contextData = (FsdbTreeCbContext *)cbClientData;
            if (contextData->desiredDepth != currentDepth) {
                return FALSE;
            }
            fsdbTreeCBDataVar *varData = (fsdbTreeCBDataVar *)cbData;

            char fullName[256] = "";
            for (int i = 0; i < currentDepth; ++i) {
                strcat(fullName, currentScope[i]);
                strcat(fullName, ".");
            }

            std::string varDataName = varData->name;
            std::size_t start, end;
            while ((start = varDataName.find('[')) != std::string::npos && (end = varDataName.find(']')) != std::string::npos) {
                if (end > start) {
                    varDataName.erase(start, end - start + 1);
                }
            }
            strcat(fullName, varDataName.c_str());

            if (std::string_view(fullName) == contextData->fullName) {
                // fmt::println("Full Name: {} varDataName: {} varIdx: {} depth: {} desiredDepth: {}", fullName, varDataName, varData->u.idcode, currentDepth, contextData->desiredDepth);
                contextData->retVarIdCode = varData->u.idcode;
                fsdbWaveVpi->varIdCodeCache[std::string(contextData->fullName)] = varData->u.idcode;
                return FALSE; // return FALSE to stop the traverse
            } else {
                std::string insertKeyStr = std::string(fullName);
                std::size_t start, end;
                while ((start = insertKeyStr.find('[')) != std::string::npos && (end = insertKeyStr.find(']')) != std::string::npos) {
                    if (end > start) {
                        insertKeyStr.erase(start, end - start + 1);
                    }
                }
                // The varIdCodeCache will store the varIdCode of the same scope depth into it even it is not required by the user.
                fsdbWaveVpi->varIdCodeCache[insertKeyStr] = varData->u.idcode;
            }
            break;
        }
        case FSDB_TREE_CBT_UPSCOPE: {
            currentDepth--;
            break;
        }
        default:
            return TRUE; // return TRUE to continue the traverse
    }
    return TRUE; // return TRUE to continue the traverse
}

FsdbWaveVpi::FsdbWaveVpi(ffrObject *fsdbObj, std::string_view waveFileName) : fsdbObj(fsdbObj), waveFileName(waveFileName) {
    char fsdbName[FSDB_MAX_PATH + 1] = {0};
    strncpy(fsdbName, this->waveFileName.c_str(), FSDB_MAX_PATH);

    if (FALSE == ffrObject::ffrIsFSDB(fsdbName)) {
        PANIC("not an fsdb file!", this->waveFileName);
    } else {
        ffrObject::ffrGetFSDBInfo(fsdbName, fsdbInfo);
        if (FSDB_FT_VERILOG != fsdbInfo.file_type) {
            PANIC("fsdb file type is not verilog", this->waveFileName);
        }

        // fsdbObj = std::make_shared<ffrObject>(ffrObject::ffrOpen3(fsdbName));
        // if (NULL == fsdbObj) {
        //     PANIC("ffrObject::ffrOpen() failed", this->waveFileName);
        // } else {
        //     fmt::println("[wave_vpi] FsdbWaveVpi open fsdb file:{} SUCCESS!", this->waveFileName);
        //     fflush(stdout);
        // }

        fsdbObj->ffrReadScopeVarTree();
        maxVarIdcode = fsdbObj->ffrGetMaxVarIdcode();

        fmt::println("[wave_vpi] FsdbWaveVpi start load all signals...");
        fflush(stdout);
        for (int i = FSDB_MIN_VAR_IDCODE; i <= TIME_TABLE_MAX_INDEX_VAR_CODE; i++) {
            // !! DO NOT try to load all signals !!
            fsdbObj->ffrAddToSignalList(i);
            sigArr[i - 1] = i;
        }
        fsdbObj->ffrLoadSignals();
        fmt::println("[wave_vpi] FsdbWaveVpi load all signals finish");
        fflush(stdout);

        if (sigNum > maxVarIdcode) {
            sigNum = maxVarIdcode - 1;
        }
        tbVcTrvsHdl = fsdbObj->ffrCreateTimeBasedVCTrvsHdl(sigNum, sigArr);
        if (NULL == tbVcTrvsHdl) {
            PANIC("Failed to create time based vc trvs hdl! please re-execute the program.", sigNum, sigArr, this->waveFileName);
        }

        bool useCachedData = false;
        std::ifstream lastModifiedTimeFile(LAST_MODIFIED_TIME_FILE);
        auto waveFileSize = std::filesystem::file_size(waveFileName);
        auto lastWriteTime = (uint64_t)std::filesystem::last_write_time(waveFileName).time_since_epoch().count();

        auto updateLastModifiedTimeFile = [&waveFileSize, &lastWriteTime]() {
            std::ofstream _lastModifiedTimeFile(LAST_MODIFIED_TIME_FILE);
            _lastModifiedTimeFile << waveFileSize << std::endl;
            _lastModifiedTimeFile << lastWriteTime << std::endl;
            _lastModifiedTimeFile.close();
        };

        if(lastModifiedTimeFile.is_open()) {
            std::string waveFileSizeStr;
            std::string lastModifiedTimeStr;
            std::string isFinish;

            try {
                std::getline(lastModifiedTimeFile, waveFileSizeStr);
                std::getline(lastModifiedTimeFile, lastModifiedTimeStr);
                std::getline(lastModifiedTimeFile, isFinish);
                lastModifiedTimeFile.close();
                
                auto _waveFileSize = std::stoull(waveFileSizeStr);
                auto _lastModifiedTime = std::stoull(lastModifiedTimeStr);
                
                if(_waveFileSize == waveFileSize && _lastModifiedTime == lastWriteTime) {
                    useCachedData = true;
                } else {
                    updateLastModifiedTimeFile();
                }
            } catch(std::invalid_argument &e) {
                fmt::println("[wave_vpi] FsdbWaveVpi ERROR while reading:{}! => std::invalid_argument", LAST_MODIFIED_TIME_FILE);
                updateLastModifiedTimeFile();
            }
        } else {
            updateLastModifiedTimeFile();
        }

        fmt::println("[wave_vpi] FsdbWaveVpi useCachedData: {}", useCachedData);

        if(useCachedData) {
            std::ifstream timeTableFile(TIME_TABLE_FILE, std::ios::binary);
            if(timeTableFile.is_open()) {
                std::size_t vecSize;

                timeTableFile.read(reinterpret_cast<char *>(&vecSize), sizeof(vecSize)); // The first elements is vector size
                xtagU64Vec.resize(vecSize);
                
                timeTableFile.read(reinterpret_cast<char *>(xtagU64Vec.data()), vecSize * sizeof(uint64_t));
                timeTableFile.close();

                xtagVec.resize(vecSize);
                for(size_t i = 0; i < vecSize; i++) {
                    xtagVec[i].hltag.H = xtagU64Vec[i] >> 32;
                    xtagVec[i].hltag.L = xtagU64Vec[i] & 0xFFFFFFFF;
                }

                fmt::println("[wave_vpi] FsdbWaveVpi read from timeTableFile => xtagU64Vec size: {}", xtagU64Vec.size());
            } else {
                fmt::println("[wave_vpi] FsdbWaveVpi failed to open {}, doing normal parse...", TIME_TABLE_FILE);
                goto NormalParse;
            }
        } else {
NormalParse:
            fmt::println("[wave_vpi] FsdbWaveVpi start collecting xtagU64Set");
            fflush(stdout);

            int i = 0;
            fsdbXTag xtag;
            std::set<uint64_t> xtagU64Set;
            while (FSDB_RC_SUCCESS == tbVcTrvsHdl->ffrGotoNextVC()) {
                tbVcTrvsHdl->ffrGetXTag((void *)&xtag);
                auto u64Xtag = Xtag64ToUInt64(xtag.hltag);
                if (xtagU64Set.find(u64Xtag) == xtagU64Set.end()) {
                    xtagU64Set.insert(u64Xtag);
                    xtagVec.emplace_back(xtag);
                }
                i++;
            }
            fmt::println("[wave_vpi] FsdbWaveVpi xtagU64Set size: {}, total size: {}", xtagU64Set.size(), i);
            fflush(stdout);

            // Create xtagU64Vec besed on xtagU64Set
            xtagU64Vec.assign(xtagU64Set.begin(), xtagU64Set.end());

            // Save time table into file so that we do not require much time to parse time table.
            std::ofstream timeTableFile(TIME_TABLE_FILE, std::ios::binary);
            std::size_t vecSize = xtagU64Vec.size();
            ASSERT(timeTableFile.is_open(), "Failed to open TIME_TABLE_FILE!", TIME_TABLE_FILE);
            timeTableFile.write(reinterpret_cast<char *>(&vecSize), sizeof(vecSize));
            timeTableFile.write(reinterpret_cast<char *>(xtagU64Vec.data()), vecSize * sizeof(uint64_t));
            ASSERT(timeTableFile, "Failed to write to file", TIME_TABLE_FILE);
            timeTableFile.close();
        }

        // Recreate tbVcTrvsHdl to reset the xtag to start point
        tbVcTrvsHdl->ffrFree();
        tbVcTrvsHdl = fsdbObj->ffrCreateTimeBasedVCTrvsHdl(sigNum, sigArr);
    }
}

fsdbVarIdcode FsdbWaveVpi::getVarIdCodeByName(char *name) {
    auto it = varIdCodeCache.find(std::string(name));
    if (it != varIdCodeCache.end()) {
        // fmt::println("found in varIdCodeCache! {}", name);
        // fflush(stdout);
        fsdbVarIdcode existingValue = it->second;
        return existingValue;
    }

    currentDepth = 0;
    for (int i = 0; i < MAX_SCOPE_DEPTH; i++) {
        std::memset(currentScope[i], 0, sizeof(currentScope[i]));
    }

    std::string fullName      = std::string(name);
    FsdbTreeCbContext contextData = {.desiredDepth = static_cast<int>(std::count(fullName.begin(), fullName.end(), '.')), .fullName = fullName, .retVarIdCode = 0};
    fsdbObj->ffrReadScopeVarTree2(fsdbTreeCb, (void *)&contextData);

    ASSERT(contextData.retVarIdCode != 0, "Failed to find varIdCode", name);

    return contextData.retVarIdCode;
}

uint32_t FsdbWaveVpi::findNearestTimeIndex(uint64_t time) {
    auto it = std::lower_bound(xtagU64Vec.begin(), xtagU64Vec.end(), time);

    if (it == xtagU64Vec.end()) {
        return xtagU64Vec.size() - 1;
    } else if (it == xtagU64Vec.begin()) {
        return 0;
    } else {
        uint32_t index = it - xtagU64Vec.begin();
        if (xtagU64Vec[index] == time) {
            return index;
        } else {
            uint32_t before = index - 1;
            if (xtagU64Vec[before] < time) {
                return before;
            } else {
                return index;
            }
        }
    }
}
#endif

WaveCursor cursor{0, 0, 0, 0};

std::unique_ptr<s_cb_data> startOfSimulationCb = NULL;
std::unique_ptr<s_cb_data> endOfSimulationCb = NULL;

std::queue<std::pair<uint64_t, std::shared_ptr<t_cb_data>>> timeCbQueue;
std::vector<std::pair<uint64_t, std::shared_ptr<t_cb_data>>> willAppendTimeCbQueue;

// The nextSimTimeQueue is a queue of callbacks that will be called at the next simulation time.
std::vector<std::shared_ptr<t_cb_data>> nextSimTimeQueue;
std::vector<std::shared_ptr<t_cb_data>> willAppendNextSimTimeQueue;

UNORDERED_MAP<vpiHandleRaw, ValueCbInfo> valueCbMap;
std::vector<std::pair<vpiHandleRaw, ValueCbInfo>> willAppendValueCb;
std::vector<vpiHandleRaw> willRemoveValueCb;

// The vpiHandleAllocator is a counter that counts the number of vpiHandles allocated which make it easy to provide unique vpiHandle values.
vpiHandleRaw vpiHandleAllcator = 0;

// UNORDERED_MAP<vpiHandle, std::string> hdlToNameMap; // For debug purpose

extern "C" void vlog_startup_routines_bootstrap();

void wave_vpi_init(const char *filename) {
#ifdef USE_FSDB
    fsdbWaveVpi = std::make_unique<FsdbWaveVpi>(ffrObject::ffrOpenNonSharedObj((char *)filename), std::string(filename));
    
    cursor.maxIndex = fsdbWaveVpi->xtagU64Vec.size() - 1;
    cursor.maxTime  = fsdbWaveVpi->xtagU64Vec.at(fsdbWaveVpi->xtagU64Vec.size() - 1);
#else
    wellen_wave_init(filename);

    cursor.maxIndex = wellen_get_max_index();
    cursor.maxTime = wellen_get_time_from_index(cursor.maxIndex);
#endif
}

void endOfSimulation(bool isSignalHandler = false) {
    static bool isEndOfSimulation = false;
    
    if(endOfSimulationCb && !isEndOfSimulation) {
        isEndOfSimulation = true;
#ifndef USE_FSDB
        wellen_vpi_finalize();
#endif
        endOfSimulationCb->cb_rtn(endOfSimulationCb.get());

        if(!isSignalHandler) {
            exit(0);
        }
    }
}

void sigint_handler(int unused) {
    VL_WARN(R"(
---------------------------------------------------------------------
----   wave_vpi_main get <SIGINT>, the program will terminate...      ----
---------------------------------------------------------------------
)");

    endOfSimulation(true);

    exit(0);
}

void sigabrt_handler(int unused) {
    VL_WARN(R"(
---------------------------------------------------------------------
----   wave_vpi_main get <SIGABRT>, the program will terminate...      ----
---------------------------------------------------------------------
)");

    endOfSimulation(true);

    exit(1);
}


inline static void appendTimeCb() {
    for(auto &cb : willAppendTimeCbQueue) {
        timeCbQueue.push(cb);
        // fmt::println("append appendTimeCb");
    }
    willAppendTimeCbQueue.clear();
}

inline static void appendValueCb() {
    if(!willAppendValueCb.empty()) {
        for(auto &cb : willAppendValueCb) {
            valueCbMap[cb.first] = cb.second;
            // fmt::println("append {}", cb.first);
        }
        willAppendValueCb.clear();
    }
}

inline static void removeValueCb() {
    if(!willRemoveValueCb.empty()) {
        for(auto &cb : willRemoveValueCb) {
            valueCbMap.erase(cb);
            // fmt::println("remove {}", cb);
        }
        willRemoveValueCb.clear();
    }
}

inline static void appendNextSimTimeCb() {
    for(auto &cb : willAppendNextSimTimeQueue) {
        nextSimTimeQueue.emplace_back(cb);
        // fmt::println("append nextSimTimeCb");
    }
    willAppendNextSimTimeQueue.clear();
}


void wave_vpi_main() {
    // Setup SIG handler so that we can exit gracefully
    std::signal(SIGINT, sigint_handler); // Deal with Ctrl-C
    std::signal(SIGABRT, sigabrt_handler); // Deal with assert

#ifndef NO_VLOG_STARTUP
    // Manually call vlog_startup_routines_bootstrap(), which is called at the beginning of the simulation according to VPI standard specification.
    vlog_startup_routines_bootstrap();
#endif

    // Call startOfSimulationCb if it exists
    if(startOfSimulationCb) {
        startOfSimulationCb->cb_rtn(startOfSimulationCb.get());
    }

    // Append callbacks which is registered from startOfSimulationCb
    appendTimeCb();
    appendNextSimTimeCb();
    appendValueCb();

    // Start wave_vpi evaluation loop
    ASSERT(cursor.maxIndex != 0);
    fmt::println("[wave_vpi] START! cursor.maxIndex => {} cursor.maxTime => {}", cursor.maxIndex, cursor.maxTime);

    while(cursor.index < cursor.maxIndex) {
        // Deal with cbAfterDelay(time) callbacks
        if(!timeCbQueue.empty()) {
            bool again = cursor.index >= timeCbQueue.front().first;
            while(again) {
                auto cb = timeCbQueue.front().second;
                cb->cb_rtn(cb.get());
                timeCbQueue.pop();
                again = !timeCbQueue.empty() && cursor.index >= timeCbQueue.front().first;
            }
        }
        appendTimeCb();

        // Deal with cbValueChange callbacks
        for(auto &cb : valueCbMap) {
            if (cb.second.cbData->cb_rtn != nullptr) [[likely]] {
                ASSERT(cb.second.cbData->obj != nullptr);
                ASSERT(cb.second.cbData->cb_rtn != nullptr);

                auto misMatch = false;
#ifdef USE_FSDB
                uint32_t newBitValue = 0;
                std::string newValueStr;
                if(cb.second.bitSize == 1) [[likely]] {
                    newBitValue = fsdbGetSingleBitValue(cb.second.handle);
                    if(newBitValue != cb.second.bitValue) {
                        misMatch = true;
                        cb.second.bitValue = newBitValue;
                    }
                } else [[unlikely]] { 
                    newValueStr = fsdbGetBinStr(cb.second.handle);
                    if(newValueStr != cb.second.valueStr) {
                        misMatch = true;
                        cb.second.valueStr = newValueStr;
                    }
                }
#else
                auto newValueStr = _wellen_get_value_str(&cb.second.handle);
                if(newValueStr != cb.second.valueStr) {
                    misMatch = true;
                    cb.second.valueStr = newValueStr;
                }
#endif
                // All the value change comparision is done by comparing the string of the value, which provides a more robust way to compare the value.
                if(misMatch) {                
                    // For now, the value change callback is only supported in vpiIntVal format.
                    switch (cb.second.cbData->value->format) {
                        [[likely]] case vpiIntVal: {
#ifdef USE_FSDB
                            if(cb.second.bitSize == 1) [[likely]] {
                                cb.second.cbData->value->value.integer = newBitValue;
                            } else [[unlikely]] {
                                cb.second.cbData->value->value.integer = std::stoi(newValueStr); // TODO: it seems incorrect?
                            }
#else
                            cb.second.cbData->value->value.integer = std::stoi(newValueStr); // TODO: it seems incorrect?
#endif
                            break;
                        }
                        default:
                            ASSERT(false, cb.second.cbData->value->format);
                            break;
                    }
                    cb.second.cbData->cb_rtn(cb.second.cbData.get());
                }
            }
        }

        // Deal with cbNextSimTime callbacks
        for(auto &cb : nextSimTimeQueue) {
            cb->cb_rtn(cb.get());
        }
        nextSimTimeQueue.clear(); // Clean the current cbNextSimTime callbacks
        appendNextSimTimeCb(); // Append callbacks which is registered from cbNextSimTime callbacks

        removeValueCb(); // Remove finished cbValueChange callbacks
        appendValueCb(); // Register newly registered cbValueChange callbacks from the previous cbNextSimTime callback

        cursor.index++; // Next simulation step
    }
    
#ifdef USE_FSDB
    fmt::println("[wave_vpi] FINISH! cursor.index => {} cursor.time => {}", cursor.index, fsdbWaveVpi->xtagU64Vec[cursor.index]);
#else
    fmt::println("[wave_vpi] FINISH! cursor.index => {} cursor.time => {}", cursor.index, wellen_get_time_from_index(cursor.index));
#endif
    
    // End of simulation
    endOfSimulation();
}


PLI_INT32 vpi_free_object(vpiHandle object) {
    if(object != nullptr) {
        ASSERT(false, "TODO:");
        if(valueCbMap.find(*object) != valueCbMap.end()) {
            valueCbMap.erase(*object);
        }
        delete object;
    }
    return 0;
}

PLI_INT32 vpi_release_handle(vpiHandle object) {
    return vpi_free_object(object);
}

vpiHandle vpi_put_value(vpiHandle object, p_vpi_value value_p, p_vpi_time time_p, PLI_INT32 flags) {
    ASSERT(false, "Unsupported in wave_vpi, all signals are read-only!");
    return 0;
}

vpiHandle vpi_handle_by_name(PLI_BYTE8 *name, vpiHandle scope) {
    // TODO: scope
    ASSERT(scope == nullptr);
#ifdef USE_FSDB
    auto varIdCode = fsdbWaveVpi->getVarIdCodeByName(name);
    auto hdl = fsdbWaveVpi->fsdbObj->ffrCreateVCTrvsHdl(varIdCode);
    if (!hdl) {
        PANIC("Failed to create value change traverse handle", name);
    }

    auto fsdbSigHdl = new FsdbSignalHandle {
        .name = std::string(name),
        .vcTrvsHdl = hdl,
        .varIdCode = varIdCode,
        .bitSize = hdl->ffrGetBitSize()
    };

    auto vpiHdl = reinterpret_cast<vpiHandle>(fsdbSigHdl);
#else
    auto vpiHdl = reinterpret_cast<vpiHandle>(wellen_vpi_handle_by_name(name)); 
#endif
    // hdlToNameMap[vpiHdl] = std::string(name); // For debug purpose
    return vpiHdl;
}

#ifdef USE_FSDB
void optThreadTask(std::string fsdbFileName, std::vector<fsdbXTag> xtagVec, FsdbSignalHandlePtr fsdbSigHdl) {
    static std::mutex optMutex;

    // Ensure only one `fsdbObj` can be processed for all the optimization threads.
    thread_local std::unique_lock<std::mutex> lock(optMutex);
    // thread_local std::lock_guard lock(optMutex);

    thread_local ffrObject *fsdbObj = ffrObject::ffrOpenNonSharedObj((char *)(fsdbFileName).c_str());
    ASSERT(fsdbObj != nullptr);
    fsdbObj->ffrReadScopeVarTree();

    thread_local auto hdl = fsdbObj->ffrCreateVCTrvsHdl(fsdbSigHdl->varIdCode);
    thread_local auto bitSize = hdl->ffrGetBitSize();
    ASSERT(hdl != nullptr, "Failed to create hdl", fsdbFileName, fsdbSigHdl->name, fsdbSigHdl->varIdCode);
    ASSERT(bitSize <= 32, "For now we only optimize signals with bitSize <= 32");

    byte_T *retVC;
    fsdbBytesPerBit bpb;
    for(auto idx = 0; idx < xtagVec.size() - 1; idx++) {
        uint32_t tmpVal = 0;
        auto time = xtagVec[idx];
        time.hltag.L = time.hltag.L + 1;

        ASSERT(FSDB_RC_SUCCESS == hdl->ffrGotoXTag(&time), "Failed to call hdl->ffrGotoXtag()", time.hltag.L, time.hltag.H, idx, fsdbSigHdl->name, fsdbFileName);
        ASSERT(FSDB_RC_SUCCESS == hdl->ffrGetVC(&retVC), "hdl->ffrGetVC() failed!");

        bpb = hdl->ffrGetBytesPerBit();

        if(bitSize == 1) {
            switch (bpb) {
            [[likely]] case FSDB_BYTES_PER_BIT_1B: {
                switch (retVC[0]) {
                    case FSDB_BT_VCD_X: // treat `X` as `0`
                    case FSDB_BT_VCD_Z: // treat `Z` as `0`
                    case FSDB_BT_VCD_0:
                        fsdbSigHdl->optValueVec.emplace_back(0);
                        break;
                    case FSDB_BT_VCD_1:
                        fsdbSigHdl->optValueVec.emplace_back(1);
                        break;
                    default:
                        PANIC("unknown verilog bit type found.");
                }
                break;
            }
            case FSDB_BYTES_PER_BIT_4B:
            case FSDB_BYTES_PER_BIT_8B:
                PANIC("TODO: FSDB_BYTES_PER_BIT_4B/8B", bpb);
                break;
            default:
                PANIC("Should not reach here!");
            }
        } else {
            switch (bpb) {
            [[likely]] case FSDB_BYTES_PER_BIT_1B: {
                for (int i = 0; i < bitSize; i++) {
                    switch (retVC[i]) {
                    case FSDB_BT_VCD_0:
                        break;
                    case FSDB_BT_VCD_1:
                        tmpVal += 1 << (bitSize - i - 1);
                        break;
                    case FSDB_BT_VCD_X:
                        // treat `X` as `0`
                        break;
                    case FSDB_BT_VCD_Z:
                        // treat `Z` as `0`
                        break;
                    default:
                        PANIC("unknown verilog bit type found.");
                    }
                }
                break;
            }
            case FSDB_BYTES_PER_BIT_4B:
                PANIC("TODO: FSDB_BYTES_PER_BIT_4B");
                break;
            case FSDB_BYTES_PER_BIT_8B:
                PANIC("TODO: FSDB_BYTES_PER_BIT_8B");
                break;
            default:
                PANIC("Should not reach here!");
            }
            fsdbSigHdl->optValueVec.emplace_back(tmpVal);
        }
    }

    fsdbObj->ffrClose();
    fsdbSigHdl->optFinish = true;

    // fmt::println("opt finish! {}", fsdbSigHdl->name);

    lock.unlock();
}
#endif

void vpi_get_value(vpiHandle object, p_vpi_value value_p) {
#ifdef USE_FSDB
    static byte_T buffer[FSDB_MAX_BIT_SIZE + 1];
    static s_vpi_vecval vpiValueVecs[100];
    auto fsdbSigHdl = reinterpret_cast<FsdbSignalHandlePtr>(object);
    
    if(fsdbSigHdl->optFinish) {
        switch (value_p->format) {
        case vpiIntVal: {
            value_p->value.integer = fsdbSigHdl->optValueVec[cursor.index];
            return;
        }
        case vpiVectorVal: {
            vpiValueVecs[0].aval = fsdbSigHdl->optValueVec[cursor.index];
            vpiValueVecs[0].bval = 0;
            value_p->value.vector = vpiValueVecs;
            return;
        }
        default:
            PANIC("Unsupported!", value_p->format);
        }
    } else {
        fsdbSigHdl->readCnt++;

        // Doing somthing like JIT(Just-In-Time)...
        if(!fsdbSigHdl->doOpt && fsdbSigHdl->bitSize <= 32 && fsdbSigHdl->readCnt > JTT_COMPILE_THRESHOLD) {
            fsdbSigHdl->doOpt = true;
            fsdbSigHdl->optThread = std::thread(std::bind(optThreadTask, fsdbWaveVpi->waveFileName, fsdbWaveVpi->xtagVec, fsdbSigHdl));
        }
    }

    auto vcTrvsHdl = fsdbSigHdl->vcTrvsHdl;
    byte_T *retVC;
    fsdbBytesPerBit bpb;
    size_t bitSize = fsdbSigHdl->bitSize;

    auto time = fsdbWaveVpi->xtagVec[cursor.index];
    time.hltag.L = time.hltag.L + 1; // Move a little bit further to ensure we are not in the sensitive clock edge which may lead to signal value confusion.
    
    if(FSDB_RC_SUCCESS != vcTrvsHdl->ffrGotoXTag(&time)) [[unlikely]] {
        auto currIndexTime = fsdbWaveVpi->xtagU64Vec[cursor.index];
        auto maxIndexTime = fsdbWaveVpi->xtagU64Vec[cursor.maxIndex];
        PANIC("vcTrvsHdl->ffrGotoXTag() failed!", time.hltag.L, time.hltag.H, maxIndexTime, currIndexTime, cursor.maxIndex, cursor.index);
    }
    if(FSDB_RC_SUCCESS != vcTrvsHdl->ffrGetVC(&retVC)) [[unlikely]] {
        PANIC("vcTrvsHdl->ffrGetVC() failed!");
    }

    bpb = vcTrvsHdl->ffrGetBytesPerBit();

    switch (value_p->format) {
    case vpiIntVal: {
        value_p->value.integer = 0;
        switch (bpb) {
        [[likely]] case FSDB_BYTES_PER_BIT_1B: {
            for (int i = 0; i < bitSize; i++) {
                switch (retVC[i]) {
                case FSDB_BT_VCD_0:
                    break;
                case FSDB_BT_VCD_1:
                    value_p->value.integer += 1 << (bitSize - i - 1);
                    break;
                case FSDB_BT_VCD_X:
                    // treat `X` as `0`
                    break;
                case FSDB_BT_VCD_Z:
                    // treat `Z` as `0`
                    break;
                default:
                    PANIC("unknown verilog bit type found.");
                }
            }
            break;
        }
        case FSDB_BYTES_PER_BIT_4B:
            PANIC("TODO: FSDB_BYTES_PER_BIT_4B");
            break;
        case FSDB_BYTES_PER_BIT_8B:
            PANIC("TODO: FSDB_BYTES_PER_BIT_8B");
            break;
        default:
            PANIC("Should not reach here!");
        }
        break;
    }
    case vpiVectorVal: {
        switch (bpb) {
        [[likely]] case FSDB_BYTES_PER_BIT_1B: {
            uint32_t chunkSize = 0;
            if ((bitSize % 32) == 0) {
                chunkSize = bitSize / 32;
            } else {
                chunkSize = bitSize / 32 + 1;
            }

            uint32_t tmpVal    = 0;
            uint32_t bufferIdx = 0;
            uint32_t tmpIdx    = 0;
            for (int i = bitSize - 1; i >= 0; i--) {
                switch (retVC[i]) {
                case FSDB_BT_VCD_0:
                    break;
                case FSDB_BT_VCD_1:
                    tmpVal += 1 << tmpIdx;
                    break;
                case FSDB_BT_VCD_X:
                    // treat `X` as `0`
                    break;
                case FSDB_BT_VCD_Z:
                    // treat `Z` as `0`
                    break;
                default:
                    PANIC("unknown verilog bit type found.", i);
                }
                tmpIdx++;
                if (tmpIdx == 32) {
                    vpiValueVecs[bufferIdx].aval = tmpVal;
                    vpiValueVecs[bufferIdx].bval = 0;
                    tmpVal                                       = 0;
                    tmpIdx                                       = 0;
                    bufferIdx++;
                }
            }

            if (tmpIdx != 0) {
                vpiValueVecs[bufferIdx].aval = tmpVal;
                vpiValueVecs[bufferIdx].bval = 0;
            }
            break;
        }
        case FSDB_BYTES_PER_BIT_4B:
            PANIC("TODO: FSDB_BYTES_PER_BIT_4B");
            break;
        case FSDB_BYTES_PER_BIT_8B:
            PANIC("TODO: FSDB_BYTES_PER_BIT_8B");
            break;
        default:
            PANIC("Should not reach here!");
        }
        value_p->value.vector = vpiValueVecs;
        break;
    }
    case vpiHexStrVal: {
        static const char hexLookUpTable[] = {'0', '1', '2', '3', '4', '4', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
        switch (bpb) {
        [[likely]] case FSDB_BYTES_PER_BIT_1B: {
            int bufferIdx = 0;
            int tmpVal    = 0;
            int tmpIdx    = 0;
            int chunkSize = 0;
            if ((bitSize % 4) == 0) {
                chunkSize = bitSize / 4;
            } else {
                chunkSize = bitSize / 4 + 1;
            }
            
            for (int i = vcTrvsHdl->ffrGetBitSize() - 1; i >= 0; i--) {
                switch (retVC[i]) {
                case FSDB_BT_VCD_0:
                    break;
                case FSDB_BT_VCD_1:
                    tmpVal += 1 << tmpIdx;
                    break;
                case FSDB_BT_VCD_X:
                    // treat `X` as `0`
                    break;
                case FSDB_BT_VCD_Z:
                    // treat `Z` as `0`
                    break;
                default:
                    PANIC("unknown verilog bit type found.", i);
                }
                tmpIdx++;
                if (tmpIdx == 4) {
                    buffer[chunkSize - 1 - bufferIdx] = hexLookUpTable[tmpVal];
                    tmpVal                            = 0;
                    tmpIdx                            = 0;
                    bufferIdx++;
                }
            }
            if (tmpIdx != 0) {
                buffer[chunkSize - 1 - bufferIdx] = hexLookUpTable[tmpVal];
                bufferIdx++;
            }
            buffer[bufferIdx] = '\0';
            break;
        }
        case FSDB_BYTES_PER_BIT_4B:
            PANIC("TODO: FSDB_BYTES_PER_BIT_4B");
            break;
        case FSDB_BYTES_PER_BIT_8B:
            PANIC("TODO: FSDB_BYTES_PER_BIT_8B");
            break;
        default:
            PANIC("Should not reach here!");
        }
        value_p->value.str = (char *)buffer;
        break;
    }
    [[unlikely]] case vpiBinStrVal: {
        switch (bpb) {
        [[likely]] case FSDB_BYTES_PER_BIT_1B: {
            int i = 0;
            for (i = 0; i < vcTrvsHdl->ffrGetBitSize(); i++) {
                switch (retVC[i]) {
                case FSDB_BT_VCD_0:
                    buffer[i] = '0';
                    break;
                case FSDB_BT_VCD_1:
                    buffer[i] = '1';
                    break;
                case FSDB_BT_VCD_X:
                    // treat `X` as `0`
                    buffer[i] = '0';
                    break;
                case FSDB_BT_VCD_Z:
                    // treat `Z` as `0`
                    buffer[i] = '0';
                    break;
                default:
                    PANIC("unknown verilog bit type found.");
                }
            }
            buffer[i] = '\0';
            break;
        }
        case FSDB_BYTES_PER_BIT_4B:
            PANIC("TODO: FSDB_BYTES_PER_BIT_4B");
            break;
        case FSDB_BYTES_PER_BIT_8B:
            PANIC("TODO: FSDB_BYTES_PER_BIT_8B");
            break;
        default:
            PANIC("Should not reach here!");
        }
        value_p->value.str = (char *)buffer;
        break;
    }
    default: {
        PANIC("Unknown value format", value_p->format);
    }
    }

    // auto format = std::string("");
    // fmt::println("\n[{}] retVC:{}", hdlToNameMap[object], retVC[0]);
    // if(value_p->format == vpiIntVal) {
    //     format = "vpiIntVal";
    //     fmt::println("[{}]@{} format:{} value: {}", hdlToNameMap[object], fsdbWaveVpi->xtagU64Vec[cursor.index], format, value_p->value.integer);
    // } else if(value_p->format == vpiVectorVal) {
    //     format = "vpiVectorVal";
    //     fmt::println("[{}]@{} format:{} value: {}", hdlToNameMap[object], fsdbWaveVpi->xtagU64Vec[cursor.index], format, value_p->value.vector[0].aval);
    // } else if (value_p->format == vpiHexStrVal) {
    //     format = "vpiHexStrVal";
    //     fmt::println("[{}]@{} format:{} value: {}", hdlToNameMap[object], fsdbWaveVpi->xtagU64Vec[cursor.index], format, value_p->value.str);
    // }
#else
    wellen_vpi_get_value_from_index(reinterpret_cast<void *>(object), cursor.index, value_p);
#endif
}

PLI_BYTE8 *vpi_get_str(PLI_INT32 property, vpiHandle object) {
#ifdef USE_FSDB
    switch (property) {
    case vpiType: {
        auto varType = reinterpret_cast<FsdbSignalHandle *>(object)->vcTrvsHdl->ffrGetVarType();
        switch (varType) {
        case FSDB_VT_VCD_REG:
            return "vpiReg";
        case FSDB_VT_VCD_WIRE:
            return "vpiNet";
        default:
            PANIC("Unknown fsdbVarType", varType);
        }
    }
    default:
        PANIC("Unimplemented property", property);
    }
#else
    return reinterpret_cast<PLI_BYTE8 *>(wellen_vpi_get_str(property, reinterpret_cast<void *>(object)));
#endif
};

PLI_INT32 vpi_get(PLI_INT32 property, vpiHandle object) {
#ifdef USE_FSDB
    switch (property) {
    case vpiSize:
        return reinterpret_cast<FsdbSignalHandlePtr>(object)->bitSize;
    default:
        PANIC("Unimplemented property", property);
    }
#else
    return wellen_vpi_get(property,reinterpret_cast<void *>(object));
#endif
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

vpiHandle vpi_handle_by_index(vpiHandle object, PLI_INT32 indx) {
    ASSERT(false, "TODO:");
    return nullptr;
}

PLI_INT32 vpi_control(PLI_INT32 operation, ...) {
    switch (operation) {
        case vpiStop:
        case vpiFinish:
            if(operation == vpiStop) {
                VL_INFO("get vpiStop\n");
            } else {
                VL_INFO("get vpiFinish\n");
            }
            endOfSimulation();
            // exit(0);
            break;
        default:
            ASSERT(false, "Unsupported operation", operation);
            break;
    }
    return 0;
}

#ifdef USE_FSDB
std::string fsdbGetBinStr(vpiHandle object) {
    s_vpi_value v;
    v.format = vpiBinStrVal;
    vpi_get_value(object, &v);
    return std::string(v.value.str);
}

uint32_t fsdbGetSingleBitValue(vpiHandle object) {
    s_vpi_value v;
    v.format = vpiIntVal;
    vpi_get_value(object, &v); // Use `vpi_get_value` since we have JIT-like feature in `vpi_get_value`
    return v.value.integer;

    // auto fsdbSigHdl = reinterpret_cast<FsdbSignalHandlePtr>(object);
    // auto vcTrvsHdl = fsdbSigHdl->vcTrvsHdl;
    // byte_T *retVC;
    // fsdbBytesPerBit bpb;

    // auto time = fsdbWaveVpi->xtagVec[cursor.index];
    // time.hltag.L = time.hltag.L + 1; // Move a little bit further to ensure we are not in the sensitive clock edge which may lead to signal value confusion.
    // if(FSDB_RC_SUCCESS != vcTrvsHdl->ffrGotoXTag(&time)) [[unlikely]] {
    //     auto currIndexTime = fsdbWaveVpi->xtagU64Vec[cursor.index];
    //     auto maxIndexTime = fsdbWaveVpi->xtagU64Vec[cursor.maxIndex];
    //     PANIC("vcTrvsHdl->ffrGotoXTag() failed!", time.hltag.L, time.hltag.H, maxIndexTime, currIndexTime, cursor.maxIndex, cursor.index);
    // }
    // if(FSDB_RC_SUCCESS != vcTrvsHdl->ffrGetVC(&retVC)) [[unlikely]] {
    //     PANIC("vcTrvsHdl->ffrGetVC() failed!");
    // }

    // bpb = vcTrvsHdl->ffrGetBytesPerBit();

    // switch (bpb) {
    // [[likely]] case FSDB_BYTES_PER_BIT_1B: {
    //     switch (retVC[0]) {
    //         case FSDB_BT_VCD_X: // treat `X` as `0`
    //         case FSDB_BT_VCD_Z: // treat `Z` as `0`
    //         case FSDB_BT_VCD_0:
    //             return 0;
    //         case FSDB_BT_VCD_1:
    //             return 1;
    //         default:
    //             PANIC("unknown verilog bit type found.");
    //     }
    //     break;
    // }
    // case FSDB_BYTES_PER_BIT_4B:
    // case FSDB_BYTES_PER_BIT_8B:
    //     PANIC("TODO: FSDB_BYTES_PER_BIT_4B/8B", bpb);
    //     break;
    // default:
    //     PANIC("Should not reach here!");
    // }

    // PANIC("Should not come here...");
}
#else
std::string _wellen_get_value_str(vpiHandle object) {
    ASSERT(object != nullptr);
    return std::string(wellen_get_value_str(reinterpret_cast<void *>(object), cursor.index));
}
#endif

vpiHandle vpi_register_cb(p_cb_data cb_data_p) {
    switch (cb_data_p->reason) {
        case cbStartOfSimulation:
            ASSERT(startOfSimulationCb == nullptr);
            startOfSimulationCb = std::make_unique<s_cb_data>(*cb_data_p);
            break;
        case cbEndOfSimulation:
            ASSERT(endOfSimulationCb == nullptr);
            endOfSimulationCb = std::make_unique<s_cb_data>(*cb_data_p);
            break;
        case cbValueChange: {
            ASSERT(cb_data_p->obj != nullptr);
            ASSERT(cb_data_p->cb_rtn != nullptr);
            ASSERT(cb_data_p->time != nullptr && cb_data_p->time->type == vpiSuppressTime);
            ASSERT(cb_data_p->value != nullptr && cb_data_p->value->format == vpiIntVal);
            
            auto t = *cb_data_p;
#ifdef USE_FSDB
            size_t bitSize = reinterpret_cast<FsdbSignalHandlePtr>(cb_data_p->obj)->bitSize;
            if(bitSize == 1) [[likely]] {
                willAppendValueCb.emplace_back(std::make_pair(vpiHandleAllcator, ValueCbInfo{
                    .cbData = std::make_shared<t_cb_data>(*cb_data_p), 
                    .handle = cb_data_p->obj,
                    .bitSize = 1,
                    .bitValue = fsdbGetSingleBitValue(cb_data_p->obj),
                    .valueStr = "", 
                }));
            } else [[unlikely]] {
                willAppendValueCb.emplace_back(std::make_pair(vpiHandleAllcator, ValueCbInfo{
                    .cbData = std::make_shared<t_cb_data>(*cb_data_p), 
                    .handle = cb_data_p->obj,
                    .bitSize = bitSize,
                    .bitValue = 0,
                    .valueStr = fsdbGetBinStr(cb_data_p->obj), 
                }));
            }
#else
            willAppendValueCb.emplace_back(std::make_pair(vpiHandleAllcator, ValueCbInfo{
                .cbData = std::make_shared<t_cb_data>(*cb_data_p), 
                .handle = *cb_data_p->obj,
                .valueStr = _wellen_get_value_str(cb_data_p->obj), 
            }));
#endif
            break;
        }
        case cbAfterDelay: {
            ASSERT(cb_data_p->time != nullptr && cb_data_p->time->type == vpiSimTime);
            
            uint64_t time = (((uint64_t) cb_data_p->time->high << 32) | (cb_data_p->time->low));
#ifdef USE_FSDB
            uint64_t targetTime = fsdbWaveVpi->xtagU64Vec[cursor.index] + time;
            uint64_t targetIndex = fsdbWaveVpi->findNearestTimeIndex(targetTime);
#else
            uint64_t targetTime = wellen_get_time_from_index(cursor.index) + time;
            uint64_t targetIndex = wellen_get_index_from_time(targetTime);            
#endif
            ASSERT(targetTime <= cursor.maxTime);

            willAppendTimeCbQueue.emplace_back(std::make_pair(targetIndex, std::make_shared<t_cb_data>(*cb_data_p)));
            break;
        }
        case cbNextSimTime: {
            ASSERT(cb_data_p->cb_rtn != nullptr);
            ASSERT(cb_data_p->obj == nullptr); // cbNextSimTime callbacks do not have an object handle.
            ASSERT(cb_data_p->value == nullptr);
            
            willAppendNextSimTimeQueue.emplace_back(std::make_shared<t_cb_data>(*cb_data_p));
            break;
        }
        default:
            ASSERT(false, "TODO:", cb_data_p->reason);
            break;
    }

    if(cb_data_p->reason == cbValueChange) {
        vpiHandleRaw handle = vpiHandleAllcator;
        vpiHandleAllcator++;
        return new vpiHandleRaw(handle);
    } else {
        return nullptr;
    }
}

PLI_INT32 vpi_remove_cb(vpiHandle cb_obj) {
    ASSERT(cb_obj != nullptr);
    if(valueCbMap.find(*cb_obj) != valueCbMap.end()) {
        willRemoveValueCb.emplace_back(*cb_obj);
    }
    delete cb_obj;
    return 0;
}

// Unsupport:
//      vpi_put_value(handle, &v, NULL, vpiNoDelay); // wave_vpi is considered a read-only waveform simulate backend in verilua
// 
// Supported:
//      OK => vpi_get_value(handle, &v);
//      OK => vpi_get_str(vpiType, actual_handle);
//      OK => vpi_get(vpiSize, actual_handle);
//      OK => vpi_handle_by_name(name)
//      OK => vpi_release_handle()
//      OK => vpi_free_object()
//      vpi_register_cb()
//          -> cbStartOfSimulation OK
//          -> cbEndOfSimulation   OK
//          -> cbValueChange       OK
//          -> cbAfterDelay        OK
//      vpi_remove_cb()            OK
// 
// TODO:
//      vpi_iterate
//      vpi_scan
// 
