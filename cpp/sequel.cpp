#include <sequel/sequel.hpp>

#include <array>
#include <cstring>
#include <stdexcept>

namespace sequel {
namespace {

sequel_dialect_t native_dialect(dialect value) { return static_cast<sequel_dialect_t>(value); }

error make_error(sequel_status_t code) { return {static_cast<status>(code), std::nullopt}; }

template <typename T> std::expected<T, error> fail(sequel_status_t code) { return std::unexpected(make_error(code)); }

template <typename F> std::expected<std::string, error> measure(F &&function) {
  std::size_t size = 0;
  auto code = function(nullptr, 0, &size);
  if (code != SEQUEL_ERR_OVERFLOW && code != SEQUEL_OK)
    return fail<std::string>(code);
  std::string output(size, '\0');
  code = function(output.data(), output.size() + 1, &size);
  if (code != SEQUEL_OK)
    return fail<std::string>(code);
  output.resize(size);
  return output;
}

} // namespace

std::string diagnostic::code_name() const { return sequel_diagnostic_code_name(native.code); }
std::string diagnostic::message() const { return native.message; }
std::string error::message() const { return detail ? detail->message() : "Sequel operation failed"; }

document::~document() {
  sequel_query_document_free(query_);
  sequel_syntax_document_free(syntax_);
}
document::document(document &&other) noexcept : syntax_(other.syntax_), query_(other.query_) {
  other.syntax_ = nullptr;
  other.query_ = nullptr;
}
document &document::operator=(document &&other) noexcept {
  if (this != &other) {
    sequel_query_document_free(query_);
    sequel_syntax_document_free(syntax_);
    syntax_ = other.syntax_;
    query_ = other.query_;
    other.syntax_ = nullptr;
    other.query_ = nullptr;
  }
  return *this;
}

std::size_t document::token_count() const noexcept {
  return syntax_ ? sequel_lexed_token_count(sequel_syntax_lexed(syntax_)) : 0;
}
std::size_t document::syntax_issue_count() const noexcept {
  return syntax_ ? sequel_syntax_issue_count(syntax_) : 0;
}
std::size_t document::statement_count() const noexcept { return query_ ? sequel_query_statement_count(query_) : 0; }
std::size_t document::relation_count() const noexcept { return query_ ? sequel_query_relation_count(query_) : 0; }

metadata::~metadata() { sequel_metadata_free(&native_); }
metadata::metadata(metadata &&other) noexcept
    : native_(other.native_), text_(other.text_), summary_(other.summary_), operation_(other.operation_),
      truncated_(other.truncated_), statement_count_(other.statement_count_) {
  other.native_ = {};
  other.text_ = other.summary_ = other.operation_ = {};
}
metadata &metadata::operator=(metadata &&other) noexcept {
  if (this != &other) {
    sequel_metadata_free(&native_);
    native_ = other.native_;
    text_ = other.text_;
    summary_ = other.summary_;
    operation_ = other.operation_;
    truncated_ = other.truncated_;
    statement_count_ = other.statement_count_;
    other.native_ = {};
  }
  return *this;
}

analysis::~analysis() { sequel_analysis_free(&native_); }
analysis::analysis(analysis &&other) noexcept
    : native_(other.native_), status_(other.status_), omission_reason_(other.omission_reason_), text_(other.text_),
      summary_(other.summary_), formatted_(other.formatted_), operation_(other.operation_),
      fingerprint_(other.fingerprint_) {
  other.native_ = {};
  other.status_ = other.omission_reason_ = other.text_ = other.summary_ = other.formatted_ = other.operation_ =
      other.fingerprint_ = {};
}
analysis &analysis::operator=(analysis &&other) noexcept {
  if (this != &other) {
    sequel_analysis_free(&native_);
    native_ = other.native_;
    status_ = other.status_;
    omission_reason_ = other.omission_reason_;
    text_ = other.text_;
    summary_ = other.summary_;
    formatted_ = other.formatted_;
    operation_ = other.operation_;
    fingerprint_ = other.fingerprint_;
    other.native_ = {};
  }
  return *this;
}

query_analysis::~query_analysis() {
  sequel_query_analysis_free(analysis_);
  sequel_query_document_free(query_);
  sequel_syntax_document_free(syntax_);
}

query_analysis::query_analysis(query_analysis &&other) noexcept
    : syntax_(other.syntax_), query_(other.query_), analysis_(other.analysis_), summary_(std::move(other.summary_)),
      sanitized_(std::move(other.sanitized_)), formatted_(std::move(other.formatted_)),
      fingerprint_(std::move(other.fingerprint_)) {
  other.syntax_ = nullptr;
  other.query_ = nullptr;
  other.analysis_ = nullptr;
}

query_analysis &query_analysis::operator=(query_analysis &&other) noexcept {
  if (this != &other) {
    sequel_query_analysis_free(analysis_);
    sequel_query_document_free(query_);
    sequel_syntax_document_free(syntax_);
    syntax_ = other.syntax_;
    query_ = other.query_;
    analysis_ = other.analysis_;
    summary_ = std::move(other.summary_);
    sanitized_ = std::move(other.sanitized_);
    formatted_ = std::move(other.formatted_);
    fingerprint_ = std::move(other.fingerprint_);
    other.syntax_ = nullptr;
    other.query_ = nullptr;
    other.analysis_ = nullptr;
  }
  return *this;
}

const sequel_diagnostic_list_t *query_analysis::diagnostics() const noexcept {
  return sequel_query_analysis_diagnostics(analysis_);
}

std::expected<metadata, error> metadata_for(std::string_view sql, sequel_metadata_options_t options) {
  metadata result;
  auto code = sequel_metadata_n(sql.data(), sql.size(), &options, &result.native_);
  if (code != SEQUEL_OK)
    return std::unexpected(make_error(code));
  result.text_ = result.native_.text ? result.native_.text : "";
  result.summary_ = result.native_.summary ? result.native_.summary : "";
  result.operation_ = result.native_.operation;
  result.truncated_ = result.native_.truncated;
  result.statement_count_ = result.native_.statement_count;
  return result;
}

std::expected<document, error> parse(std::string_view sql, dialect dialect_value) {
  sequel_parse_options_t options;
  sequel_parse_options_init(&options);
  options.dialect = native_dialect(dialect_value);
  sequel_source_view_t source{sql.data(), sql.size()};
  document result;
  auto code = sequel_parse_sql(&source, &options, nullptr, &result.syntax_);
  if (code != SEQUEL_OK)
    return std::unexpected(make_error(code));
  code = sequel_build_query_document(result.syntax_, &options, &result.query_);
  if (code != SEQUEL_OK)
    return std::unexpected(make_error(code));
  return result;
}

std::expected<analysis, error> analyze(std::string_view sql, analysis_options options) {
  sequel_analysis_options_t native{};
  native.dialect = native_dialect(options.dialect_value);
  native.max_input_bytes = options.max_input_bytes;
  native.max_output_bytes = options.max_output_bytes;
  native.max_memory_bytes = options.max_memory_bytes;
  native.max_work_units = options.max_work_units;
  native.max_query_text_bytes = options.max_query_text_bytes;
  native.omit_query_text = options.omit_query_text;
  native.format = options.format;
  native.keep_in_lists = options.keep_in_lists;
  native.omit_source_maps = options.omit_source_maps;
  native.omit_bind_descriptors = options.omit_bind_descriptors;
  native.include_lints = options.include_lints;
  analysis result;
  auto code = sequel_analyze_n(sql.data(), sql.size(), &native, &result.native_);
  if (code != SEQUEL_OK && code != SEQUEL_ERR_LIMIT && code != SEQUEL_ERR_CANCELLED)
    return std::unexpected(make_error(code));
  result.status_ = result.native_.status ? result.native_.status : "";
  result.omission_reason_ = result.native_.omission_reason ? result.native_.omission_reason : "";
  result.text_ = result.native_.text ? result.native_.text : "";
  result.summary_ = result.native_.summary ? result.native_.summary : "";
  result.formatted_ = result.native_.formatted ? result.native_.formatted : "";
  result.operation_ = result.native_.operation;
  result.fingerprint_ = result.native_.fingerprint;
  return result;
}

std::expected<query_analysis, error> analyze_query(std::string_view sql, analysis_options options) {
  query_analysis result;
  sequel_parse_options_t parse_options;
  sequel_parse_options_init(&parse_options);
  parse_options.dialect = native_dialect(options.dialect_value);
  parse_options.retain_comments = true;
  parse_options.tolerant = true;
  parse_options.recover_errors = true;
  sequel_source_view_t source{sql.data(), sql.size()};
  auto code = sequel_parse_sql(&source, &parse_options, nullptr, &result.syntax_);
  if (code != SEQUEL_OK)
    return std::unexpected(make_error(code));
  code = sequel_build_query_document(result.syntax_, &parse_options, &result.query_);
  if (code != SEQUEL_OK)
    return std::unexpected(make_error(code));
  sequel_query_analysis_options_t analysis_options;
  sequel_query_analysis_options_init(&analysis_options);
  analysis_options.include_lints = options.include_lints;
  analysis_options.include_summary = true;
  analysis_options.include_fingerprint = true;
  analysis_options.include_sanitized_sql = !options.omit_query_text;
  analysis_options.include_formatted_sql = options.format;
  code = sequel_analyze_document(result.syntax_, result.query_, &analysis_options, nullptr, &result.analysis_);
  if (code != SEQUEL_OK)
    return std::unexpected(make_error(code));
  const sequel_text_result_t *text = sequel_query_analysis_summary(result.analysis_);
  if (text)
    result.summary_.assign(text->data ? text->data : "", text->length);
  text = sequel_query_analysis_sanitized_sql(result.analysis_);
  if (text)
    result.sanitized_.assign(text->data ? text->data : "", text->length);
  text = sequel_query_analysis_formatted_sql(result.analysis_);
  if (text)
    result.formatted_.assign(text->data ? text->data : "", text->length);
  char fingerprint[SEQUEL_FINGERPRINT_LENGTH] = {0};
  if (sequel_query_analysis_fingerprint(result.analysis_, fingerprint) == SEQUEL_OK)
    result.fingerprint_ = fingerprint;
  return result;
}

std::expected<std::string, error> sanitize(std::string_view sql, dialect dialect_value, bool collapse,
                                           std::size_t limit) {
  std::size_t size = 0;
  bool truncated = false;
  auto capacity = limit == 0 ? sql.size() + 1 : limit + 1;
  std::string output(capacity, '\0');
  auto code = sequel_sanitize(sql.data(), native_dialect(dialect_value), collapse, limit, output.data(), output.size(),
                              &truncated);
  if (code != SEQUEL_OK)
    return fail<std::string>(code);
  output.resize(std::strlen(output.c_str()));
  return output;
}

std::expected<std::string, error> format(std::string_view sql, dialect dialect_value) {
  return measure([&](char *out, std::size_t capacity, std::size_t *length) {
    return sequel_format_ex(std::string(sql).c_str(), native_dialect(dialect_value), out, capacity, length, nullptr);
  });
}

std::expected<std::string, error> fingerprint(std::string_view sql, dialect dialect_value) {
  std::array<char, 96> output{};
  auto code = sequel_fingerprint(std::string(sql).c_str(), native_dialect(dialect_value), output.data());
  if (code != SEQUEL_OK)
    return fail<std::string>(code);
  return std::string(output.data());
}

} // namespace sequel
