use algebra::{FromBytes, ToBytes, UniformRand};
use libc::{c_uchar, c_uint};
use rand::rngs::OsRng;
use std::{
    io::{Error as IoError, ErrorKind},
    path::Path,
    ptr::null_mut,
    any::type_name,
    slice,
};

pub mod error;
use error::*;

pub mod ginger_calls;
use ginger_calls::*;

#[cfg(test)]
pub mod tests;

#[cfg(not(target_os = "windows"))]
use std::ffi::OsStr;
#[cfg(not(target_os = "windows"))]
use std::os::unix::ffi::OsStrExt;

#[cfg(target_os = "windows")]
use std::ffi::OsString;
#[cfg(target_os = "windows")]
use std::os::windows::ffi::OsStringExt;

// ***********UTILITY FUNCTIONS*************

fn read_raw_pointer<'a, T>(input: *const T) -> &'a T {
    assert!(!input.is_null());
    unsafe { &*input }
}

fn read_nullable_raw_pointer<'a, T>(input: *const T) -> Option<&'a T> {
    unsafe { input.as_ref() }
}

fn read_double_raw_pointer<T: Copy>(
    input: *const *const T,
    input_len: usize,
) -> Vec<T> {

    //Read *const T from *const *const T
    assert!(!input.is_null());
    let input_raw = unsafe { slice::from_raw_parts(input, input_len) };

    //Read T from *const T
    let mut input = vec![];
    for &ptr in input_raw.iter() {
        assert!(!ptr.is_null());
        input.push(unsafe { *ptr });
    }
    input
}

fn deserialize_to_raw_pointer<T: FromBytes>(buffer: &[u8]) -> *mut T {
    match deserialize_from_buffer(buffer) {
        Ok(t) => Box::into_raw(Box::new(t)),
        Err(_) => {
            let e = IoError::new(
                ErrorKind::InvalidData,
                format!("unable to read {} from buffer", type_name::<T>()),
            );
            set_last_error(Box::new(e), IO_ERROR);
            return null_mut();
        }
    }
}

fn serialize_from_raw_pointer<T: ToBytes>(
    to_write: *const T,
    buffer: &mut [u8],
) {
    serialize_to_buffer(read_raw_pointer(to_write), buffer)
        .expect(format!("unable to write {} to buffer", type_name::<T>()).as_str())
}

fn deserialize_from_file<T: FromBytes>(
    file_path: &Path,
) -> Option<T> {
    match read_from_file(file_path) {
        Ok(t) => Some(t),
        Err(e) => {
            let e = IoError::new(
                ErrorKind::InvalidData,
                format!(
                    "unable to deserialize {} from file: {}",
                    type_name::<T>(),
                    e.to_string()
                ),
            );
            set_last_error(Box::new(e), IO_ERROR);
            None
        }
    }
}

//***********Field functions****************
#[no_mangle]
pub extern "C" fn zendoo_get_field_size_in_bytes() -> c_uint {
    FIELD_SIZE as u32
}

#[no_mangle]
pub extern "C" fn zendoo_serialize_field(
    field_element: *const FieldElement,
    result: *mut [c_uchar; FIELD_SIZE],
){
    serialize_from_raw_pointer(
        field_element,
        &mut (unsafe { &mut *result })[..],
    )
}

#[no_mangle]
pub extern "C" fn zendoo_deserialize_field(
    field_bytes: *const [c_uchar; FIELD_SIZE],
) -> *mut FieldElement {
    deserialize_to_raw_pointer(&(unsafe { &*field_bytes })[..])
}

#[no_mangle]
pub extern "C" fn zendoo_field_free(field: *mut FieldElement) {
    if field.is_null() {
        return;
    }
    drop(unsafe { Box::from_raw(field) });
}

//********************Sidechain SNARK functions********************
#[repr(C)]
pub struct BackwardTransfer {
    pub pk_dest: [c_uchar; 32],
    pub amount: u64,
}

#[no_mangle]
pub extern "C" fn zendoo_get_sc_proof_size() -> c_uint {
    GROTH_PROOF_SIZE as u32
}

#[no_mangle]
pub extern "C" fn zendoo_serialize_sc_proof(
    sc_proof: *const SCProof,
    sc_proof_bytes: *mut [c_uchar; GROTH_PROOF_SIZE],
){
    serialize_from_raw_pointer(
        sc_proof,
        &mut (unsafe { &mut *sc_proof_bytes })[..],
    )
}

#[no_mangle]
pub extern "C" fn zendoo_deserialize_sc_proof(
    sc_proof_bytes: *const [c_uchar; GROTH_PROOF_SIZE],
) -> *mut SCProof {
    deserialize_to_raw_pointer(&(unsafe { &*sc_proof_bytes })[..])
}

#[no_mangle]
pub extern "C" fn zendoo_sc_proof_free(sc_proof: *mut SCProof) {
    if sc_proof.is_null() {
        return;
    }
    drop(unsafe { Box::from_raw(sc_proof) });
}

#[cfg(not(target_os = "windows"))]
#[no_mangle]
pub extern "C" fn zendoo_deserialize_sc_vk_from_file(
    vk_path: *const u8,
    vk_path_len: usize,
) -> *mut SCVk
{
    // Read file path
    let vk_path = Path::new(OsStr::from_bytes(unsafe {
        slice::from_raw_parts(vk_path, vk_path_len)
    }));

	println!("{:?}", vk_path); 


    match deserialize_from_file(vk_path){
        Some(vk) => Box::into_raw(Box::new(vk)),
        None => null_mut(),
    }
}

#[cfg(target_os = "windows")]
#[no_mangle]
pub extern "C" fn zendoo_deserialize_sc_vk_from_file(
    vk_path: *const u16,
    vk_path_len: usize,
) -> *mut SCVk
{
    // Read file path
    let path_str = OsString::from_wide(unsafe {
        slice::from_raw_parts(vk_path, vk_path_len)
    });
    let vk_path = Path::new(&path_str);

    match deserialize_from_file(vk_path){
        Some(vk) => Box::into_raw(Box::new(vk)),
        None => null_mut(),
    }
}

#[no_mangle]
pub extern "C" fn zendoo_sc_vk_free(sc_vk: *mut SCVk)
{
    if sc_vk.is_null()  { return }
    drop(unsafe { Box::from_raw(sc_vk) });
}


#[no_mangle]
pub extern "C" fn zendoo_verify_sc_proof(
    end_epoch_mc_b_hash: *const [c_uchar; 32],
    prev_end_epoch_mc_b_hash: *const [c_uchar; 32],
    bt_list: *const BackwardTransfer,
    bt_list_len: usize,
    quality: u64,
    constant: *const FieldElement,
    proofdata: *const FieldElement,
    sc_proof: *const SCProof,
    vk:       *const SCVk,
) -> bool {

	
    //Read end_epoch_mc_b_hash
    let end_epoch_mc_b_hash = read_raw_pointer(end_epoch_mc_b_hash);
	println!("{:?}", end_epoch_mc_b_hash);	
    
	//Read prev_end_epoch_mc_b_hash
    let prev_end_epoch_mc_b_hash = read_raw_pointer(prev_end_epoch_mc_b_hash);
	println!("{:?}", prev_end_epoch_mc_b_hash);	


    //Read bt_list
    let bt_list = unsafe { slice::from_raw_parts(bt_list, bt_list_len) };
	// println!("{:?}", bt_list);	


    //Read constant
    let constant = read_nullable_raw_pointer(constant);

    //Read proofdata
    let proofdata = read_nullable_raw_pointer(proofdata);

    //Read SCProof
    let sc_proof = read_raw_pointer(sc_proof);
	println!("{:?}", sc_proof);

    //Read vk from file
    let vk = read_raw_pointer(vk);
	println!("{:?}", vk);

    //Verify proof
    match ginger_calls::verify_sc_proof(
        end_epoch_mc_b_hash,
        prev_end_epoch_mc_b_hash,
        bt_list,
        quality,
        constant,
        proofdata,
        sc_proof,
        &vk,
    ) {
        Ok(result) => result,
        Err(e) => {
            set_last_error(e, CRYPTO_ERROR);
            false
        }
    }
}

//********************Poseidon hash functions********************

#[no_mangle]
pub extern "C" fn zendoo_compute_poseidon_hash(
    input: *const *const FieldElement,
    input_len: usize,
) -> *mut FieldElement {

    //Read message
    let message = read_double_raw_pointer(input, input_len);

    //Compute hash
    let hash = match compute_poseidon_hash(message.as_slice()) {
        Ok(hash) => hash,
        Err(e) => {
            set_last_error(e, CRYPTO_ERROR);
            return null_mut()
        }
    };

    //Return pointer to hash
    Box::into_raw(Box::new(hash))
}

// ********************Merkle Tree functions********************
#[no_mangle]
pub extern "C" fn ginger_mt_new(
    leaves: *const *const FieldElement,
    leaves_len: usize,
) -> *mut GingerMerkleTree {

    //Read leaves
    let leaves = read_double_raw_pointer(leaves, leaves_len);

    //Generate tree and compute Merkle Root
    let gmt = match new_ginger_merkle_tree(leaves.as_slice()) {
        Ok(tree) => tree,
        Err(e) => {
            set_last_error(e, CRYPTO_ERROR);
            return null_mut();
        }
    };

    Box::into_raw(Box::new(gmt))
}

#[no_mangle]
pub extern "C" fn ginger_mt_get_root(tree: *const GingerMerkleTree) -> *mut FieldElement {
    Box::into_raw(Box::new(get_ginger_merkle_root(read_raw_pointer(tree))))
}

#[no_mangle]
pub extern "C" fn ginger_mt_get_merkle_path(
    leaf: *const FieldElement,
    leaf_index: usize,
    tree: *const GingerMerkleTree,
) -> *mut GingerMerkleTreePath {
    //Read tree
    let tree = read_raw_pointer(tree);
    //Read leaf
    let leaf = read_raw_pointer(leaf);

    //Compute Merkle Path
    let mp = match get_ginger_merkle_path(leaf, leaf_index, tree) {
        Ok(path) => path,
        Err(e) => {
            set_last_error(e, CRYPTO_ERROR);
            return null_mut();
        }
    };

    Box::into_raw(Box::new(mp))
}

#[no_mangle]
pub extern "C" fn ginger_mt_verify_merkle_path(
    leaf: *const FieldElement,
    merkle_root: *const FieldElement,
    path: *const GingerMerkleTreePath,
) -> bool {

    //Read path
    let path = read_raw_pointer(path);

    //Read leaf
    let leaf = read_raw_pointer(leaf);

    //Read root
    let root = read_raw_pointer(merkle_root);

    // Verify leaf belonging
    match verify_ginger_merkle_path(path, root, leaf) {
        Ok(result) => result,
        Err(e) => {
            set_last_error(e, CRYPTO_ERROR);
            false
        }
    }
}

#[no_mangle]
pub extern "C" fn ginger_mt_free(tree: *mut GingerMerkleTree) {
    if tree.is_null() {
        return;
    }
    drop(unsafe { Box::from_raw(tree) });
}

#[no_mangle]
pub extern "C" fn ginger_mt_path_free(path: *mut GingerMerkleTreePath) {
    if path.is_null() {
        return;
    }
    drop(unsafe { Box::from_raw(path) });
}

//***************Test functions*******************

fn check_equal<T: Eq>(val_1: *const T, val_2: *const T) -> bool {
    let val_1 = unsafe { &*val_1 };
    let val_2 = unsafe { &*val_2 };
    val_1 == val_2
}

#[no_mangle]
pub extern "C" fn zendoo_get_random_field() -> *mut FieldElement {
    let mut rng = OsRng;
	let random_f = FieldElement::rand(&mut rng);
    Box::into_raw(Box::new(random_f))
}

#[no_mangle]
pub extern "C" fn zendoo_field_assert_eq(
    field_1: *const FieldElement,
    field_2: *const FieldElement,
) -> bool {
    check_equal(field_1, field_2)
}
