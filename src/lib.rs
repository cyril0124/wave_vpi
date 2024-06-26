#![allow(non_upper_case_globals)]
#![allow(non_snake_case)]

use byteorder::{BigEndian, ByteOrder};
use bytesize::ByteSize;
use std::borrow::Borrow;
use std::collections::HashMap;
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_void};
use wellen::*;

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
    println!("The hierarchy takes up at least {} of memory.", ByteSize::b(hierarchy.size_in_memory() as u64));

    unsafe {
        TIME_TABLE = Some(body.time_table);
        HIERARCHY = Some(hierarchy);
        WAVE_SOURCE = Some(wave_source);

        println!("Time table size: {}", TIME_TABLE.clone().unwrap().len());
    }
}

// type vpiHandle = u32;
type vpiHandle = SignalRef;

#[derive(Debug)]
struct SignalInfo {
    pub signal: Signal,
    pub var_type: VarType,
}

// static mut SIGNAL_CACHE: Option<HashMap<vpiHandle, Signal>> = None;
static mut SIGNAL_CACHE: Option<HashMap<SignalRef, SignalInfo>> = None;

#[no_mangle]
pub unsafe extern "C" fn wellen_vpi_handle_by_name(name: *const c_char) -> *mut c_void {
    let name = unsafe {
        assert!(!name.is_null());
        CStr::from_ptr(name)
    }
    .to_str()
    .unwrap();

    let id = unsafe {
        // HIERARCHY.as_ref().unwrap().get_unique_signals_vars().iter().flatten().find_map(|var| {
        HIERARCHY.as_ref().unwrap().iter_vars().find_map(|var| {
            let signal_name = var.full_name(&HIERARCHY.as_ref().unwrap());
            if signal_name == name.to_string() {
                let ids = [var.signal_ref(); 1];
                let loaded = WAVE_SOURCE.as_mut().unwrap().load_signals(&ids, &HIERARCHY.as_ref().unwrap(), LOAD_OPTS.multi_thread);
                let (loaded_id, loaded_signal) = loaded.into_iter().next().unwrap();
                assert_eq!(loaded_id, ids[0]);

                if SIGNAL_CACHE.is_none() {
                    SIGNAL_CACHE = Some(HashMap::new());
                }
                // SIGNAL_CACHE.as_mut().unwrap().insert(loaded_id.get_raw() as vpiHandle, loaded_signal);
                SIGNAL_CACHE.as_mut().unwrap().insert(
                    loaded_id,
                    SignalInfo {
                        signal: loaded_signal,
                        var_type: var.var_type(),
                    },
                );

                // Some(loaded_id.get_raw() as vpiHandle)
                Some(loaded_id as vpiHandle)
            } else {
                None
            }
        })
    };
    assert!(id.is_some(), "[wellen_vpi_handle_by_name] cannot find vpiHandle => name:{}", name);
    // println!("[wellen_vpi_handle_by_name] find vpiHandle => name:{} id:{:?}", name, id);

    // println!("{:#?}", unsafe { SIGNAL_CACHE.as_ref().unwrap()});

    let value = Box::new(id.unwrap() as vpiHandle);
    Box::into_raw(value) as *mut c_void
}

#[no_mangle]
pub extern "C" fn wellen_vpi_release_handle(handle: *mut c_void) {
    todo!();
    if !handle.is_null() {
        unsafe {
            Box::from_raw(handle as *mut vpiHandle);
        }
    }
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
                    for i in 0..vec_len {
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
