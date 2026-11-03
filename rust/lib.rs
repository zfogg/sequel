//! Safe Rust bindings to Sequel's C SQL engine.
//!
//! Analysis sanitizes SQL without executing it. Formatting and highlighting
//! preserve contents. Native results are copied into Rust-owned values before
//! native storage is released. See the repository README for release linking.
#![warn(missing_docs)]
#![doc = include_str!("README.md")]
#![doc(html_root_url = "https://zfogg.github.io/sequel/rust")]

mod analysis;
mod diagnostics;
#[allow(
    dead_code,
    non_camel_case_types,
    non_snake_case,
    non_upper_case_globals
)]
mod ffi;
mod highlighting;
mod text;

pub use analysis::*;
pub use diagnostics::*;
pub use highlighting::*;
pub use text::*;

use std::ffi::{CStr, CString};
use std::os::raw::c_char;

/// Maximum input and default output/memory budgets: one GiB.
pub const DEFAULT_MAX_BYTES: usize = 1024 * 1024 * 1024;

/// SQL lexical dialect. This does not select or connect to a database.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
#[repr(u32)]
pub enum Dialect {
    /// Conservative generic SQL.
    #[default]
    Generic = 0,
    /// SQLite quoting and parameters.
    Sqlite = 1,
    /// PostgreSQL quoting, operators and parameters.
    PostgreSql = 2,
    /// DuckDB SQL.
    DuckDb = 3,
    /// MySQL SQL.
    MySql = 4,
    /// ClickHouse typed parameters and identifiers.
    ClickHouse = 5,
}

/// Native operation status. Analysis also returns canonical structured details.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Status {
    /// Success.
    Ok,
    /// A required native argument was null.
    Null,
    /// Invalid input or options.
    InvalidArgument,
    /// Insufficient output capacity.
    Overflow,
    /// Allocation failed.
    NoMemory,
    /// A resource budget was exceeded.
    Limit,
    /// The caller cancelled work.
    Cancelled,
    /// A future native status, retained without inventing its meaning.
    Unknown(u32),
}
impl Status {
    pub(crate) fn from_native(code: ffi::sequel_status_t) -> Self {
        match code {
            0 => Self::Ok,
            1 => Self::Null,
            2 => Self::InvalidArgument,
            3 => Self::Overflow,
            4 => Self::NoMemory,
            5 => Self::Limit,
            6 => Self::Cancelled,
            other => Self::Unknown(other as u32),
        }
    }
}

/// Operation error. Messages contain canonical explanations, never input SQL.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Error {
    /// Native failure category.
    pub status: Status,
    /// Canonical source diagnostic when available.
    pub diagnostic: Option<Box<Diagnostic>>,
}
impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "Sequel {:?}", self.status)?;
        if let Some(d) = &self.diagnostic {
            write!(f, ": {}", d.message)?;
        }
        Ok(())
    }
}
impl std::error::Error for Error {}

pub(crate) fn check(code: ffi::sequel_status_t) -> Result<(), Error> {
    if code == 0 {
        Ok(())
    } else {
        Err(Error {
            status: Status::from_native(code),
            diagnostic: None,
        })
    }
}
pub(crate) fn cstring(value: &str) -> Result<CString, Error> {
    CString::new(value).map_err(|_| Error {
        status: Status::InvalidArgument,
        diagnostic: None,
    })
}
pub(crate) unsafe fn native_string(pointer: *const c_char) -> String {
    if pointer.is_null() {
        return String::new();
    }
    // The engine guarantees NUL-terminated UTF-8 for all owned/static strings.
    unsafe { CStr::from_ptr(pointer) }
        .to_str()
        .expect("Sequel returned invalid UTF-8")
        .to_owned()
}
pub(crate) unsafe fn native_slice<'a, T>(pointer: *const T, count: usize) -> &'a [T] {
    if count == 0 {
        return &[];
    }
    assert!(
        !pointer.is_null(),
        "Sequel returned a null array with a nonzero count"
    );
    unsafe { std::slice::from_raw_parts(pointer, count) }
}

#[cfg(test)]
mod tests;
