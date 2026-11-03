use crate::{
    analysis::{analyze, AnalysisOptions},
    cstring,
    diagnostics::diagnosed_error,
    ffi, Dialect, Error, Status, DEFAULT_MAX_BYTES,
};
use std::os::raw::c_char;

/// Select independent transformations. Retained literals/comments can contain secrets.
#[derive(Debug, Clone)]
pub struct SanitizeOptions {
    /// SQL dialect.
    pub dialect: Dialect,
    /// Remove comments.
    pub remove_comments: bool,
    /// Replace literals and bind markers with question marks.
    pub replace_literals: bool,
    /// Keep every IN-list placeholder.
    pub keep_in_lists: bool,
    /// Display byte limit; zero preserves the one-GiB default.
    pub max_bytes: usize,
}
impl Default for SanitizeOptions {
    fn default() -> Self {
        Self {
            dialect: Dialect::Generic,
            remove_comments: true,
            replace_literals: true,
            keep_in_lists: false,
            max_bytes: 0,
        }
    }
}
/// Sanitized output with explicit prefix truncation metadata.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Sanitized {
    /// UTF-8 output, according to the selected privacy flags.
    pub text: String,
    /// The independent display limit shortened the output.
    pub truncated: bool,
}
fn buffer(length: usize) -> Result<Vec<u8>, Error> {
    let mut output = Vec::new();
    output.try_reserve_exact(length).map_err(|_| Error {
        status: Status::NoMemory,
        diagnostic: None,
    })?;
    output.resize(length, 0);
    Ok(output)
}
/// Default sanitization removes comments and replaces literals/bind markers.
pub fn sanitize(sql: &str, dialect: Dialect) -> Result<String, Error> {
    Ok(sanitize_with_options(
        sql,
        &SanitizeOptions {
            dialect,
            ..Default::default()
        },
    )?
    .text)
}
/// Sanitize using independently selected flags and an explicit display limit.
pub fn sanitize_with_options(sql: &str, options: &SanitizeOptions) -> Result<Sanitized, Error> {
    if sql.len() > DEFAULT_MAX_BYTES || options.max_bytes > DEFAULT_MAX_BYTES {
        return Err(Error {
            status: Status::Limit,
            diagnostic: None,
        });
    }
    let input = cstring(sql)?;
    let limit = if options.max_bytes == 0 {
        DEFAULT_MAX_BYTES
    } else {
        options.max_bytes
    };
    let flags = u32::from(options.remove_comments) | (u32::from(options.replace_literals) << 1);
    let mut capacity = sql.len().min(limit) + 1;
    loop {
        let mut output = buffer(capacity)?;
        let mut truncated = false;
        let status = unsafe {
            ffi::sequel_sanitize_with_flags(
                input.as_ptr(),
                options.dialect as ffi::sequel_dialect_t,
                flags,
                !options.keep_in_lists,
                options.max_bytes,
                output.as_mut_ptr().cast(),
                output.len(),
                &mut truncated,
            )
        };
        if status == ffi::SEQUEL_ERR_OVERFLOW && capacity < limit + 1 {
            capacity = capacity.saturating_mul(2).min(limit + 1);
            continue;
        }
        if status != 0 {
            return Err(Error {
                status: Status::from_native(status),
                diagnostic: Some(Box::new(crate::diagnose(sql, options.dialect))),
            });
        }
        return Ok(Sanitized {
            text: unsafe { crate::native_string(output.as_ptr().cast()) },
            truncated,
        });
    }
}
pub(crate) fn render(
    mut call: impl FnMut(
        *mut c_char,
        usize,
        *mut usize,
        *mut ffi::sequel_diagnostic_t,
    ) -> ffi::sequel_status_t,
) -> Result<(String, Option<crate::Diagnostic>), Error> {
    let mut length = 0;
    let mut d = unsafe { std::mem::zeroed() };
    let status = call(std::ptr::null_mut(), 0, &mut length, &mut d);
    if status != 0 && status != 3 {
        return Err(diagnosed_error(status, &d));
    }
    if length > DEFAULT_MAX_BYTES {
        return Err(Error {
            status: Status::Limit,
            diagnostic: None,
        });
    }
    let mut output = buffer(length + 1)?;
    let status = call(
        output.as_mut_ptr().cast(),
        output.len(),
        &mut length,
        &mut d,
    );
    if status != 0 {
        return Err(diagnosed_error(status, &d));
    }
    let text = String::from_utf8(output[..length].to_vec()).expect("native UTF-8 output");
    Ok((
        text,
        (d.code != 0).then(|| crate::Diagnostic::from_native(&d)),
    ))
}
/// Layout-only formatting. Literal and comment contents are preserved.
pub fn format(sql: &str, dialect: Dialect) -> Result<String, Error> {
    let input = cstring(sql)?;
    Ok(render(|p, n, len, d| unsafe {
        ffi::sequel_format_ex(
            input.as_ptr(),
            dialect as ffi::sequel_dialect_t,
            p,
            n,
            len,
            d,
        )
    })?
    .0)
}
/// Collapse horizontal token gaps while preserving contents and newlines.
pub fn compact(sql: &str, dialect: Dialect) -> Result<String, Error> {
    if sql.len() > DEFAULT_MAX_BYTES {
        return Err(Error {
            status: Status::Limit,
            diagnostic: None,
        });
    }
    let input = cstring(sql)?;
    let mut output = buffer(sql.len() + 1)?;
    crate::check(unsafe {
        ffi::sequel_compact(
            input.as_ptr(),
            dialect as ffi::sequel_dialect_t,
            output.as_mut_ptr().cast(),
            output.len(),
        )
    })?;
    Ok(unsafe { crate::native_string(output.as_ptr().cast()) })
}
/// Versioned, dialect-specific identity, excluding literal values and bind spelling.
pub fn fingerprint(sql: &str, dialect: Dialect) -> Result<String, Error> {
    let r = analyze(
        sql,
        &AnalysisOptions {
            dialect,
            omit_query_text: true,
            omit_source_maps: true,
            omit_bind_descriptors: true,
            ..Default::default()
        },
    )?;
    if r.native_status != Status::Ok {
        return Err(Error {
            status: r.native_status,
            diagnostic: r.diagnostics.first().cloned().map(Box::new),
        });
    }
    Ok(r.fingerprint)
}
/// Inspect safe text and structural metadata without generating maps/bind arrays.
pub fn inspect(sql: &str, dialect: Dialect) -> Result<crate::Analysis, Error> {
    analyze(
        sql,
        &AnalysisOptions {
            dialect,
            omit_source_maps: true,
            omit_bind_descriptors: true,
            ..Default::default()
        },
    )
}
/// Produce a safe structural summary; malformed SQL returns canonical diagnostics.
pub fn summarize(sql: &str, dialect: Dialect) -> Result<String, Error> {
    let r = analyze(
        sql,
        &AnalysisOptions {
            dialect,
            omit_query_text: true,
            omit_source_maps: true,
            omit_bind_descriptors: true,
            ..Default::default()
        },
    )?;
    if r.native_status != Status::Ok {
        return Err(Error {
            status: r.native_status,
            diagnostic: r.diagnostics.first().cloned().map(Box::new),
        });
    }
    Ok(r.summary)
}
