use crate::{check, cstring, ffi, native_string, text::render, Diagnostic, Dialect, Error};

/// Preserve a palette or choose its actual published appearance variant.
#[derive(Debug, Clone, Copy, Default)]
#[repr(u32)]
pub enum Appearance {
    /// Preserve the selected palette.
    #[default]
    Auto = 0,
    /// Select its published dark variant.
    Dark = 1,
    /// Select its published light variant.
    Light = 2,
}
/// Highlight output encoding.
#[derive(Debug, Clone, Copy, Default)]
#[repr(u32)]
pub enum OutputMode {
    /// Escaped HTML pre/code fragment with inline styles.
    #[default]
    Html = 0,
    /// Truecolor terminal ANSI with final reset.
    Ansi = 1,
}
/// Base16 base00 through base0F as 24-bit RGB colors.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Palette {
    /// Sixteen RGB values; values above 0xffffff are rejected when applied.
    pub base: [u32; 16],
}
impl Palette {
    /// Select a bundled palette and its actual published light/dark counterpart.
    pub fn named(name: &str, appearance: Appearance) -> Result<Self, Error> {
        let name = cstring(name)?;
        let mut out = std::mem::MaybeUninit::zeroed();
        check(unsafe {
            ffi::sequel_colorscheme_for_appearance(
                name.as_ptr(),
                appearance as ffi::sequel_appearance_t,
                out.as_mut_ptr(),
            )
        })?;
        Ok(Self {
            base: unsafe { out.assume_init() }.base,
        })
    }
    /// Parse legacy or current Base16 YAML. No network or filesystem access occurs.
    pub fn from_yaml(yaml: &str) -> Result<Self, Error> {
        let yaml = cstring(yaml)?;
        let mut out = std::mem::MaybeUninit::zeroed();
        check(unsafe { ffi::sequel_colorscheme_parse(yaml.as_ptr(), out.as_mut_ptr()) })?;
        Ok(Self {
            base: unsafe { out.assume_init() }.base,
        })
    }
    /// Choose a recognized palette's real appearance counterpart; never invert colors.
    pub fn with_appearance(&self, appearance: Appearance) -> Result<Self, Error> {
        let mut raw = ffi::sequel_colorscheme_t { base: self.base };
        check(unsafe {
            ffi::sequel_colorscheme_set_appearance(&mut raw, appearance as ffi::sequel_appearance_t)
        })?;
        Ok(Self { base: raw.base })
    }
}
/// Copy bundled Base16 palette names.
pub fn colorschemes() -> Vec<String> {
    (0..unsafe { ffi::sequel_colorscheme_count() })
        .map(|i| unsafe { native_string(ffi::sequel_colorscheme_name(i)) })
        .collect()
}
/// Highlight policy. Defaults to generic SQL, HTML, strict quotes, default-dark.
#[derive(Debug, Clone)]
pub struct HighlightOptions {
    raw: ffi::sequel_highlight_options_t,
}
impl Default for HighlightOptions {
    fn default() -> Self {
        let mut raw = unsafe { std::mem::zeroed() };
        unsafe { ffi::sequel_highlight_options_init(&mut raw) };
        Self { raw }
    }
}
impl HighlightOptions {
    /// Choose the SQL dialect.
    pub fn dialect(mut self, dialect: Dialect) -> Self {
        self.raw.dialect = dialect as ffi::sequel_dialect_t;
        self
    }
    /// Choose mutually exclusive HTML or ANSI output.
    pub fn mode(mut self, mode: OutputMode) -> Self {
        self.raw.mode = mode as ffi::sequel_highlight_mode_t;
        self
    }
    /// Permit unfinished strings/comments/identifiers/parameters for editors.
    pub fn tolerant(mut self, enabled: bool) -> Self {
        self.raw.tolerant = enabled;
        self
    }
    /// Apply a palette, resetting token-specific overrides.
    pub fn palette(mut self, palette: &Palette) -> Result<Self, Error> {
        check(unsafe {
            ffi::sequel_highlight_set_colorscheme(
                &mut self.raw,
                &ffi::sequel_colorscheme_t { base: palette.base },
            )
        })?;
        Ok(self)
    }
    /// Override a native token category, for example `variable`, with #RRGGBB.
    pub fn color(mut self, category: &str, hex: &str) -> Result<Self, Error> {
        let category = cstring(category)?;
        let hex = cstring(hex)?;
        check(unsafe {
            ffi::sequel_highlight_set_color(&mut self.raw, category.as_ptr(), hex.as_ptr())
        })?;
        Ok(self)
    }
}
/// Highlight SQL without formatting or redacting its contents.
pub fn highlight(sql: &str, options: &HighlightOptions) -> Result<String, Error> {
    Ok(highlight_diagnosed(sql, options)?.0)
}
/// Highlight and preserve an incomplete-input warning when tolerant mode succeeds.
pub fn highlight_diagnosed(
    sql: &str,
    options: &HighlightOptions,
) -> Result<(String, Option<Diagnostic>), Error> {
    let input = cstring(sql)?;
    render(|p, n, length, d| unsafe {
        ffi::sequel_highlight_ex(input.as_ptr(), &options.raw, p, n, length, d)
    })
}
