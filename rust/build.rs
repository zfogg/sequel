use std::{env, path::PathBuf};

fn main() {
    println!("cargo:rerun-if-changed=rust/abi.c");
    println!("cargo:rerun-if-changed=include/sequel.h");
    if env::var_os("CARGO_FEATURE_ABI_CHECK").is_some() {
        cc::Build::new()
            .file("rust/abi.c")
            .include("include")
            .std("c11")
            .compile("sequel_rust_abi");
    }
    println!("cargo:rerun-if-env-changed=SEQUEL_LIB_DIR");
    println!("cargo:rerun-if-env-changed=SEQUEL_LINK_KIND");
    if let Some(directory) = env::var_os("SEQUEL_LIB_DIR") {
        let kind = env::var("SEQUEL_LINK_KIND").unwrap_or_else(|_| "static".into());
        assert!(
            kind == "static" || kind == "dylib",
            "SEQUEL_LINK_KIND must be static or dylib"
        );
        println!(
            "cargo:rustc-link-search=native={}",
            PathBuf::from(directory).display()
        );
        println!("cargo:rustc-link-lib={kind}=sequel");
        if kind == "static" {
            println!("cargo:rustc-link-lib=static=sequel_utf8proc");
        }
        return;
    }
    for directory in [
        "src/sql",
        "src/utf8.c",
        "src/utf8.h",
        "include",
        "third_party",
    ] {
        println!("cargo:rerun-if-changed={directory}");
    }
    let mut build = cc::Build::new();
    build
        .files([
            "src/sql/main.c",
            "src/sql/source.c",
            "src/sql/dialect.c",
            "src/sql/lexer.c",
            "src/sql/parser.c",
            "src/sql/query.c",
            "src/sql/analysis_document.c",
            "src/sql/serialization.c",
            "src/sql/diagnostics_bridge.c",
            "src/utf8.c",
            "third_party/utf8proc.c",
        ])
        .includes(["include", "third_party"])
        .define("UTF8PROC_STATIC", "1")
        .std("c11")
        .flag_if_supported("/utf-8")
        .warnings(false)
        .compile("sequel");
}
