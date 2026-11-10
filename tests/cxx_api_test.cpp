#include <sequel/parser.h>
#include <sequel/query.h>
#include <sequel/source.h>
#include <sequel/sequel.hpp>

#include <cassert>
#include <string>
#include <utility>

static bool has_code(const sequel::analysis &value, const char *code) {
  for (std::size_t i = 0; i < value.native().diagnostic_count; ++i)
    if (value.native().diagnostics[i].code ==
        static_cast<sequel_diagnostic_code_t>(value.native().diagnostics[i].code))
      if (std::string(sequel_diagnostic_code_name(value.native().diagnostics[i].code)) == code)
        return true;
  return false;
}

int main() {
  auto document = sequel::parse("SELECT id FROM users WHERE id = $1", sequel::dialect::postgresql);
  assert(document);
  assert(document->token_count() > 0);
  assert(document->syntax_issue_count() == 0);
  assert(document->statement_count() == 1);
  assert(document->relation_count() == 1);

  auto analyzed = sequel::analyze("SELECT * FROM users WHERE id = $1", {.dialect_value = sequel::dialect::postgresql});
  assert(analyzed);
  assert(!analyzed->operation().empty());
  assert(!analyzed->fingerprint().empty());

  auto lints = sequel::analyze("UPDATE users SET name = 'x';", {.dialect_value = sequel::dialect::sqlite,
                                                                  .include_lints = true});
  assert(lints);
  assert(has_code(*lints, "unbounded_write"));
  assert(lints->native().diagnostics[0].severity == SEQUEL_WARNING);

  auto malformed = sequel::analyze("SELECT '", {.dialect_value = sequel::dialect::sqlite});
  assert(!malformed);
  assert(malformed.error().code == sequel::status::invalid_argument);

  auto suppressed = sequel::analyze("SELECT id FROM users", {.omit_query_text = true});
  assert(suppressed);
  assert(suppressed->text().empty());
  assert(suppressed->omission_reason() == "policy");

  auto inspected = sequel::inspect("SELECT id FROM users");
  assert(inspected);
  assert(!inspected->text().empty());
  auto moved = std::move(*inspected);
  assert(!moved.text().empty());

  auto sanitized = sequel::sanitize("SELECT 'secret' FROM users", sequel::dialect::sqlite);
  assert(sanitized);
  assert(sanitized->find("secret") == std::string::npos);
  assert(sanitized->find('?') != std::string::npos);

  auto formatted = sequel::format("SELECT 1", sequel::dialect::generic);
  assert(formatted);
  assert(!formatted->empty());

  auto digest = sequel::fingerprint("SELECT 1", sequel::dialect::generic);
  auto equivalent = sequel::fingerprint("SELECT 2", sequel::dialect::generic);
  assert(digest && equivalent && *digest == *equivalent);

  auto invalid = sequel::analyze("SELECT )", {.dialect_value = static_cast<sequel::dialect>(99)});
  assert(!invalid);
  assert(invalid.error().code == sequel::status::invalid_argument);
  return 0;
}
