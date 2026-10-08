use crate::{
    abl_verify, build,
    build_tools::{ToolError, ToolResolver},
    graft, vbmeta_inspect, vendorboot,
};
use clap::{Parser, Subcommand};
use serde_json::{Value, json};
use std::{
    env, fs,
    path::{Path, PathBuf},
};

#[derive(Parser)]
#[command(
    name = "canoe-image",
    version = crate::version::VERSION,
    about = "Inspect and prepare Canoe images without accessing a device"
)]
pub struct Cli {
    #[arg(long, global = true)]
    json: bool,
    #[command(subcommand)]
    command: Command,
}

#[derive(Subcommand)]
enum Command {
    PrepareExplicit {
        #[arg(long)]
        abl: PathBuf,
        #[arg(long)]
        staged: PathBuf,
        #[arg(long)]
        system_version: u32,
        #[arg(long)]
        system_spl: u32,
        #[arg(long)]
        rot_digest: String,
        #[arg(long)]
        pubkey_digest: String,
        #[arg(long)]
        verified_boot_hash: String,
    },
    /// Derive a loader and sidecars, or probe an ABL without writing outputs.
    Build(build::BuildArgs),
    /// Check whether an ABL contains the supported vulnerable loader.
    AblCheck {
        #[arg(long)]
        image: PathBuf,
        #[arg(long)]
        tools: Option<PathBuf>,
        #[arg(long)]
        expected_sha256: Option<String>,
    },
    /// Inspect signing identity and version properties.
    Vbmeta {
        #[arg(long)]
        image: PathBuf,
        #[arg(long)]
        tools: Option<PathBuf>,
    },
    /// Check an image against the named chain in a vbmeta image.
    Check {
        #[arg(long)]
        image: PathBuf,
        #[arg(long)]
        vbmeta: PathBuf,
        #[arg(long)]
        partition: String,
        #[arg(long)]
        tools: Option<PathBuf>,
    },
    /// Extract a boot image's embedded verification data.
    Extract {
        #[arg(long)]
        image: PathBuf,
        #[arg(long)]
        output: PathBuf,
    },
    /// Add selected verification data to a boot image copy.
    Graft {
        #[arg(long)]
        vbmeta: PathBuf,
        #[arg(long)]
        image: PathBuf,
        #[arg(long)]
        output: PathBuf,
    },
    /// Prepare a vendor_boot image copy with the Canoe guard blacklist.
    VendorBoot {
        #[arg(long)]
        image: PathBuf,
        #[arg(long)]
        output: PathBuf,
    },
}

/// Standalone command lookup. An explicit directory is authoritative; it never
/// falls back to another installation when one of its helpers is missing.
struct CommandTools<'a>(Option<&'a Path>);
impl ToolResolver for CommandTools<'_> {
    fn resolve(&self, name: &str) -> Result<PathBuf, ToolError> {
        let find = |directory: &Path| {
            let path = directory.join(format!("{name}{}", env::consts::EXE_SUFFIX));
            let metadata = fs::metadata(&path).ok()?;
            if !metadata.is_file() {
                return None;
            }
            #[cfg(unix)]
            {
                use std::os::unix::fs::PermissionsExt;
                if metadata.permissions().mode() & 0o111 == 0 {
                    return None;
                }
            }
            Some(path)
        };
        let result = if let Some(directory) = self.0 {
            find(directory)
        } else {
            env::current_exe()
                .ok()
                .and_then(|p| p.parent().and_then(find))
                .or_else(|| {
                    env::var_os("PATH").and_then(|p| env::split_paths(&p).find_map(|d| find(&d)))
                })
        };
        result.ok_or_else(|| ToolError::Unavailable {
            tool: name.to_owned(),
        })
    }
}

impl Cli {
    fn execute(&self) -> Result<Value, Box<dyn std::error::Error>> {
        Ok(match &self.command {
            Command::PrepareExplicit { abl, staged, system_version, system_spl,
                rot_digest, pubkey_digest, verified_boot_hash } => {
                let digest = |value: &str| -> Result<[u8; 32], Box<dyn std::error::Error>> {
                    if value.len() != 64 || !value.bytes().all(|b| b.is_ascii_hexdigit()) {
                        return Err("profile digests must contain exactly 64 hexadecimal characters".into());
                    }
                    let mut result = [0; 32];
                    for (index, byte) in result.iter_mut().enumerate() {
                        *byte = u8::from_str_radix(&value[index * 2..index * 2 + 2], 16)?;
                    }
                    Ok(result)
                };
                let profile = mode2_profile::Profile {
                    magic: *b"GM2P", version: 1, reserved: 0,
                    is_unlocked: 0, color: 0,
                    system_version: *system_version, system_spl: *system_spl,
                    rot_digest: digest(rot_digest)?,
                    pubkey_digest: digest(pubkey_digest)?,
                    vbh: digest(verified_boot_hash)?,
                };
                let prepared = crate::loader::prepare_loader_with_profile(
                    &fs::read(abl)?, &profile.to_bytes(),
                    crate::loader::TzMapPolicy::ProtocolFallback)?;
                fs::create_dir(staged)?;
                fs::write(staged.join("boot.efi"), prepared.loader)?;
                fs::write(staged.join("boot.efi.gm2p"), prepared.gm2p)?;
                fs::write(staged.join("boot.efi.tzmap"), prepared.tzmap)?;
                json!({"staged": staged})
            }
            Command::Build(args) => {
                match build::execute(args, &CommandTools(args.tools.as_deref()))? {
                    build::BuildOutcome::Full(result) => serde_json::to_value(result)?,
                    build::BuildOutcome::Probe(result) => serde_json::to_value(result)?,
                }
            }
            Command::AblCheck {
                image,
                tools,
                expected_sha256,
            } => serde_json::to_value(abl_verify::verify(
                &abl_verify::AblVerifyRequest {
                    image: image.clone(),
                    expected_sha256: expected_sha256.clone(),
                },
                &CommandTools(tools.as_deref()),
            )?)?,
            Command::Vbmeta { image, tools } => {
                serde_json::to_value(vbmeta_inspect::inspect_header(
                    image,
                    &CommandTools(tools.as_deref()).resolve("mode2_profile")?,
                )?)?
            }
            Command::Check {
                image,
                vbmeta,
                partition,
                tools,
            } => serde_json::to_value(vbmeta_inspect::check(
                image,
                vbmeta,
                partition,
                &CommandTools(tools.as_deref()).resolve("mode2_profile")?,
            )?)?,
            Command::Extract { image, output } => {
                serde_json::to_value(graft::extract(image, output)?)?
            }
            Command::Graft {
                vbmeta,
                image,
                output,
            } => serde_json::to_value(graft::graft(vbmeta, image, output)?)?,
            Command::VendorBoot { image, output } => {
                serde_json::to_value(vendorboot::patch_cmdline(image, output)?)?
            }
        })
    }
    pub fn run(self) -> i32 {
        match self.execute() {
            Ok(value) => {
                if self.json {
                    println!("{}", json!({"ok": true, "result": value}));
                } else if let Some(output) = value.get("output").or_else(|| value.get("staged")) {
                    println!("Prepared {}", output.as_str().unwrap_or_default());
                } else {
                    println!(
                        "{}",
                        serde_json::to_string_pretty(&value).expect("serialize result")
                    );
                }
                0
            }
            Err(error) => {
                if self.json {
                    println!("{}", json!({"ok": false, "error": error.to_string()}));
                } else {
                    eprintln!("canoe-image: {error}");
                }
                1
            }
        }
    }
}
