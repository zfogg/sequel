package sequel

/*
#cgo CFLAGS: -I${SRCDIR}/../include -I${SRCDIR}/../third_party
#include <sequel.h>
static size_t sequel_go_range_start(sequel_source_range_t range) { return range.start; }
static size_t sequel_go_range_end(sequel_source_range_t range) { return range.end; }
static size_t sequel_go_token_start(const sequel_token_t *token) { return token->range.start; }
static size_t sequel_go_token_end(const sequel_token_t *token) { return token->range.end; }
static size_t sequel_go_token_leading_start(const sequel_token_t *token) { return token->leading_trivia.start; }
static size_t sequel_go_token_leading_end(const sequel_token_t *token) { return token->leading_trivia.end; }
static size_t sequel_go_token_trailing_start(const sequel_token_t *token) { return token->trailing_trivia.start; }
static size_t sequel_go_token_trailing_end(const sequel_token_t *token) { return token->trailing_trivia.end; }
static size_t sequel_go_issue_start(const sequel_parse_issue_t *issue) { return issue->range.start; }
static size_t sequel_go_issue_end(const sequel_parse_issue_t *issue) { return issue->range.end; }
static size_t sequel_go_statement_start(const sequel_statement_info_t *statement) { return statement->range.start; }
static size_t sequel_go_statement_end(const sequel_statement_info_t *statement) { return statement->range.end; }
static size_t sequel_go_relation_start(const sequel_relation_info_t *relation) { return relation->range.start; }
static size_t sequel_go_relation_end(const sequel_relation_info_t *relation) { return relation->range.end; }
static size_t sequel_go_relation_name_start(const sequel_relation_info_t *relation) { return relation->name_range.start; }
static size_t sequel_go_relation_name_end(const sequel_relation_info_t *relation) { return relation->name_range.end; }
static size_t sequel_go_relation_alias_start(const sequel_relation_info_t *relation) { return relation->alias_range.start; }
static size_t sequel_go_relation_alias_end(const sequel_relation_info_t *relation) { return relation->alias_range.end; }
*/
import "C"

import (
	"fmt"
	"runtime"
	"unsafe"
)

// Token is a lossless lexical token. Ranges refer to UTF-8 byte offsets in the
// source passed to Parse.
type Token struct {
	Kind          int
	Flags         uint32
	Start         int
	End           int
	LeadingStart  int
	LeadingEnd    int
	TrailingStart int
	TrailingEnd   int
	KeywordID     uint32
	DialectID     uint32
}

// ParseIssue describes a recoverable structural problem in a syntax document.
type ParseIssue struct {
	Code      int
	Recovery  int
	Start     int
	End       int
	Statement int
}

// QueryDocument exposes normalized statement and relation metadata derived
// from the immutable syntax tree.
type QueryDocument struct {
	Statements []StatementInfo
	Relations  []RelationInfo
}

type StatementInfo struct {
	Start, End int
	Operation  int
	SyntaxNode int
	QueryBody  int
	Flags      uint32
}

type RelationInfo struct {
	Kind                 int
	Start, End           int
	NameStart, NameEnd   int
	AliasStart, AliasEnd int
	Statement            int
	SyntaxNode           int
	Parent               int
	SubqueryStatement    int
	Flags                uint32
}

const (
	SanitizeRemoveComments  = 1 << 0
	SanitizeReplaceLiterals = 1 << 1
)

type FingerprintMode int

const (
	FingerprintTokens FingerprintMode = iota
	FingerprintSyntax
	FingerprintQueryShape
)

// Document owns the native lexical, syntax and query documents. It is safe to
// retain after Parse returns and must be released with Close.
type Document struct {
	source string
	lexed  *C.sequel_lexed_document_t
	syntax *C.sequel_syntax_document_t
	query  *C.sequel_query_document_t
}

// Parse lexes, parses and indexes SQL using the shared native parser model.
func Parse(source string, dialect Dialect) (*Document, error) {
	options := C.sequel_parse_options_t{}
	C.sequel_parse_options_init(&options)
	options.dialect = nativeDialect(dialect)
	view := C.sequel_source_view_t{data: (*C.char)(unsafe.Pointer(unsafe.StringData(source))), length: C.size_t(len(source))}
	var syntax *C.sequel_syntax_document_t
	if status := C.sequel_parse_sql(&view, &options, nil, &syntax); status != C.SEQUEL_OK {
		return nil, fmt.Errorf("sequel parse failed with status %d", int(status))
	}
	query := (*C.sequel_query_document_t)(nil)
	if status := C.sequel_build_query_document(syntax, &options, &query); status != C.SEQUEL_OK {
		C.sequel_syntax_document_free(syntax)
		return nil, fmt.Errorf("sequel query indexing failed with status %d", int(status))
	}
	document := &Document{source: source, syntax: syntax, query: query}
	runtime.KeepAlive(source)
	return document, nil
}

// Close releases all native parser state. It is safe to call repeatedly.
func (d *Document) Close() {
	if d == nil {
		return
	}
	if d.query != nil {
		C.sequel_query_document_free(d.query)
		d.query = nil
	}
	if d.syntax != nil {
		C.sequel_syntax_document_free(d.syntax)
		d.syntax = nil
	}
	if d.lexed != nil {
		C.sequel_lexed_document_free(d.lexed)
		d.lexed = nil
	}
}

// Tokens returns a copied immutable token view.
func (d *Document) Tokens() []Token {
	if d == nil || d.syntax == nil {
		return nil
	}
	lexed := C.sequel_syntax_lexed(d.syntax)
	var count C.size_t
	ptr := C.sequel_lexed_tokens(lexed, &count)
	native := unsafe.Slice(ptr, int(count))
	out := make([]Token, len(native))
	for i, token := range native {
		out[i] = Token{Kind: int(token.kind), Flags: uint32(token.flags), Start: int(C.sequel_go_token_start(&native[i])), End: int(C.sequel_go_token_end(&native[i])), LeadingStart: int(C.sequel_go_token_leading_start(&native[i])), LeadingEnd: int(C.sequel_go_token_leading_end(&native[i])), TrailingStart: int(C.sequel_go_token_trailing_start(&native[i])), TrailingEnd: int(C.sequel_go_token_trailing_end(&native[i])), KeywordID: uint32(token.keyword_id), DialectID: uint32(token.dialect_id)}
	}
	return out
}

// Issues returns copied recovery issues from the syntax document.
func (d *Document) Issues() []ParseIssue {
	if d == nil || d.syntax == nil {
		return nil
	}
	var count C.size_t
	ptr := C.sequel_syntax_issues(d.syntax, &count)
	native := unsafe.Slice(ptr, int(count))
	out := make([]ParseIssue, len(native))
	for i, issue := range native {
		out[i] = ParseIssue{Code: int(issue.code), Recovery: int(issue.recovery), Start: int(C.sequel_go_issue_start(&native[i])), End: int(C.sequel_go_issue_end(&native[i])), Statement: int(issue.statement_index)}
	}
	return out
}

// Query copies normalized statement and relation records.
func (d *Document) Query() QueryDocument {
	if d == nil || d.query == nil {
		return QueryDocument{}
	}
	statementPtr := C.sequel_query_statements(d.query)
	statementCount := C.sequel_query_statement_count(d.query)
	relationPtr := C.sequel_query_relations(d.query)
	relationCount := C.sequel_query_relation_count(d.query)
	out := QueryDocument{Statements: make([]StatementInfo, int(statementCount)), Relations: make([]RelationInfo, int(relationCount))}
	for i, item := range unsafe.Slice(statementPtr, int(statementCount)) {
		out.Statements[i] = StatementInfo{Start: int(C.sequel_go_statement_start(&unsafe.Slice(statementPtr, int(statementCount))[i])), End: int(C.sequel_go_statement_end(&unsafe.Slice(statementPtr, int(statementCount))[i])), Operation: int(item.operation), SyntaxNode: int(item.syntax_node), QueryBody: int(item.query_body), Flags: uint32(item.flags)}
	}
	for i, item := range unsafe.Slice(relationPtr, int(relationCount)) {
		out.Relations[i] = RelationInfo{Kind: int(item.kind), Start: int(C.sequel_go_relation_start(&unsafe.Slice(relationPtr, int(relationCount))[i])), End: int(C.sequel_go_relation_end(&unsafe.Slice(relationPtr, int(relationCount))[i])), NameStart: int(C.sequel_go_relation_name_start(&unsafe.Slice(relationPtr, int(relationCount))[i])), NameEnd: int(C.sequel_go_relation_name_end(&unsafe.Slice(relationPtr, int(relationCount))[i])), AliasStart: int(C.sequel_go_relation_alias_start(&unsafe.Slice(relationPtr, int(relationCount))[i])), AliasEnd: int(C.sequel_go_relation_alias_end(&unsafe.Slice(relationPtr, int(relationCount))[i])), Statement: int(item.statement_index), SyntaxNode: int(item.syntax_node), Parent: int(item.parent_relation), SubqueryStatement: int(item.subquery_statement), Flags: uint32(item.flags)}
	}
	return out
}

// Sanitize applies token-based redaction to the owned document.
func (d *Document) Sanitize(flags uint) (string, error) {
	if d == nil || d.syntax == nil {
		return "", fmt.Errorf("nil parser document")
	}
	var result C.sequel_text_result_t
	lexed := C.sequel_syntax_lexed(d.syntax)
	status := C.sequel_sanitize_tokens(lexed, C.uint(flags), &result)
	if status != C.SEQUEL_OK {
		return "", fmt.Errorf("sequel sanitization failed with status %d", int(status))
	}
	defer C.sequel_text_result_free(&result)
	return C.GoStringN(result.data, C.int(result.length)), nil
}

// Summary renders normalized query metadata without rescanning source text.
func (d *Document) Summary() (string, error) {
	if d == nil || d.query == nil {
		return "", fmt.Errorf("nil parser document")
	}
	var result C.sequel_text_result_t
	status := C.sequel_summarize_query(d.query, nil, &result)
	if status != C.SEQUEL_OK {
		return "", fmt.Errorf("sequel summary failed with status %d", int(status))
	}
	defer C.sequel_text_result_free(&result)
	return C.GoStringN(result.data, C.int(result.length)), nil
}

// Fingerprint returns a mode-specific digest of the retained parser model.
func (d *Document) Fingerprint(mode FingerprintMode) (string, error) {
	if d == nil || d.syntax == nil || d.query == nil {
		return "", fmt.Errorf("nil parser document")
	}
	var output [C.SEQUEL_FINGERPRINT_LENGTH]C.char
	status := C.sequel_fingerprint_document(d.syntax, d.query, C.sequel_fingerprint_mode_t(mode), &output[0])
	if status != C.SEQUEL_OK {
		return "", fmt.Errorf("sequel fingerprint failed with status %d", int(status))
	}
	return C.GoString(&output[0]), nil
}
