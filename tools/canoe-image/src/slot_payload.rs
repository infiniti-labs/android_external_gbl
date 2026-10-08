use std::fs;
use std::path::Path;
use sha2::{Digest, Sha256};

fn append_entry(out: &mut Vec<u8>, name: &str, data: &[u8], inode: u32) {
    let fields = [inode, 0o100644, 0, 0, 1, 0, data.len() as u32,
                  0, 0, 0, 0, name.len() as u32 + 1, 0];
    out.extend_from_slice(b"070701");
    for value in fields {
        out.extend_from_slice(format!("{value:08x}").as_bytes());
    }
    out.extend_from_slice(name.as_bytes());
    out.push(0);
    out.resize(out.len().next_multiple_of(4), 0);
    out.extend_from_slice(data);
    out.resize(out.len().next_multiple_of(4), 0);
}

pub fn pack(loader: &Path, profile: &Path, tzmap: &Path, output: &Path)
    -> Result<(), Box<dyn std::error::Error>> {
    let images = [fs::read(loader)?, fs::read(profile)?, fs::read(tzmap)?];
    if images[0].len() > 16 * 1024 * 1024 || abl_extract::pe_size(&images[0]) != Some(images[0].len()) {
        return Err("invalid ARM64 loader".into());
    }
    mode2_profile::Profile::decode(&images[1])?;
    abl_tzmap::manifest::TzMap::decode(&images[2])?;
    let mut payload = b"GBLSLOT1".to_vec();
    for value in [1, images[0].len() as u32, 120, 256] {
        payload.extend_from_slice(&value.to_le_bytes());
    }
    for image in &images {
        payload.extend_from_slice(&Sha256::digest(image));
    }
    for image in &images {
        payload.extend_from_slice(image);
    }
    let mut archive = Vec::new();
    append_entry(&mut archive, "gbl/abl-chainload.bin", &payload, 1);
    append_entry(&mut archive, "TRAILER!!!", &[], 2);
    archive.resize(archive.len().next_multiple_of(512), 0);
    fs::write(output, archive)?;
    Ok(())
}
