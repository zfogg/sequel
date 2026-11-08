use crate::{check, ffi, native_string, Dialect, Error, Status};
use std::ops::Range;

/// Canonical diagnostic wire schema version.
pub const DIAGNOSTIC_VERSION: u32 = 1;

/// A lexical or delimiter diagnostic in original UTF-8 coordinates.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Diagnostic {
    /// Stable native catalog index; use `code_name` for its wire spelling.
    pub code: u32,
    /// Severity: zero note, one warning, two error.
    pub severity: u32,
    /// Zero-based, end-exclusive UTF-8 byte range.
    pub range: Range<usize>,
    /// One-based starting line.
    pub line: usize,
    /// One-based Unicode scalar starting column.
    pub column: usize,
    /// One-based ending line.
    pub end_line: usize,
    /// One-based Unicode scalar ending column.
    pub end_column: usize,
    /// Input can be completed interactively.
    pub incomplete: bool,
    /// Input ends in a complete statement boundary.
    pub statement_complete: bool,
    /// Short canonical message.
    pub message: String,
    /// Canonical explanation, without SQL values.
    pub explanation: String,
    /// Suggested next step.
    pub hint: String,
}
impl Diagnostic {
    /// Category used for diagnostic policy decisions.
    pub fn kind(&self) -> &'static str {
        match self.code_name() {
            Some("bind_missing" | "bind_extra" | "bind_invalid") => "bind",
            Some(
                "input_limit" | "output_limit" | "resource_limit" | "cancelled" | "out_of_memory",
            ) => "policy",
            Some("select_star" | "unbounded_write") => "safety",
            Some(
                "large_in_list" | "deep_query_nesting" | "excessive_joins" | "repeated_predicate",
            ) => "shape",
            Some("dialect_portability") => "portability",
            Some("empty_statement" | "multiple_statements" | "mixed_operations") => "structure",
            Some("unknown_operation" | "no_referenced_table" | "sqlc_annotation") => "metadata",
            _ => "lexical",
        }
    }
    /// Stable lowercase catalog code; unknown codes return `None`.
    pub fn code_name(&self) -> Option<&'static str> {
        let p = unsafe { ffi::sequel_diagnostic_code_name(self.code as _) };
        if p.is_null() {
            None
        } else {
            unsafe { std::ffi::CStr::from_ptr(p) }.to_str().ok()
        }
    }
    /// Lowercase severity suitable for diagnostic transport.
    pub fn severity_name(&self) -> &'static str {
        match self.severity {
            0 => "note",
            1 => "warning",
            2 => "error",
            _ => "unknown",
        }
    }
    /// Validate transported ranges/version and replace all prose with the catalog.
    /// This checks structure, not correspondence to SQL unavailable to the receiver.
    pub fn canonicalize(&self, version: u32, source_length: usize) -> Result<Self, Error> {
        let input = ffi::sequel_diagnostic_t {
            code: self.code as _,
            severity: self.severity as _,
            offset: self.range.start,
            end_offset: self.range.end,
            line: self.line,
            column: self.column,
            end_line: self.end_line,
            end_column: self.end_column,
            incomplete: self.incomplete,
            statement_complete: self.statement_complete,
            ..unsafe { std::mem::zeroed() }
        };
        let mut out = std::mem::MaybeUninit::zeroed();
        check(unsafe {
            ffi::sequel_diagnostic_canonicalize(version, source_length, &input, out.as_mut_ptr())
        })?;
        Ok(Self::from_native(&unsafe { out.assume_init() }))
    }
    pub(crate) fn from_native(d: &ffi::sequel_diagnostic_t) -> Self {
        Self {
            code: d.code as _,
            severity: d.severity as _,
            range: d.offset..d.end_offset,
            line: d.line,
            column: d.column,
            end_line: d.end_line,
            end_column: d.end_column,
            incomplete: d.incomplete,
            statement_complete: d.statement_complete,
            message: unsafe { native_string(d.message.as_ptr()) },
            explanation: unsafe { native_string(d.explanation.as_ptr()) },
            hint: unsafe { native_string(d.hint.as_ptr()) },
        }
    }
}

/// Copy the immutable native diagnostic catalog, including its `none` entry.
pub fn diagnostic_catalog() -> Vec<Diagnostic> {
    (0..unsafe { ffi::sequel_diagnostic_catalog_count() })
        .map(|index| {
            let mut out = std::mem::MaybeUninit::zeroed();
            check(unsafe { ffi::sequel_diagnostic_catalog_get(index, out.as_mut_ptr()) })
                .expect("valid catalog index");
            Diagnostic::from_native(&unsafe { out.assume_init() })
        })
        .collect()
}

/// Diagnose the first lexical/delimiter issue, without treating SQL as executable.
/// Valid input returns the catalog's `none` diagnostic with completion metadata.
pub fn diagnose(sql: &str, dialect: Dialect) -> Diagnostic {
    diagnose_bytes(sql.as_bytes(), dialect)
}

/// Diagnose explicit bytes, including invalid UTF-8 or embedded NULs.
pub fn diagnose_bytes(sql: &[u8], dialect: Dialect) -> Diagnostic {
    let mut d = std::mem::MaybeUninit::zeroed();
    unsafe {
        ffi::sequel_diagnose_n(
            sql.as_ptr().cast(),
            sql.len(),
            dialect as ffi::sequel_dialect_t,
            d.as_mut_ptr(),
        );
    }
    Diagnostic::from_native(&unsafe { d.assume_init() })
}

pub(crate) fn diagnosed_error(
    code: ffi::sequel_status_t,
    diagnostic: &ffi::sequel_diagnostic_t,
) -> Error {
    Error {
        status: Status::from_native(code),
        diagnostic: (diagnostic.code != 0).then(|| Box::new(Diagnostic::from_native(diagnostic))),
    }
}
