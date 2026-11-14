use crate::{check, ffi, Dialect, Error};

/// A copied lexical token, safe to retain after native parsing completes.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Token {
    /// Token category.
    pub kind: i32,
    /// Token flags.
    pub flags: u32,
    /// Byte range in the source.
    pub range: std::ops::Range<usize>,
    /// Keyword identity, when applicable.
    pub keyword_id: u32,
}

/// A recovered parser issue.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ParseIssue {
    /// Stable issue code.
    pub code: i32,
    /// Recovery strategy used by the parser.
    pub recovery: i32,
    /// Byte range associated with the issue.
    pub range: std::ops::Range<usize>,
    /// Zero-based statement index.
    pub statement_index: usize,
}

/// A parsed statement summary.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Statement {
    /// Byte range of the statement.
    pub range: std::ops::Range<usize>,
    /// Operation category.
    pub operation: i32,
}

/// A relation discovered in the query model.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Relation {
    /// Relation category.
    pub kind: i32,
    /// Byte range of the relation.
    pub range: std::ops::Range<usize>,
    /// Byte range of its name.
    pub name_range: std::ops::Range<usize>,
    /// Statement containing the relation.
    pub statement_index: usize,
}

/// A copied lexical, syntax, semantic, policy, or portability finding.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct LayeredDiagnostic {
    /// Producing layer.
    pub origin: i32,
    /// Stable diagnostic code.
    pub code: i32,
    /// Severity.
    pub severity: i32,
    /// Source byte range.
    pub range: std::ops::Range<usize>,
    /// Statement index, or `usize::MAX` when not statement-specific.
    pub statement_index: usize,
    /// Whether the issue represents incomplete input.
    pub incomplete: bool,
    /// Whether parsing recovered around the issue.
    pub recovered: bool,
}

/// Owned lossless syntax and semantic query documents.
pub struct Document {
    source: Vec<u8>,
    syntax: *mut ffi::sequel_syntax_document_t,
    query: *mut ffi::sequel_query_document_t,
    tokens: Vec<Token>,
    issues: Vec<ParseIssue>,
    statements: Vec<Statement>,
    relations: Vec<Relation>,
    diagnostics: Vec<LayeredDiagnostic>,
}

impl Document {
    /// Parse UTF-8 SQL into recovered syntax and query models.
    pub fn parse(sql: &str, dialect: Dialect) -> Result<Self, Error> {
        let source = sql.as_bytes().to_vec();
        let view = ffi::sequel_source_view_t {
            data: source.as_ptr() as *const _,
            length: source.len(),
        };
        let mut options = unsafe { std::mem::zeroed::<ffi::sequel_parse_options_t>() };
        unsafe { ffi::sequel_parse_options_init(&mut options) };
        options.dialect = dialect as _;
        let mut syntax = std::ptr::null_mut();
        check(unsafe { ffi::sequel_parse_sql(&view, &options, std::ptr::null(), &mut syntax) })?;
        let mut query = std::ptr::null_mut();
        if let Err(error) =
            check(unsafe { ffi::sequel_build_query_document(syntax, &options, &mut query) })
        {
            unsafe { ffi::sequel_syntax_document_free(syntax) };
            return Err(error);
        }
        let lexed = unsafe { ffi::sequel_syntax_lexed(syntax) };
        let mut token_count = 0;
        let native_tokens = unsafe { ffi::sequel_lexed_tokens(lexed, &mut token_count) };
        let tokens = unsafe { crate::native_slice(native_tokens, token_count) }
            .iter()
            .map(|token| Token {
                kind: token.kind,
                flags: token.flags,
                range: token.range.start..token.range.end,
                keyword_id: token.keyword_id,
            })
            .collect();
        let mut issue_count = 0;
        let native_issues = unsafe { ffi::sequel_syntax_issues(syntax, &mut issue_count) };
        let issues = unsafe { crate::native_slice(native_issues, issue_count) }
            .iter()
            .map(|issue| ParseIssue {
                code: issue.code,
                recovery: issue.recovery,
                range: issue.range.start..issue.range.end,
                statement_index: issue.statement_index,
            })
            .collect();
        let statements = unsafe {
            crate::native_slice(
                ffi::sequel_query_statements(query),
                ffi::sequel_query_statement_count(query),
            )
        }
        .iter()
        .map(|statement| Statement {
            range: statement.range.start..statement.range.end,
            operation: statement.operation,
        })
        .collect();
        let relations = unsafe {
            crate::native_slice(
                ffi::sequel_query_relations(query),
                ffi::sequel_query_relation_count(query),
            )
        }
        .iter()
        .map(|relation| Relation {
            kind: relation.kind,
            range: relation.range.start..relation.range.end,
            name_range: relation.name_range.start..relation.name_range.end,
            statement_index: relation.statement_index,
        })
        .collect();
        let mut diagnostics = Vec::new();
        let mut lists = [
            std::mem::MaybeUninit::<ffi::sequel_diagnostic_list_t>::zeroed(),
            std::mem::MaybeUninit::<ffi::sequel_diagnostic_list_t>::zeroed(),
            std::mem::MaybeUninit::<ffi::sequel_diagnostic_list_t>::zeroed(),
        ];
        let statuses = unsafe {
            [
                ffi::sequel_collect_lexical_diagnostics(lexed, lists[0].as_mut_ptr()),
                ffi::sequel_collect_syntax_diagnostics(syntax, lists[1].as_mut_ptr()),
                ffi::sequel_collect_query_diagnostics(query, lists[2].as_mut_ptr()),
            ]
        };
        if statuses.iter().any(|status| *status != 0) {
            unsafe {
                for list in &mut lists {
                    ffi::sequel_diagnostic_list_free(list.as_mut_ptr());
                }
                ffi::sequel_query_document_free(query);
                ffi::sequel_syntax_document_free(syntax);
            }
            return Err(Error {
                status: crate::Status::NoMemory,
                diagnostic: None,
            });
        }
        for list in &mut lists {
            let list = unsafe { list.assume_init_mut() };
            diagnostics.extend(
                unsafe { crate::native_slice(list.records, list.count) }
                    .iter()
                    .map(|record| LayeredDiagnostic {
                        origin: record.origin,
                        code: record.code,
                        severity: record.severity,
                        range: record.range.start..record.range.end,
                        statement_index: record.statement_index,
                        incomplete: record.incomplete != 0,
                        recovered: record.recovered != 0,
                    }),
            );
            unsafe { ffi::sequel_diagnostic_list_free(list) };
        }
        Ok(Self {
            source,
            syntax,
            query,
            tokens,
            issues,
            statements,
            relations,
            diagnostics,
        })
    }

    /// Original source bytes.
    pub fn source(&self) -> &[u8] {
        &self.source
    }
    /// Copied lexical tokens.
    pub fn tokens(&self) -> &[Token] {
        &self.tokens
    }
    /// Recovered syntax issues.
    pub fn issues(&self) -> &[ParseIssue] {
        &self.issues
    }
    /// Statement summaries.
    pub fn statements(&self) -> &[Statement] {
        &self.statements
    }
    /// Relation summaries.
    pub fn relations(&self) -> &[Relation] {
        &self.relations
    }
    /// Layered lexical, syntax, semantic, and policy diagnostics.
    pub fn diagnostics(&self) -> &[LayeredDiagnostic] {
        &self.diagnostics
    }
}

impl Drop for Document {
    fn drop(&mut self) {
        unsafe {
            ffi::sequel_query_document_free(self.query);
            ffi::sequel_syntax_document_free(self.syntax);
        }
    }
}

unsafe impl Send for Document {}
unsafe impl Sync for Document {}

#[cfg(test)]
mod tests {
    use super::Document;
    use crate::Dialect;

    #[test]
    fn parses_owned_document_models() {
        let document =
            Document::parse("select id from users where id = ?", Dialect::Generic).unwrap();
        assert_eq!(document.source(), b"select id from users where id = ?");
        assert!(!document.tokens().is_empty());
        assert_eq!(document.statements().len(), 1);
        assert_eq!(document.relations().len(), 1);
        assert!(document.issues().is_empty());
    }

    #[test]
    fn preserves_recovery_issues() {
        let document = Document::parse("select (", Dialect::Generic).unwrap();
        assert!(!document.issues().is_empty());
    }

    #[test]
    fn exposes_layered_query_diagnostics() {
        let document = Document::parse("select * from users; select 1", Dialect::Generic).unwrap();
        assert!(!document.diagnostics().is_empty());
    }
}
