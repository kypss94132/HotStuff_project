// -*- mode: rust; -*-
//
// This file is part of the course 720 project.
use std::vec::Vec;
use rand::rngs::OsRng;

use crate::GroupKey;
use crate::compute_message_hash;
use crate::generate_commitment_share_lists;
use crate::DistributedKeyGeneration;
use crate::Parameters;
use crate::Participant;
use crate::SignatureAggregator;
use crate::keygen::SecretKey;
use crate::precomputation::PublicCommitmentShareList;
use crate::precomputation::SecretCommitmentShareList;
use crate::signature::ThresholdSignature;


/// A simulation for t out n threshold signature, where primary is the array of indices of the t parties.
#[no_mangle]
pub unsafe extern "C" fn simulation(_n: u32, _t: u32, primary: &[u32], msg: &[u8]) {
    let (n, t) = (4, 3);

    // A precomputation should be done when a set of parties are fixed, the output includes:
    // 1) parameters for the group of parties
    // 2) the group key
    // 3) the secret keys for each party
    let (params, group_key, sk_vk) = precomputation(n, t);

    // Each party should create the commitment for their private key to prove their ownship of the
    // secret key without revealing any further information
    let commitment_vec = commitment_creation(n);

    // We add the specific context message for this signature procedures, using something
    // related to our course : )
    let context = b"CONTEXT STRING FOR THE COURSE 720 BLABLABLA";

    // The procedure of signature generation regarding the given message, where the primary determins which parties will participate
    // using an array of index.
    let (threshold_signature, message_hash) = signature_aggregation(
        params, 
        (group_key.clone(), sk_vk), 
        commitment_vec, 
        context, 
        msg,
        primary,
    );

    // The verification procedure
    verification(threshold_signature, &group_key, message_hash);
}

pub fn precomputation(_n: u32, _t: u32) -> (Parameters, GroupKey, Vec<SecretKey>) {
    let (n, t) = (4, 3);
    let params = Parameters { n, t };
    
    let (p1, p1coeffs) = Participant::new(&params, 1);
    let (p2, p2coeffs) = Participant::new(&params, 2);
    let (p3, p3coeffs) = Participant::new(&params, 3);
    let (p4, p4coeffs) = Participant::new(&params, 4);

    let mut p1_other_participants: Vec<Participant> = vec!(p2.clone(), p3.clone(), p4.clone());
    let p1_state = DistributedKeyGeneration::<_>::new(&params,
                                                        &p1.index,
                                                        &p1coeffs,
                                                        &mut p1_other_participants).unwrap();
    let p1_their_secret_shares = p1_state.their_secret_shares().unwrap();
    
    let mut p2_other_participants: Vec<Participant> = vec!(p1.clone(), p3.clone(), p4.clone());
    let p2_state = DistributedKeyGeneration::<>::new(&params,
                                                        &p2.index,
                                                        &p2coeffs,
                                                        &mut p2_other_participants).unwrap();
    let p2_their_secret_shares = p2_state.their_secret_shares().unwrap();

    let mut p3_other_participants: Vec<Participant> = vec!(p1.clone(), p2.clone(), p4.clone());
    let p3_state = DistributedKeyGeneration::<_>::new(&params,
                                                        &p3.index,
                                                        &p3coeffs,
                                                        &mut p3_other_participants).unwrap();
    let p3_their_secret_shares = p3_state.their_secret_shares().unwrap();

    let mut p4_other_participants: Vec<Participant> = vec!(p1.clone(), p2.clone(), p3.clone());
    let p4_state = DistributedKeyGeneration::<_>::new(&params,
                                                        &p4.index,
                                                        &p4coeffs,
                                                        &mut p4_other_participants).unwrap();
    let p4_their_secret_shares = p4_state.their_secret_shares().unwrap();

    let p1_my_secret_shares = vec!(p2_their_secret_shares[0].clone(), // XXX FIXME indexing
                                    p3_their_secret_shares[0].clone(),
                                    p4_their_secret_shares[0].clone());

    let p2_my_secret_shares = vec!(p1_their_secret_shares[0].clone(),
                                    p3_their_secret_shares[1].clone(),
                                    p4_their_secret_shares[1].clone());
    
    let p3_my_secret_shares = vec!(p1_their_secret_shares[1].clone(),
                                    p2_their_secret_shares[1].clone(),
                                    p4_their_secret_shares[2].clone());
    
    let p4_my_secret_shares = vec!(p1_their_secret_shares[2].clone(),
                                    p2_their_secret_shares[2].clone(),
                                    p3_their_secret_shares[2].clone());
    
    let p1_state = p1_state.to_round_two(p1_my_secret_shares).unwrap();
    let p2_state = p2_state.to_round_two(p2_my_secret_shares).unwrap();
    let p3_state = p3_state.to_round_two(p3_my_secret_shares).unwrap();
    let p4_state = p4_state.to_round_two(p4_my_secret_shares).unwrap();


    // group key is common for all parties, to be used for partial signatures.
    // pi_sk is private key for each parties, to be used for generating public key and partial signatures. 
    let (group_key, p1_sk) = p1_state.finish(p1.public_key().unwrap()).unwrap();
    let (_, p2_sk) = p2_state.finish(p2.public_key().unwrap()).unwrap();
    let (_, p3_sk) = p3_state.finish(p3.public_key().unwrap()).unwrap();
    let (_, p4_sk) = p4_state.finish(p4.public_key().unwrap()).unwrap();

    let sk_vec = vec!(p1_sk, p2_sk, p3_sk, p4_sk);

    (params, group_key, sk_vec)
}

pub fn commitment_creation(n: u32) -> Vec<(PublicCommitmentShareList, SecretCommitmentShareList)>{
    // For security reasons, we add the separation message to avoid repetition attack. 
    // For each siganure, we use fresh commitment shares for all parties.
    let mut commitment_vec = Vec::new();
    for i in 0..n {
        let (public_comshares, secret_comshares) = generate_commitment_share_lists(&mut OsRng, i + 1, 1);
        commitment_vec.push((public_comshares, secret_comshares));
    }

    commitment_vec
}

pub fn signature_aggregation(
    params: Parameters, 
    keys: (GroupKey, Vec<SecretKey>), 
    mut commitment_vec: Vec<(PublicCommitmentShareList, SecretCommitmentShareList)>, 
    context: &[u8], 
    msg: &[u8],
    primary: &[u32],
) -> (ThresholdSignature, [u8; 64]) {
        let (group_key, sk_vec) = keys;
        // The role of primary / aggregator can be assigned to any untrusted party, including the participants of the signature precedures.
        let mut aggregator = SignatureAggregator::new(params, group_key, &context[..], &msg[..]);

        // The aggregator determins which participants will give their partial signatures
        for i in primary.iter() {
            let idx = i + 1;
            let (public_comshares, _) = &commitment_vec[idx as usize - 1];
            let sk = &sk_vec[idx as usize - 1];
            aggregator.include_signer(idx, public_comshares.commitments[0], sk.into());
    
        }
    
        // the list of the participants chosen by the aggregator
        let signers = aggregator.get_signers();
        let message_hash = compute_message_hash(&context[..], &msg[..]);
        let mut partial_vec = Vec::new();
    
        for i in primary.iter() {
            let idx = i + 1;
            let (_, secret_comshares) = &mut commitment_vec[idx as usize -1];
            let sk = &sk_vec[idx as usize - 1];
            let partial = sk.sign(&message_hash, &group_key, secret_comshares, 0, signers).unwrap();
            partial_vec.push(partial);
        }
    
        // aggregate all the required partial signatures
        for partial in partial_vec.into_iter() {
            aggregator.include_partial_signature(partial);
        }
    
        let aggregator = aggregator.finalize().unwrap();
        let threshold_signature = aggregator.aggregate().unwrap();

        (threshold_signature, message_hash)
}

pub fn verification(threshold_sig: ThresholdSignature, group_key: &GroupKey, message_hash: [u8; 64]) -> Result<(), ()> {
    threshold_sig.verify(group_key, &message_hash)
}
mod test {
    use super::*;

    #[test]
    fn test_simulation() {
        let n: u32 = 4;
        let t: u32 = 3;
        let primary: &[u32] = &[2, 1, 0];  // only accept three indices out of {0, 1, 2, 3}
        let msg: &[u8] = b"Course 720 Project confidential information.";

        unsafe {
            simulation(n, t, primary, msg)
        }
    }
}