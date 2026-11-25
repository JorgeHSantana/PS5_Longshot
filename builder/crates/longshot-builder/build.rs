//! The sources a release carries (crate build_driver::sources): the pack `LONGSHOT_SOURCES` names, made by
//! `longshot-builder dev release sources`, embedded in the program; none when it is unset (a developer's build).

use std::path::PathBuf;

fn main() {
    println!("cargo:rerun-if-env-changed=LONGSHOT_SOURCES");
    let out = PathBuf::from(std::env::var("OUT_DIR").expect("cargo sets OUT_DIR")).join("sources.rs");
    let code = match std::env::var("LONGSHOT_SOURCES").ok().filter(|v| !v.trim().is_empty()) {
        Some(path) => {
            let path = std::fs::canonicalize(path.trim()).expect("LONGSHOT_SOURCES names a file");
            println!("cargo:rerun-if-changed={}", path.display());
            format!("/// The sources this release carries.\npub static SOURCES: &[u8] = include_bytes!({:?});\n", path)
        }
        None => "/// None: a developer's build.\npub static SOURCES: &[u8] = &[];\n".to_string(),
    };
    std::fs::write(&out, code).expect("OUT_DIR is writable");
}
