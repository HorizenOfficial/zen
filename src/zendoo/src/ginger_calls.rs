use algebra::{
    curves::mnt4753::MNT4 as PairingCurve,
    fields::{mnt4753::Fr, PrimeField},
    BigInteger768, FromBytes, ToBytes,
};

use crate::BackwardTransfer;
use primitives::{
    crh::{FieldBasedHash, MNT4PoseidonHash as FieldHash},
    merkle_tree::field_based_mht::{
        FieldBasedMerkleHashTree, FieldBasedMerkleTreeConfig, FieldBasedMerkleTreePath,
    },
};
use proof_systems::groth16::{prepare_verifying_key, verifier::verify_proof, Proof, VerifyingKey};

use std::{fs::File, io::Result as IoResult, path::Path};
pub type Error = Box<dyn std::error::Error>;

pub type FieldElement = Fr;

pub const FIELD_SIZE: usize = 96; //Field size in bytes
pub const SCALAR_FIELD_SIZE: usize = FIELD_SIZE; // 96

pub const GROTH_PROOF_SIZE: usize = 771;

//*******************************Generic I/O functions**********************************************
// Note: Should decide if panicking or handling IO errors

pub fn deserialize_from_buffer<T: FromBytes>(buffer: &[u8]) -> IoResult<T> {
    T::read(buffer)
}

pub fn serialize_to_buffer<T: ToBytes>(to_write: &T, buffer: &mut [u8]) -> IoResult<()> {
    to_write.write(buffer)
}

pub fn read_from_file<T: FromBytes>(file_path: &Path) -> IoResult<T> {
	println!("{:?}", file_path); 
    let mut fs = File::open(file_path)?;
	println!("ginger_calls:  after File::open ");
    let t = T::read(&mut fs)?;
    Ok(t)
}

pub fn write_to_file<T: ToBytes>(to_write: &T, file_path: &str) -> IoResult<()> {
    let mut fs = File::create(file_path)?;
    to_write.write(&mut fs)?;
    Ok(())
}

//Will return error if buffer.len > FIELD_SIZE. If buffer.len < FIELD_SIZE, padding 0s will be added
pub fn read_field_element_from_buffer_with_padding(buffer: &[u8]) -> IoResult<FieldElement> {
    let buff_len = buffer.len();

    //Pad to reach field element size
    let mut new_buffer = vec![];
    new_buffer.extend_from_slice(buffer);
    for _ in buff_len..FIELD_SIZE {
        new_buffer.push(0u8)
    } //Add padding zeros to reach field size

    FieldElement::read(&new_buffer[..])
}

pub fn read_field_element_from_u64(num: u64) -> FieldElement {
    FieldElement::from_repr(BigInteger768::from(num))
}

//************************************Poseidon Hash function****************************************

pub fn compute_poseidon_hash(input: &[FieldElement]) -> Result<FieldElement, Error> {
    FieldHash::evaluate(input)
}

//*****************************Naive threshold sig circuit related functions************************
pub type SCProof = Proof<PairingCurve>;
pub type SCVk = VerifyingKey<PairingCurve>;

impl BackwardTransfer {
    pub fn to_field_element(&self) -> IoResult<FieldElement> {
        let mut buffer = vec![];
        self.pk_dest.write(&mut buffer)?;
        self.amount.write(&mut buffer)?;
        read_field_element_from_buffer_with_padding(buffer.as_slice())
    }
}

pub fn verify_sc_proof(
    end_epoch_mc_b_hash: &[u8; 32],
    prev_end_epoch_mc_b_hash: &[u8; 32],
    bt_list: &[BackwardTransfer],
    quality: u64,
    constant: Option<&FieldElement>,
    proofdata: Option<&FieldElement>,
    sc_proof: &SCProof,
    vk: &SCVk,
) -> Result<bool, Error> {
    //Read inputs as field elements
    let end_epoch_mc_b_hash = read_field_element_from_buffer_with_padding(end_epoch_mc_b_hash)?;
    let prev_end_epoch_mc_b_hash =
        read_field_element_from_buffer_with_padding(prev_end_epoch_mc_b_hash)?;
    let quality = read_field_element_from_u64(quality);
    let mut bt_as_fes = vec![];
    for bt in bt_list.iter() {
        let bt_as_fe = bt.to_field_element()?;
        bt_as_fes.push(bt_as_fe);
    }

    //Get Merkle Root of Backward Transfer list
    let bt_tree = new_ginger_merkle_tree(bt_as_fes.as_slice())?;
    let bt_root = get_ginger_merkle_root(&bt_tree);
    drop(bt_as_fes);
    drop(bt_tree);

    //Load vk from file
    let pvk = prepare_verifying_key(&vk);

    //Prepare public inputs
    let mut public_inputs = Vec::new();

    let wcert_sysdata_hash = compute_poseidon_hash(&[
        quality,
        bt_root,
        prev_end_epoch_mc_b_hash,
        end_epoch_mc_b_hash,
    ])?;

    if constant.is_some(){
        public_inputs.push(*(constant.unwrap()));
    }
    if proofdata.is_some(){
        public_inputs.push(*(proofdata.unwrap()));
    }
    public_inputs.push(wcert_sysdata_hash);
    //Verify proof
    let is_verified = verify_proof(&pvk, &sc_proof, public_inputs.as_slice())?;
    Ok(is_verified)
}

//************Merkle Tree functions******************

pub struct FieldBasedMerkleTreeParams;

impl FieldBasedMerkleTreeConfig for FieldBasedMerkleTreeParams {
    const HEIGHT: usize = 9;
    type H = FieldHash;
}

pub type GingerMerkleTree = FieldBasedMerkleHashTree<FieldBasedMerkleTreeParams>;
pub type GingerMerkleTreePath = FieldBasedMerkleTreePath<FieldBasedMerkleTreeParams>;

pub fn new_ginger_merkle_tree(leaves: &[FieldElement]) -> Result<GingerMerkleTree, Error> {
    GingerMerkleTree::new(leaves)
}

pub fn get_ginger_merkle_root(tree: &GingerMerkleTree) -> FieldElement {
    tree.root()
}

pub fn get_ginger_merkle_path(
    leaf: &FieldElement,
    leaf_index: usize,
    tree: &GingerMerkleTree,
) -> Result<GingerMerkleTreePath, Error> {
    tree.generate_proof(leaf_index, leaf)
}

pub fn verify_ginger_merkle_path(
    path: &GingerMerkleTreePath,
    merkle_root: &FieldElement,
    leaf: &FieldElement,
) -> Result<bool, Error> {
    path.verify(merkle_root, leaf)
}
