use crate::{check, ffi, native_slice, native_string, Diagnostic, Dialect, Error, Status};
use std::{
    cell::Cell,
    ops::Range,
    panic::{catch_unwind, AssertUnwindSafe},
};

/// Per-call analysis policy. Zero budgets preserve the native defaults (1 GiB,
/// except zero work units means unlimited). Callbacks must not re-enter analysis.
#[derive(Default)]
pub struct AnalysisOptions<'a> {
    /// SQL dialect.
    pub dialect: Dialect,
    /// Maximum input bytes, zero for the default.
    pub max_input_bytes: usize,
    /// Maximum native output string/array bytes.
    pub max_output_bytes: usize,
    /// Maximum live native allocation bytes; excludes Rust result copies.
    pub max_memory_bytes: usize,
    /// Maximum counted native work units.
    pub max_work_units: usize,
    /// Independent maximum display prefix bytes.
    pub max_query_text_bytes: usize,
    /// Omit query text, formatting and maps, retaining safe structural metadata.
    pub omit_query_text: bool,
    /// Format the sanitized text.
    pub format: bool,
    /// Retain every placeholder in IN lists instead of collapsing them.
    pub keep_in_lists: bool,
    /// Avoid generating source maps.
    pub omit_source_maps: bool,
    /// Avoid generating value-free bind metadata.
    pub omit_bind_descriptors: bool,
    /// Synchronous cancellation probe; return true to stop. Panics become errors.
    pub cancelled: Option<&'a dyn Fn() -> bool>,
}

/// A value-free bind occurrence, in original UTF-8 byte coordinates.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct BindDescriptor {
    /// One-based occurrence ordinal across the document.
    pub ordinal: usize,
    /// One-based slot; repeated markers share a slot.
    pub slot: usize,
    /// Zero-based statement index.
    pub statement_index: usize,
    /// Inclusive starting byte.
    pub offset: usize,
    /// Exclusive ending byte.
    pub end_offset: usize,
    /// Syntax only: zero positional, one numbered, two named, three typed.
    pub style: u32,
}
impl BindDescriptor {
    fn native(&self) -> ffi::sequel_bind_descriptor_t {
        ffi::sequel_bind_descriptor_t {
            ordinal: self.ordinal,
            slot: self.slot,
            statement_index: self.statement_index,
            offset: self.offset,
            end_offset: self.end_offset,
            style: self.style as _,
        }
    }
}

/// Numeric mapping segment; never contains original SQL.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MapSegment {
    /// Source start byte.
    pub from_start: usize,
    /// Source end byte.
    pub from_end: usize,
    /// Destination start byte.
    pub to_start: usize,
    /// Destination end byte.
    pub to_end: usize,
    /// Provenance: zero copy, one replace, two delete, three insert.
    pub kind: u32,
}

/// Owning numeric map between two UTF-8 documents.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SourceMap {
    /// Source length in bytes.
    pub from_length: usize,
    /// Destination length in bytes.
    pub to_length: usize,
    /// Complete monotone segments, or empty when omitted.
    pub segments: Vec<MapSegment>,
}

/// Mapped range and whether its contents were copied exactly.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MappedRange {
    /// End-exclusive byte range; deleted ranges become empty anchors.
    pub range: Range<usize>,
    /// False for replacements, deletions or inserted layout.
    pub exact: bool,
}
impl SourceMap {
    pub(crate) fn from_native(map: &ffi::sequel_source_map_t) -> Self {
        Self {
            from_length: map.from_length,
            to_length: map.to_length,
            segments: unsafe { native_slice(map.segments, map.count) }
                .iter()
                .map(|s| MapSegment {
                    from_start: s.from_start,
                    from_end: s.from_end,
                    to_start: s.to_start,
                    to_end: s.to_end,
                    kind: s.kind as _,
                })
                .collect(),
        }
    }
    /// Map a source range forward, or a destination range backward with reverse.
    /// Invalid maps/ranges are errors. Approximate results must not be shown as
    /// exact diagnostic underlines.
    pub fn map_range(&self, range: Range<usize>, reverse: bool) -> Result<MappedRange, Error> {
        let segments: Vec<_> = self
            .segments
            .iter()
            .map(|s| ffi::sequel_map_segment_t {
                from_start: s.from_start,
                from_end: s.from_end,
                to_start: s.to_start,
                to_end: s.to_end,
                kind: s.kind as _,
            })
            .collect();
        let map = ffi::sequel_source_map_t {
            from_length: self.from_length,
            to_length: self.to_length,
            segments: segments.as_ptr(),
            count: segments.len(),
        };
        let mut out = std::mem::MaybeUninit::zeroed();
        check(unsafe {
            ffi::sequel_source_map_range(&map, range.start, range.end, reverse, out.as_mut_ptr())
        })?;
        let out = unsafe { out.assume_init() };
        Ok(MappedRange {
            range: out.start..out.end,
            exact: out.exact,
        })
    }
}

/// Rust-owned unified result. No original SQL or parameter values are retained.
#[derive(Debug, Clone)]
pub struct Analysis {
    /// Wire schema version.
    pub schema_version: u32,
    /// Requested SQL dialect.
    pub dialect: Dialect,
    /// Native return status, including lexical/resource/cancellation failures.
    pub native_status: Status,
    /// Canonical status: ok, malformed, limit, cancelled, or error.
    pub status: String,
    /// Why text is absent, or empty when present.
    pub omission_reason: String,
    /// Sanitized UTF-8 query text.
    pub text: String,
    /// Safe structural summary.
    pub summary: String,
    /// Formatted sanitized text when requested/available.
    pub formatted: String,
    /// Uppercase operation, or empty for mixed/unknown operations.
    pub operation: String,
    /// Versioned dialect-specific shape fingerprint, absent on failed input.
    pub fingerprint: String,
    /// Text was truncated independently of full-input fingerprinting.
    pub truncated: bool,
    /// Supplied original UTF-8 length, including rejected bytes.
    pub original_length: usize,
    /// Canonical original-coordinate issues.
    pub diagnostics: Vec<Diagnostic>,
    /// Value-free bind descriptors, absent on failure or omission.
    pub binds: Vec<BindDescriptor>,
    /// Original nonempty statement byte ranges.
    pub statements: Vec<Range<usize>>,
    /// Original to sanitized text provenance.
    pub original_to_sanitized: SourceMap,
    /// Sanitized to formatted text provenance.
    pub sanitized_to_formatted: SourceMap,
    /// Peak native requested allocation bytes (not Rust copies).
    pub memory_peak_bytes: usize,
    /// Native counted work; units can change between versions.
    pub work_units: usize,
}
impl Analysis {
    /// Validate a statement's supplied arity without inspecting any values.
    pub fn check_binds(&self, statement_index: usize, supplied_slots: usize) -> Result<(), Error> {
        check_binds(&self.binds, statement_index, supplied_slots)
    }
}

/// Validate descriptor slots/arity; $3 requires three supplied slots.
pub fn check_binds(
    binds: &[BindDescriptor],
    statement_index: usize,
    supplied_slots: usize,
) -> Result<(), Error> {
    let raw: Vec<_> = binds.iter().map(BindDescriptor::native).collect();
    check(unsafe {
        ffi::sequel_check_binds(raw.as_ptr(), raw.len(), statement_index, supplied_slots)
    })
}

struct NativeAnalysis(Box<ffi::sequel_analysis_t>);
impl Drop for NativeAnalysis {
    fn drop(&mut self) {
        unsafe { ffi::sequel_analysis_free(self.0.as_mut()) };
    }
}
struct Cancellation<'a> {
    callback: &'a dyn Fn() -> bool,
    panicked: Cell<bool>,
}
unsafe extern "C" fn cancel(data: *mut std::ffi::c_void) -> bool {
    let state = unsafe { &*(data.cast::<Cancellation<'_>>()) };
    catch_unwind(AssertUnwindSafe(|| (state.callback)())).unwrap_or_else(|_| {
        state.panicked.set(true);
        true
    })
}

/// Parse once, redact literals, and derive diagnostics, fingerprint, maps and binds.
/// Malformed/resource/cancel failures return a fail-closed analysis, not partial
/// SQL. Only Rust binding errors (such as a panicking probe) return `Err`.
pub fn analyze(sql: &str, options: &AnalysisOptions<'_>) -> Result<Analysis, Error> {
    analyze_bytes(sql.as_bytes(), options)
}

/// Analyze explicit bytes, diagnosing invalid UTF-8 and embedded NULs safely.
pub fn analyze_bytes(sql: &[u8], options: &AnalysisOptions<'_>) -> Result<Analysis, Error> {
    let mut cancellation = options.cancelled.map(|callback| Cancellation {
        callback,
        panicked: Cell::new(false),
    });
    let native_options = ffi::sequel_analysis_options_t {
        dialect: options.dialect as ffi::sequel_dialect_t,
        max_input_bytes: options.max_input_bytes,
        max_output_bytes: options.max_output_bytes,
        max_memory_bytes: options.max_memory_bytes,
        max_work_units: options.max_work_units,
        max_query_text_bytes: options.max_query_text_bytes,
        omit_query_text: options.omit_query_text,
        format: options.format,
        keep_in_lists: options.keep_in_lists,
        omit_source_maps: options.omit_source_maps,
        omit_bind_descriptors: options.omit_bind_descriptors,
        cancelled: cancellation
            .as_ref()
            .map(|_| cancel as unsafe extern "C" fn(*mut std::ffi::c_void) -> bool),
        cancel_userdata: cancellation.as_mut().map_or(std::ptr::null_mut(), |s| {
            (s as *mut Cancellation<'_>).cast()
        }),
    };
    // Box before calling C: failure diagnostics can point inside this structure.
    // Moving a stack result afterward would invalidate that fallback pointer.
    let mut owner = NativeAnalysis(Box::new(unsafe { std::mem::zeroed() }));
    let status = unsafe {
        ffi::sequel_analyze_n(
            sql.as_ptr().cast(),
            sql.len(),
            &native_options,
            owner.0.as_mut(),
        )
    };
    if cancellation.as_ref().is_some_and(|c| c.panicked.get()) {
        return Err(Error {
            status: Status::Cancelled,
            diagnostic: None,
        });
    }
    let r = &owner.0;
    Ok(Analysis {
        schema_version: r.schema_version,
        dialect: options.dialect,
        native_status: Status::from_native(status),
        status: unsafe { native_string(r.status) },
        omission_reason: unsafe { native_string(r.omission_reason) },
        text: unsafe { native_string(r.text) },
        summary: unsafe { native_string(r.summary) },
        formatted: unsafe { native_string(r.formatted) },
        operation: unsafe { native_string(r.operation.as_ptr()) },
        fingerprint: unsafe { native_string(r.fingerprint.as_ptr()) },
        truncated: r.truncated,
        original_length: r.original_length,
        diagnostics: unsafe { native_slice(r.diagnostics, r.diagnostic_count) }
            .iter()
            .map(Diagnostic::from_native)
            .collect(),
        binds: unsafe { native_slice(r.binds, r.bind_count) }
            .iter()
            .map(|b| BindDescriptor {
                ordinal: b.ordinal,
                slot: b.slot,
                statement_index: b.statement_index,
                offset: b.offset,
                end_offset: b.end_offset,
                style: b.style as _,
            })
            .collect(),
        statements: unsafe { native_slice(r.statements, r.statement_count) }
            .iter()
            .map(|s| s.offset..s.end_offset)
            .collect(),
        original_to_sanitized: SourceMap::from_native(&r.original_to_sanitized),
        sanitized_to_formatted: SourceMap::from_native(&r.sanitized_to_formatted),
        memory_peak_bytes: r.memory_peak_bytes,
        work_units: r.work_units,
    })
}

/// Convert a UTF-8 byte boundary to UTF-16 units; reject split scalars.
pub fn utf8_to_utf16(text: &str, offset: usize) -> Result<usize, Error> {
    let mut out = 0;
    check(unsafe {
        ffi::sequel_utf8_to_utf16(text.as_ptr().cast(), text.len(), offset, &mut out)
    })?;
    Ok(out)
}
/// Convert UTF-16 units to a UTF-8 boundary; reject split surrogate pairs.
pub fn utf16_to_utf8(text: &str, units: usize) -> Result<usize, Error> {
    let mut out = 0;
    check(unsafe { ffi::sequel_utf16_to_utf8(text.as_ptr().cast(), text.len(), units, &mut out) })?;
    Ok(out)
}
