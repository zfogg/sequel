#include <sequel/sequel.hpp>

#include <cassert>

int main() {
  auto analyzed = sequel::analyze("SELECT * FROM users WHERE id = $1", {.dialect_value = sequel::dialect::postgresql});
  assert(analyzed);
  assert(!analyzed->operation().empty());
  assert(!analyzed->fingerprint().empty());

  auto formatted = sequel::format("SELECT 1", sequel::dialect::generic);
  assert(formatted);
  assert(!formatted->empty());
  return 0;
}
