#![allow(non_upper_case_globals)]
#![allow(non_snake_case)]

use byteorder::{BigEndian, ByteOrder};
use bytesize::ByteSize;
use serde::{Deserialize, Serialize};
use std::borrow::Borrow;
use std::collections::HashMap;
use std::ffi::{CStr, CString};
use std::fs;
use std::fs::File;
use std::io::{BufReader, BufWriter};
use std::os::raw::{c_char, c_void};
use std::os::unix::fs::MetadataExt;
use std::time::UNIX_EPOCH;
use wellen::*;

#[allow(warnings)]
mod vpi {
    include!("vpi_user.rs");
}

use vpi::*;

#[repr(C)]
pub struct VecData {
    ptr: *const i32,
    len: usize,
}

#[no_mangle]
pub extern "C" fn create_vec() -> VecData {
    let vec = vec![1, 2, 3, 4, 5];

    let vec_data = VecData {
        ptr: vec.as_ptr(),
        len: vec.len(),
    };

    std::mem::forget(vec); // Prevent Rust from deallocating the Vec

    vec_data
}

static mut TIME_TABLE: Option<Vec<u64>> = None;
static mut HIERARCHY: Option<Hierarchy> = None;
static mut WAVE_SOURCE: Option<SignalSource> = None;

const LOAD_OPTS: LoadOptions = LoadOptions {
    multi_thread: true,
    remove_scopes_with_empty_name: false,
};

#[allow(non_camel_case_types)]
type vpiHandle = SignalRef;

#[derive(Debug, Serialize, Deserialize)]
struct FileModifiedInfo {
    size: u64,
    time: u64,
}

#[derive(Debug, Serialize, Deserialize)]
struct SignalInfo {
    pub signal: Signal,
    pub var_type: VarType,
}

const SIGNAL_REF_COUNT_THRESHOLD: usize = 15; // If the cached signal ref count is greater than this, we will not use the cached data.

static mut SIGNAL_REF_CACHE: Option<HashMap<String, SignalRef>> = None;
static mut SIGNAL_CACHE: Option<HashMap<SignalRef, SignalInfo>> = None;
static mut HAS_NEWLY_ADD_SIGNAL_REF: bool = false;

const LAST_MODIFIED_TIME_FILE: &str = "last_modified_time.wave_vpi.yaml";
const SIGNAL_REF_COUNT_FILE: &str = "signal_ref_count.wave_vpi.yaml";
const SIGNAL_REF_CACHE_FILE: &str = "signal_ref_cache.wave_vpi.yaml";
const SIGNAL_CACHE_FILE: &str = "signal_cache.wave_vpi.yaml";

#[no_mangle]
pub extern "C" fn wellen_wave_init(filename: *const c_char) {
    let c_str = unsafe {
        assert!(!filename.is_null());
        CStr::from_ptr(filename)
    };

    let r_str = c_str.to_str().unwrap();
    let filename = r_str;

    let header = viewers::read_header(&filename, &LOAD_OPTS).expect("Failed to load file!");
    let hierarchy = header.hierarchy;
    let body = viewers::read_body(header.body, &hierarchy, None).expect("Failed to load body!");
    let wave_source = body.source;
    wave_source.print_statistics();
    println!("[wellen_wave_init] The hierarchy takes up at least {} of memory.", ByteSize::b(hierarchy.size_in_memory() as u64));

    unsafe {
        TIME_TABLE = Some(body.time_table);
        HIERARCHY = Some(hierarchy);
        WAVE_SOURCE = Some(wave_source);

        println!("[wellen_wave_init] Time table size: {}", TIME_TABLE.clone().unwrap().len());
    }

    // If the wave file has not been modified, we can use the cached data to speed up the simulation.
    let mut use_cached_data = false;
    if let Ok(file) = File::open(LAST_MODIFIED_TIME_FILE) {
        let reader = BufReader::new(file);
        let modified_time: FileModifiedInfo = serde_yaml::from_reader(reader).expect(format!("Failed to parse {}", LAST_MODIFIED_TIME_FILE).as_str());
        let last_modified_timestamp = modified_time.time;
        let last_file_size = modified_time.size;

        let metadata = fs::metadata(filename).expect("Failed to get file metadata");
        let file_size = metadata.size();
        let modified = metadata.modified().expect("Failed to get modified time");
        let duration_since_epoch = modified.duration_since(UNIX_EPOCH).unwrap();
        let modified_timestamp = duration_since_epoch.as_secs();

        // Update the timestamp if the file has been modified.
        if last_modified_timestamp != modified_timestamp || last_file_size != file_size {
            let file = File::create(LAST_MODIFIED_TIME_FILE).unwrap();
            let writer = BufWriter::new(file);

            let modified_time = FileModifiedInfo {
                size: file_size,
                time: modified_timestamp,
            };

            println!("[wellen_wave_init] modified_timestamp: last({}) curr({})  file_size: last({}) curr({})", last_modified_timestamp, modified_timestamp, last_file_size, file_size);

            serde_yaml::to_writer(writer, &modified_time).unwrap();
        } else {
            if let Ok(file) = File::open(SIGNAL_REF_COUNT_FILE) {
                let reader: BufReader<File> = BufReader::new(file);
                let signal_ref_count: usize = serde_yaml::from_reader(reader).expect(format!("Failed to parse {}", SIGNAL_REF_COUNT_FILE).as_str());

                let signal_ref_count_threshold = SIGNAL_REF_COUNT_THRESHOLD;
                println!("[wellen_wave_init] signal_ref_count: {} signal_ref_count_threshold: {}", signal_ref_count, signal_ref_count_threshold);

                if signal_ref_count >= signal_ref_count_threshold {
                    use_cached_data = true;
                }
            } else {
                use_cached_data = true;
            }
        }
    } else {
        // Create new file if it does not exist.
        let metadata = fs::metadata(filename).expect("Failed to get file metadata");
        let file_size = metadata.size();
        let modified = metadata.modified().expect("Failed to get modified time");
        let duration_since_epoch = modified.duration_since(UNIX_EPOCH).unwrap();
        let modified_timestamp = duration_since_epoch.as_secs();

        let file = File::create(LAST_MODIFIED_TIME_FILE).unwrap();
        let writer = BufWriter::new(file);

        let modified_time = FileModifiedInfo {
            size: file_size,
            time: modified_timestamp,
        };

        println!("[wellen_wave_init] modified_timestamp(new): {}  file_size(new): {}", modified_timestamp, file_size);

        serde_yaml::to_writer(writer, &modified_time).unwrap();
    }
    println!("[wellen_wave_init] use_cached_data => {}", use_cached_data);

    unsafe {
        if SIGNAL_REF_CACHE.is_none() {
            if use_cached_data {
                println!("[wellen_wave_init] start read {}", SIGNAL_REF_CACHE_FILE);
                let _file = File::open(SIGNAL_REF_CACHE_FILE);
                if let Ok(file) = _file {
                    let reader = BufReader::new(file);
                    SIGNAL_REF_CACHE = Some(serde_yaml::from_reader(reader).unwrap());
                } else {
                    println!("[wellen_wave_init] Failed to open {}", SIGNAL_REF_CACHE_FILE);
                    SIGNAL_REF_CACHE = Some(HashMap::new());
                }
            } else {
                SIGNAL_REF_CACHE = Some(HashMap::new());
            }
        }

        if SIGNAL_CACHE.is_none() {
            if use_cached_data {
                println!("[wellen_wave_init] start read {}", SIGNAL_CACHE_FILE);
                let _file = File::open(SIGNAL_CACHE_FILE);
                if let Ok(file) = _file {
                    let reader = BufReader::new(file);
                    SIGNAL_CACHE = Some(serde_yaml::from_reader(reader).unwrap());
                } else {
                    println!("[wellen_wave_init] Failed to open signal cache file: {}", SIGNAL_CACHE_FILE);
                    SIGNAL_CACHE = Some(HashMap::new());
                }
            } else {
                SIGNAL_CACHE = Some(HashMap::new());
            }
        }
    }

    println!("[wellen_wave_init] init finish...");
}

#[no_mangle]
pub unsafe extern "C" fn wellen_vpi_handle_by_name(name: *const c_char) -> *mut c_void {
    let name = unsafe {
        assert!(!name.is_null());
        CStr::from_ptr(name)
    }
    .to_str()
    .unwrap();

    let id_opt = SIGNAL_REF_CACHE.as_mut().unwrap().get(&name.to_string());
    if let Some(id) = id_opt {
        // println!("[wellen_vpi_handle_by_name] find vpiHandle => name:{} id:{:?}", name, id);
        let value = Box::new(id.clone() as vpiHandle);
        return Box::into_raw(value) as *mut c_void;
    }

    let id = unsafe {
        HIERARCHY.as_ref().unwrap().iter_vars().find_map(|var| {
            let signal_name = var.full_name(&HIERARCHY.as_ref().unwrap());
            if signal_name == name.to_string() {
                let ids = [var.signal_ref(); 1];
                let loaded = WAVE_SOURCE.as_mut().unwrap().load_signals(&ids, &HIERARCHY.as_ref().unwrap(), LOAD_OPTS.multi_thread);
                let (loaded_id, loaded_signal) = loaded.into_iter().next().unwrap();
                assert_eq!(loaded_id, ids[0]);

                SIGNAL_CACHE.as_mut().unwrap().insert(
                    loaded_id,
                    SignalInfo {
                        signal: loaded_signal,
                        var_type: var.var_type(),
                    },
                );

                Some(loaded_id as vpiHandle)
            } else {
                None
            }
        })
    };
    assert!(id.is_some(), "[wellen_vpi_handle_by_name] cannot find vpiHandle => name:{}", name);
    // println!("[wellen_vpi_handle_by_name] find vpiHandle => name:{} id:{:?}", name, id);

    SIGNAL_REF_CACHE.as_mut().unwrap().insert(name.to_string(), id.unwrap());
    HAS_NEWLY_ADD_SIGNAL_REF = true;

    let value = Box::new(id.unwrap() as vpiHandle);
    Box::into_raw(value) as *mut c_void
}

#[no_mangle]
pub extern "C" fn wellen_vpi_release_handle(_handle: *mut c_void) {
    todo!();
    // if !handle.is_null() {
    //     unsafe {
    //         Box::from_raw(_handle as *mut vpiHandle);
    //     }
    // }
}

fn bytes_to_u32s_be(bytes: &[u8]) -> Vec<u32> {
    let mut u32s = Vec::with_capacity((bytes.len() + 3) / 4);

    let padded_bytes = if bytes.len() % 4 != 0 {
        let ret = 4 - bytes.len() % 4;
        match ret {
            | 1 => [vec![0], bytes.to_vec()].concat(),
            | 2 => [vec![0, 0], bytes.to_vec()].concat(),
            | 3 => [vec![0, 0, 0], bytes.to_vec()].concat(),
            | _ => unreachable!(),
        }
    } else {
        Vec::from(bytes)
    };

    for chunk in padded_bytes.chunks(4) {
        let value = BigEndian::read_u32(chunk);
        u32s.push(value);
    }

    u32s
}

pub const fn cover_with_32(size: usize) -> usize {
    (size + 31) / 32
}

fn find_nearest_time_index(time_table: &[u64], time: u64) -> usize {
    match time_table.binary_search_by(|&probe| {
        if probe < time {
            std::cmp::Ordering::Less
        } else if probe > time {
            std::cmp::Ordering::Greater
        } else {
            std::cmp::Ordering::Equal
        }
    }) {
        | Ok(index) => index,
        | Err(index) => {
            if index == 0 {
                0
            } else if index == time_table.len() {
                time_table.len() - 1
            } else {
                let before = index - 1;
                if time_table[before] < time {
                    before
                } else {
                    index
                }
            }
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn wellen_vpi_get_value_from_index(handle: *mut c_void, time_table_idx: u64, value_p: p_vpi_value) {
    let handle = unsafe { *{ handle as *mut vpiHandle } };
    let v_format = value_p.read().format;

    let loaded_signal = SIGNAL_CACHE.as_ref().unwrap().get(&(handle as vpiHandle)).unwrap().signal.borrow();
    let off = loaded_signal.get_offset(time_table_idx as u32).unwrap();
    let _wave_time = TIME_TABLE.as_ref().unwrap()[time_table_idx as usize];
    let signal_bit_string = loaded_signal.get_value_at(&off, 0).to_bit_string().unwrap();
    let signal_v = loaded_signal.get_value_at(&off, 0);

    // TODO: performance
    match signal_v {
        | SignalValue::Binary(data, _bits) => {
            let words = bytes_to_u32s_be(data);
            // println!("data => {:?} ww => {:?}   {}", data, words, words[words.len() - 1]);

            match v_format as u32 {
                | vpiVectorVal => {
                    let mut vecvals = Vec::new();
                    for i in 0..words.len() {
                        vecvals.insert(
                            0,
                            t_vpi_vecval {
                                aval: words[i] as i32,
                                bval: 0,
                            },
                        );
                    }
                    let vecvals_box = vecvals.into_boxed_slice();
                    let vecvals_ptr = vecvals_box.as_ptr() as *mut t_vpi_vecval;
                    let _ = Box::into_raw(vecvals_box);
                    (*value_p).value.vector = vecvals_ptr;
                }
                | vpiIntVal => {
                    let value = words[words.len() - 1] as i32;
                    (*value_p).value.integer = value;
                }
                | vpiHexStrVal => {
                    const chunk_size: u32 = 4;
                    // let hex_digits = _bits / chunk_size;

                    let chunks: Vec<Vec<u8>> = signal_bit_string.as_bytes().chunks(chunk_size as usize).map(|chunk| chunk.iter().map(|&x| x - b'0').collect::<Vec<u8>>()).collect();
                    let hex_string: String = chunks
                        .iter()
                        .map(|chunk| {
                            let bin_value = chunk.iter().rev().enumerate().fold(0, |acc, (i, &b)| acc | (b << i));
                            format!("{:x}", bin_value)
                        })
                        .collect();

                    let c_string = CString::new(hex_string).expect("CString::new failed");
                    let c_str_ptr = c_string.into_raw();
                    (*value_p).value.str_ = c_str_ptr as *mut PLI_BYTE8;
                    // todo!("vpiHexStrVal signal_bit_string => {} bits => {} hex_digits => {} {}", signal_bit_string, _bits, hex_digits, hex_string);
                }
                | vpiBinStrVal => {
                    let c_string = CString::new(signal_bit_string).expect("CString::new failed");
                    let c_str_ptr = c_string.into_raw();
                    (*value_p).value.str_ = c_str_ptr as *mut PLI_BYTE8;
                }
                | _ => {
                    todo!("v_format => {}", v_format)
                }
            };
        }
        | SignalValue::FourValue(_data, bits) => {
            match v_format as u32 {
                | vpiVectorVal => {
                    let vec_len = cover_with_32(bits as usize);
                    let mut vecvals = Vec::new();
                    for _i in 0..vec_len {
                        vecvals.push(t_vpi_vecval {
                            aval: 0,
                            bval: 0,
                        });
                    }
                    let vecvals_box = vecvals.into_boxed_slice();
                    let vecvals_ptr = vecvals_box.as_ptr() as *mut t_vpi_vecval;
                    let _ = Box::into_raw(vecvals_box);
                    (*value_p).value.vector = vecvals_ptr;
                }
                | vpiIntVal => {
                    (*value_p).value.integer = 0;
                }
                | vpiBinStrVal => {
                    let c_string = CString::new(signal_bit_string).expect("CString::new failed");
                    let c_str_ptr = c_string.into_raw();
                    (*value_p).value.str_ = c_str_ptr as *mut PLI_BYTE8;
                }
                | _ => {
                    todo!("v_format => {}", v_format)
                }
            };
        }
        | _ => panic!("{:#?}", signal_v),
    }

    // println!("[wellen_vpi_get_value] handle is {:?} format is {:?} value is {:?} signal_v is {:?}", handle, v_format, signal_bit_string, signal_v);
}

#[no_mangle]
pub unsafe extern "C" fn wellen_vpi_get_value(handle: *mut c_void, time: u64, value_p: p_vpi_value) {
    let time_table_idx = find_nearest_time_index(TIME_TABLE.as_ref().unwrap(), time);
    wellen_vpi_get_value_from_index(handle, time_table_idx as u64, value_p);
}

#[no_mangle]
pub unsafe extern "C" fn wellen_get_value_str(handle: *mut c_void, time_table_idx: u64) -> *mut c_char {
    let handle = unsafe { *{ handle as *mut vpiHandle } };
    let loaded_signal = SIGNAL_CACHE.as_ref().unwrap().get(&(handle as vpiHandle)).unwrap().signal.borrow();
    let off = loaded_signal.get_offset(time_table_idx as u32).unwrap();
    let signal_bit_string = loaded_signal.get_value_at(&off, 0).to_bit_string().unwrap();
    let c_string = CString::new(signal_bit_string).expect("CString::new failed");
    c_string.into_raw()
}

#[no_mangle]
pub unsafe extern "C" fn wellen_vpi_get(property: PLI_INT32, handle: *mut c_void) -> PLI_INT32 {
    let handle = unsafe { *{ handle as *mut vpiHandle } };
    let loaded_signal = SIGNAL_CACHE.as_ref().unwrap().get(&(handle as vpiHandle)).unwrap().signal.borrow();
    let off = loaded_signal.get_offset(0).unwrap();
    let signal_v = loaded_signal.get_value_at(&off, 0);

    match property as u32 {
        | vpiSize => signal_v.bits().unwrap() as PLI_INT32,
        | _ => {
            todo!("property => {}", property)
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn wellen_vpi_get_str(property: PLI_INT32, handle: *mut c_void) -> *mut c_void {
    let handle = unsafe { *{ handle as *mut vpiHandle } };
    let var_type = SIGNAL_CACHE.as_ref().unwrap().get(&(handle as vpiHandle)).unwrap().var_type.borrow();

    let c_string = match property as u32 {
        | vpiType => {
            match var_type {
                | VarType::Reg => CString::new("vpiReg").unwrap(),
                | VarType::Wire => CString::new("vpiNet").unwrap(),
                | _ => {
                    todo!("{:#?}", var_type)
                } // TODO: vpiRegArray vpiNetArray vpiMemory
            }
        }
        | _ => {
            todo!("property => {}", property)
        }
    };

    c_string.into_raw() as *mut c_void
}

#[no_mangle]
pub unsafe extern "C" fn wellen_vpi_iterate(_type: PLI_INT32, refHandle: *mut c_void) -> *mut c_void {
    let hier = HIERARCHY.as_ref().unwrap();
    let scopes = hier.scopes();

    if refHandle.is_null() {
        match _type as u32 {
            | vpiModule => {
                let r = scopes.into_iter().find_map(|scope_ref| {
                    let scope = hier.get(scope_ref);
                    let full_name = scope.full_name(hier);
                    if scope.scope_type() == ScopeType::Module {
                        let scope_name = scope.name(hier);
                        if scope_name.starts_with("$") || scope_name.ends_with("_pkg") {
                            return None;
                        } else {
                            println!("{:#?} scope_ref => {:?} name => {} full_name => {}", scope, scope_ref, scope_name, full_name);
                            Some(scope_name)
                        }
                    } else {
                        None
                    }
                });
                println!("iterate name => {}", r.unwrap());
            }
            | _ => {
                panic!("type => {}", _type)
            }
        }

        panic!()
    } else {
        todo!()
    }
}

#[no_mangle]
pub extern "C" fn wellen_get_time_from_index(index: u64) -> u64 {
    let time_table = unsafe { TIME_TABLE.as_ref().unwrap() };
    time_table[index as usize]
}

#[no_mangle]
pub extern "C" fn wellen_get_index_from_time(time: u64) -> u64 {
    let time_table = unsafe { TIME_TABLE.as_ref().unwrap() };
    find_nearest_time_index(time_table, time) as u64
}

#[no_mangle]
pub unsafe extern "C" fn wellen_get_max_index() -> u64 {
    (TIME_TABLE.as_ref().unwrap().len() - 1) as u64
}

#[no_mangle]
pub unsafe extern "C" fn wellen_vpi_finalize() {
    println!("[wellen_vpi_finalize] ... ");

    if SIGNAL_REF_CACHE.as_ref().unwrap().len() >= SIGNAL_REF_COUNT_THRESHOLD {
        if HAS_NEWLY_ADD_SIGNAL_REF {
            println!("[wellen_vpi_finalize] save signal ref into cache file");

            if let Some(ref cache) = SIGNAL_REF_CACHE {
                let file: File = File::create(SIGNAL_REF_COUNT_FILE).unwrap();
                serde_yaml::to_writer(file, &cache.len()).unwrap();
            }

            if let Some(ref cache) = SIGNAL_REF_CACHE {
                let file = File::create(SIGNAL_REF_CACHE_FILE).unwrap();
                serde_yaml::to_writer(file, cache).unwrap();
            }

            if let Some(ref cache) = SIGNAL_CACHE {
                let file = File::create(SIGNAL_CACHE_FILE).unwrap();
                serde_yaml::to_writer(file, cache).unwrap();
            }
        } else {
            println!("[wellen_vpi_finalize] no newly added signal ref")
        }
    } else {
        println!("[wellen_vpi_finalize] signal ref count is too small, not save cache file! signal_ref_count: {} < threshold: {}", SIGNAL_REF_CACHE.as_ref().unwrap().len(), SIGNAL_REF_COUNT_THRESHOLD);
    }
}
