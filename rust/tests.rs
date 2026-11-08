use super::*;

#[test]
fn diagnose_all_returns_every_safe_boundary_issue() {
    let diagnostics = diagnose_all("SELECT ); SELECT ]", Dialect::PostgreSql);
    assert_eq!(
        diagnostics
            .iter()
            .map(|diagnostic| diagnostic.code_name().unwrap())
            .collect::<Vec<_>>(),
        ["unmatched_close", "unmatched_close"]
    );
}

#[test]
fn diagnose_all_clean_sql_is_empty() {
    assert!(diagnose_all("SELECT 1", Dialect::Sqlite).is_empty());
}

#[test]
#[cfg(feature = "abi-check")]
fn checked_in_ffi_matches_target_c_abi() {
    unsafe extern "C" {
        fn sequel_rust_layout(index: u32) -> usize;
    }
    let values = [
        std::mem::size_of::<ffi::sequel_diagnostic_t>(),
        std::mem::size_of::<ffi::sequel_analysis_options_t>(),
        std::mem::size_of::<ffi::sequel_analysis_t>(),
        std::mem::size_of::<ffi::sequel_source_map_t>(),
        std::mem::size_of::<ffi::sequel_bind_descriptor_t>(),
        std::mem::size_of::<ffi::sequel_metadata_t>(),
        std::mem::offset_of!(ffi::sequel_analysis_t, _failure_diagnostic),
        std::mem::size_of::<ffi::sequel_highlight_options_t>(),
    ];
    for (i, expected) in values.iter().enumerate() {
        assert_eq!(
            unsafe { sequel_rust_layout(i as u32) },
            *expected,
            "ABI layout {i}"
        );
    }
}

#[test]
fn analysis_redacts_and_owns_outputs() {
    for dialect in [
        Dialect::Generic,
        Dialect::Sqlite,
        Dialect::PostgreSql,
        Dialect::DuckDb,
        Dialect::MySql,
        Dialect::ClickHouse,
    ] {
        let input = String::from("SELECT 'PRIVATE_VALUE', 123 FROM widgets;");
        let r = analyze(
            &input,
            &AnalysisOptions {
                dialect,
                format: true,
                ..Default::default()
            },
        )
        .unwrap();
        drop(input);
        assert_eq!(r.status, "ok");
        assert_eq!(r.operation, "SELECT");
        assert_eq!(r.statements.len(), 1);
        assert!(!format!("{r:?}").contains("PRIVATE_VALUE"));
        assert!(r.fingerprint.starts_with("sequel:v1:"));
        assert!(r.formatted.contains('?'));
        assert!(r.memory_peak_bytes > 0);
        assert!(r.work_units > 0);
    }
}
#[test]
fn fingerprint_equivalence_and_bind_identity() {
    let d = Dialect::PostgreSql;
    assert_eq!(
        fingerprint("SELECT 1", d).unwrap(),
        fingerprint("-- x\n SELECT $1", d).unwrap()
    );
    assert_eq!(
        fingerprint("SELECT $1, $1", d).unwrap(),
        fingerprint("SELECT $3, $3", d).unwrap()
    );
    assert_ne!(
        fingerprint("SELECT $1, $1", d).unwrap(),
        fingerprint("SELECT $1, $2", d).unwrap()
    );
    assert_ne!(
        fingerprint("SELECT a", d).unwrap(),
        fingerprint("SELECT b", d).unwrap()
    );
    let r = analyze(
        "SELECT $3, $3; SELECT $1",
        &AnalysisOptions {
            dialect: d,
            ..Default::default()
        },
    )
    .unwrap();
    assert_eq!(r.binds.len(), 3);
    r.check_binds(0, 3).unwrap();
    r.check_binds(1, 1).unwrap();
    assert!(r.check_binds(0, 2).is_err());
}
#[test]
fn failures_are_owned_canonical_and_fail_closed() {
    for bytes in [
        b"SELECT 'PRIVATE_VALUE".as_slice(),
        b"SELECT \xff".as_slice(),
        b"SELECT\0PRIVATE_VALUE".as_slice(),
    ] {
        let r = analyze_bytes(bytes, &Default::default()).unwrap();
        assert_ne!(r.status, "ok");
        assert!(r.text.is_empty() && r.fingerprint.is_empty() && r.binds.is_empty());
        assert!(!r.diagnostics.is_empty());
        assert!(!format!("{r:?}").contains("PRIVATE_VALUE"));
        for d in &r.diagnostics {
            assert_eq!(d.canonicalize(1, bytes.len()).unwrap(), *d);
            assert!(d.canonicalize(999, bytes.len()).is_err());
        }
    }
    let r = analyze("SELECT ); SELECT ];", &Default::default()).unwrap();
    assert!(r.diagnostics.len() >= 2);
}
#[test]
fn native_failure_fallback_stays_valid() {
    // Tiny budgets exercise C's inline diagnostic fallback; its address must
    // remain stable until all fields are copied and the native owner is freed.
    for _ in 0..100 {
        let r = analyze(
            "SELECT 'PRIVATE_VALUE'",
            &AnalysisOptions {
                max_memory_bytes: 1,
                ..Default::default()
            },
        )
        .unwrap();
        assert_eq!(r.status, "limit");
        assert!(!r.diagnostics[0].message.is_empty());
    }
}
#[test]
fn budgets_cancellation_and_policy() {
    for options in [
        AnalysisOptions {
            max_input_bytes: 4,
            ..Default::default()
        },
        AnalysisOptions {
            max_output_bytes: 1,
            ..Default::default()
        },
        AnalysisOptions {
            max_work_units: 1,
            ..Default::default()
        },
    ] {
        let r = analyze("SELECT 'PRIVATE_VALUE'", &options).unwrap();
        assert_eq!(r.status, "limit");
        assert!(r.text.is_empty());
    }
    let cancelled = || true;
    let r = analyze(
        "SELECT 1",
        &AnalysisOptions {
            cancelled: Some(&cancelled),
            ..Default::default()
        },
    )
    .unwrap();
    assert_eq!(r.status, "cancelled");
    assert_eq!(r.original_length, 8);
    let panic_probe = || panic!("test callback panic");
    assert!(analyze(
        "SELECT 1",
        &AnalysisOptions {
            cancelled: Some(&panic_probe),
            ..Default::default()
        }
    )
    .is_err());
    let r = analyze(
        "SELECT 1",
        &AnalysisOptions {
            omit_query_text: true,
            ..Default::default()
        },
    )
    .unwrap();
    assert_eq!(r.omission_reason, "policy");
    assert!(r.text.is_empty());
    assert!(!r.fingerprint.is_empty());
}
#[test]
fn unicode_maps_and_utf16_boundaries() {
    let input = "SELECT \"😀é\", 'SECRET'";
    let r = analyze(
        input,
        &AnalysisOptions {
            format: true,
            ..Default::default()
        },
    )
    .unwrap();
    let start = input.find("😀é").unwrap();
    let end = start + "😀é".len();
    let first = r
        .original_to_sanitized
        .map_range(start..end, false)
        .unwrap();
    assert!(first.exact);
    assert_eq!(&r.text[first.range.clone()], "😀é");
    let second = r
        .sanitized_to_formatted
        .map_range(first.range, false)
        .unwrap();
    assert!(second.exact);
    assert_eq!(&r.formatted[second.range], "😀é");
    assert_eq!(utf8_to_utf16("😀é", 4).unwrap(), 2);
    assert_eq!(utf16_to_utf8("😀é", 2).unwrap(), 4);
    assert!(utf8_to_utf16("😀", 1).is_err());
    assert!(utf16_to_utf8("😀", 1).is_err());
    assert!(r
        .original_to_sanitized
        .map_range(0..usize::MAX, false)
        .is_err());
}
#[test]
fn selective_sanitization_and_formatting() {
    for sql in [
        "select(1)",
        "SELECT*FROM t WHERE x=1",
        "VALUES(1),(2)",
        "SELECT 1+2",
    ] {
        assert_eq!(
            sanitize(sql, Dialect::Generic).unwrap(),
            analyze(sql, &Default::default()).unwrap().text
        );
        compact(sql, Dialect::Generic).unwrap();
    }
    let sql = "SELECT 'value', $1 -- comment\nFROM things";
    let d = Dialect::PostgreSql;
    let comments = sanitize_with_options(
        sql,
        &SanitizeOptions {
            dialect: d,
            replace_literals: false,
            ..Default::default()
        },
    )
    .unwrap();
    assert!(comments.text.contains("'value'"));
    assert!(!comments.text.contains("comment"));
    let literals = sanitize_with_options(
        sql,
        &SanitizeOptions {
            dialect: d,
            remove_comments: false,
            ..Default::default()
        },
    )
    .unwrap();
    assert!(!literals.text.contains("value"));
    assert!(literals.text.contains("comment"));
    assert!(format(sql, d).unwrap().contains("'value'"));
    assert!(compact("SELECT   1", d).unwrap().contains("SELECT 1"));
    assert!(format("SELECT\0hidden", d).is_err());
    assert!(sanitize("SELECT 'bad", d).is_err());
    assert_eq!(
        summarize("SELECT * FROM things", d).unwrap(),
        "SELECT … FROM things"
    );
    assert_eq!(inspect("SELECT 1", d).unwrap().text, "SELECT ?");
}
#[test]
fn html_ansi_themes_overrides_and_incomplete() {
    let formatted = format("SELECT '<script>'", Dialect::Generic).unwrap();
    let html = highlight(&formatted, &Default::default()).unwrap();
    assert!(html.contains("&lt;script&gt;"));
    assert!(!html.contains("<script>"));
    let ansi = highlight(
        "SELECT $1",
        &HighlightOptions::default()
            .dialect(Dialect::PostgreSql)
            .mode(OutputMode::Ansi)
            .color("variable", "#123000")
            .unwrap(),
    )
    .unwrap();
    assert!(ansi.contains("\x1b["));
    assert!(ansi.ends_with("\x1b[0m"));
    assert!(HighlightOptions::default()
        .color("unknown", "#123000")
        .is_err());
    assert!(HighlightOptions::default()
        .color("variable", "not-a-color")
        .is_err());
    let dark = Palette::named("default-dark", Appearance::Dark).unwrap();
    let light = dark.with_appearance(Appearance::Light).unwrap();
    assert_ne!(dark, light);
    assert!(colorschemes().len() > 100);
    for name in colorschemes() {
        let p = Palette::named(&name, Appearance::Auto).unwrap();
        highlight(
            "SELECT 1",
            &HighlightOptions::default().palette(&p).unwrap(),
        )
        .unwrap();
    }
    assert!(highlight("SELECT 'unfinished", &Default::default()).is_err());
    let (text, d) = highlight_diagnosed(
        "SELECT 'unfinished",
        &HighlightOptions::default().tolerant(true),
    )
    .unwrap();
    assert!(text.contains("unfinished"));
    assert!(d.unwrap().incomplete);
}
#[test]
fn concurrency_and_large_input() {
    let handles: Vec<_> = (0..8)
        .map(|_| {
            std::thread::spawn(|| {
                let sql = format!("SELECT '{}'", "x".repeat(40000));
                for _ in 0..20 {
                    let r = analyze(&sql, &Default::default()).unwrap();
                    assert_eq!(r.status, "ok");
                    assert_eq!(r.text, "SELECT ?");
                }
            })
        })
        .collect();
    for handle in handles {
        handle.join().unwrap();
    }
    assert_eq!(DEFAULT_MAX_BYTES, 1 << 30);
}
