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
        "lib",
        "lib/util/utf8.c",
        "lib/util/utf8.h",
        "include",
        "third_party",
    ] {
        println!("cargo:rerun-if-changed={directory}");
    }
    let mut build = cc::Build::new();
    build
        .files([
            "lib/engine.c",
            "lib/runtime.c",
            "lib/contracts.c",
            "lib/sha256.c",
            "lib/highlight.c",
            "lib/compact.c",
            "lib/api.c",
            "lib/access.c",
            "lib/visit/query.c",
            "lib/visit/syntax.c",
            "lib/parse_entry.c",
            "lib/reparse.c",
            "lib/analysis/core.c",
            "lib/analysis/document.c",
            "lib/analysis/bindings.c",
            "lib/analysis/diagnostics.c",
            "lib/analysis/fingerprint.c",
            "lib/analysis/format.c",
            "lib/analysis/layout.c",
            "lib/analysis/locations.c",
            "lib/analysis/maps.c",
            "lib/analysis/operation.c",
            "lib/analysis/lifecycle.c",
            "lib/analysis/literal.c",
            "lib/analysis/binding_token.c",
            "lib/analysis/text.c",
            "lib/analysis/policy.c",
            "lib/analysis/sanitize.c",
            "lib/analysis/statements.c",
            "lib/analysis/summary.c",
            "lib/arrays.c",
            "lib/bindings.c",
            "lib/statements.c",
            "lib/joins.c",
            "lib/expressions.c",
            "lib/cte.c",
            "lib/tokens.c",
            "lib/source.c",
            "lib/dialect.c",
            "lib/lexer.c",
            "lib/parser.c",
            "lib/query.c",
            "lib/diagnostics.c",
            "lib/serialization.c",
            "lib/formatter.c",
            "lib/format_buffer.c",
            "lib/format_rules.c",
            "lib/fingerprint.c",
            "lib/metadata.c",
            "lib/metadata_document.c",
            "lib/metadata_text.c",
            "lib/sanitizer.c",
            "lib/summary/entry.c",
            "lib/summary/document.c",
            "lib/summary/render.c",
            "lib/summary/render_query.c",
            "lib/summary/prepare.c",
            "lib/summary/cte.c",
            "lib/summary/routes.c",
            "lib/summary/special.c",
            "lib/summary/repair.c",
            "lib/summary/recovery.c",
            "lib/summary/normalize.c",
            "lib/summary/buffer.c",
            "lib/summary/options.c",
            "lib/summary/lexical.c",
            "lib/summary/operation.c",
            "lib/summary/clause.c",
            "lib/summary/body.c",
            "lib/summary/relation.c",
            "lib/summary/postprocess.c",
            "lib/summary/statement.c",
            "lib/summary/subquery.c",
            "lib/summary/limits.c",
            "lib/util/utf8.c",
            "third_party/utf8proc/utf8proc.c",
        ])
        .includes(["include", "third_party", "third_party/utf8proc"])
        .define("UTF8PROC_STATIC", "1")
        .std("c11")
        .flag_if_supported("/utf-8")
        .warnings(false)
        .compile("sequel");
}
