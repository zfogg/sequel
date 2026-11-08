#pragma once

#include <sequel.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace sequel {

enum class status : unsigned {
  ok = SEQUEL_OK,
  null_input = SEQUEL_ERR_NULL,
  invalid_argument = SEQUEL_ERR_INVALID_ARGUMENT,
  overflow = SEQUEL_ERR_OVERFLOW,
  no_memory = SEQUEL_ERR_NOMEM,
  limit = SEQUEL_ERR_LIMIT,
  cancelled = SEQUEL_ERR_CANCELLED,
};

enum class dialect : unsigned {
  generic = SEQUEL_GENERIC,
  sqlite = SEQUEL_SQLITE,
  postgresql = SEQUEL_POSTGRESQL,
  duckdb = SEQUEL_DUCKDB,
  mysql = SEQUEL_MYSQL,
  clickhouse = SEQUEL_CLICKHOUSE,
};

struct diagnostic {
  sequel_diagnostic_t native{};

  [[nodiscard]] std::string code_name() const;
  [[nodiscard]] std::string message() const;
};

struct error {
  status code;
  std::optional<diagnostic> detail;

  [[nodiscard]] std::string message() const;
};

class metadata {
public:
  metadata() = default;
  ~metadata();
  metadata(const metadata &) = delete;
  metadata &operator=(const metadata &) = delete;
  metadata(metadata &&other) noexcept;
  metadata &operator=(metadata &&other) noexcept;

  [[nodiscard]] std::string_view text() const noexcept { return text_; }
  [[nodiscard]] std::string_view summary() const noexcept { return summary_; }
  [[nodiscard]] std::string_view operation() const noexcept { return operation_; }
  [[nodiscard]] bool truncated() const noexcept { return truncated_; }
  [[nodiscard]] std::size_t statement_count() const noexcept { return statement_count_; }

private:
  friend std::expected<metadata, error> inspect(std::string_view, sequel_options_t);
  sequel_metadata_t native_{};
  std::string_view text_;
  std::string_view summary_;
  std::string_view operation_;
  bool truncated_ = false;
  std::size_t statement_count_ = 0;
};

struct analysis_options {
  dialect dialect_value = dialect::generic;
  std::size_t max_input_bytes = 0;
  std::size_t max_output_bytes = 0;
  std::size_t max_memory_bytes = 0;
  std::size_t max_work_units = 0;
  std::size_t max_query_text_bytes = 0;
  bool omit_query_text = false;
  bool format = false;
  bool keep_in_lists = false;
  bool omit_source_maps = false;
  bool omit_bind_descriptors = false;
  bool include_lints = false;
};

class analysis {
public:
  analysis() = default;
  ~analysis();
  analysis(const analysis &) = delete;
  analysis &operator=(const analysis &) = delete;
  analysis(analysis &&other) noexcept;
  analysis &operator=(analysis &&other) noexcept;

  [[nodiscard]] std::string_view status_text() const noexcept { return status_; }
  [[nodiscard]] std::string_view omission_reason() const noexcept { return omission_reason_; }
  [[nodiscard]] std::string_view text() const noexcept { return text_; }
  [[nodiscard]] std::string_view summary() const noexcept { return summary_; }
  [[nodiscard]] std::string_view formatted() const noexcept { return formatted_; }
  [[nodiscard]] std::string_view operation() const noexcept { return operation_; }
  [[nodiscard]] std::string_view fingerprint() const noexcept { return fingerprint_; }
  [[nodiscard]] bool truncated() const noexcept { return native_.truncated; }
  [[nodiscard]] const sequel_analysis_t &native() const noexcept { return native_; }

private:
  friend std::expected<analysis, error> analyze(std::string_view, analysis_options);
  sequel_analysis_t native_{};
  std::string_view status_, omission_reason_, text_, summary_, formatted_, operation_, fingerprint_;
};

[[nodiscard]] std::expected<metadata, error> inspect(std::string_view sql, sequel_options_t options = {});

[[nodiscard]] std::expected<analysis, error> analyze(std::string_view sql, analysis_options options = {});

[[nodiscard]] std::expected<std::string, error> sanitize(std::string_view sql, dialect dialect_value = dialect::generic,
                                                         bool collapse_in_lists = false, std::size_t limit = 0);

[[nodiscard]] std::expected<std::string, error> format(std::string_view sql, dialect dialect_value = dialect::generic);

[[nodiscard]] std::expected<std::string, error> fingerprint(std::string_view sql,
                                                            dialect dialect_value = dialect::generic);

} // namespace sequel
