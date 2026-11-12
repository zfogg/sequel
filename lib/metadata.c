#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "metadata.h"
#include "util/text.h"

void sequel_metadata_set_operation(const sequel_source_view_t *source, const sequel_statement_info_t *statements,
                                     size_t statement_count, char *operation, size_t operation_capacity) {
  if (!source || !statements || !operation || !operation_capacity || !statement_count)
    return;
  size_t first = 0;
  while (first < source->length && isspace((unsigned char)source->data[first]))
    ++first;
  if (first + 3 <= source->length && sequel_text_equal_ci(source->data + first, 3, "SET") &&
      (first + 3 == source->length || !isalpha((unsigned char)source->data[first + 3]))) {
    operation[0] = 0;
    return;
  }
  bool same_operation = true;
  for (size_t i = 1; i < statement_count; ++i)
    if (statements[i].operation != statements[0].operation) {
      same_operation = false;
      break;
    }
  if (!same_operation) {
    operation[0] = 0;
    return;
  }
  const char *name = statements[0].operation == SEQUEL_OPERATION_SELECT        ? "SELECT"
                     : statements[0].operation == SEQUEL_OPERATION_INSERT      ? "INSERT"
                     : statements[0].operation == SEQUEL_OPERATION_UPDATE      ? "UPDATE"
                     : statements[0].operation == SEQUEL_OPERATION_DELETE      ? "DELETE"
                     : statements[0].operation == SEQUEL_OPERATION_MERGE       ? "MERGE"
                     : statements[0].operation == SEQUEL_OPERATION_CREATE      ? "CREATE"
                     : statements[0].operation == SEQUEL_OPERATION_ALTER       ? "ALTER"
                     : statements[0].operation == SEQUEL_OPERATION_DROP        ? "DROP"
                     : statements[0].operation == SEQUEL_OPERATION_EXPLAIN     ? "EXPLAIN"
                     : statements[0].operation == SEQUEL_OPERATION_OPTIMIZE    ? "OPTIMIZE"
                     : statements[0].operation == SEQUEL_OPERATION_TRUNCATE    ? "TRUNCATE"
                     : statements[0].operation == SEQUEL_OPERATION_TRANSACTION ? "TRANSACTION"
                                                                               : "";
  if (statements[0].operation == SEQUEL_OPERATION_TRANSACTION) {
    size_t start = 0;
    while (start < source->length && isspace((unsigned char)source->data[start]))
      ++start;
    if (start + 5 <= source->length && sequel_text_equal_ci(source->data + start, 5, "BEGIN"))
      name = "BEGIN";
    else if (start + 6 <= source->length && sequel_text_equal_ci(source->data + start, 6, "COMMIT"))
      name = "COMMIT";
    else if (start + 8 <= source->length && sequel_text_equal_ci(source->data + start, 8, "ROLLBACK"))
      name = "ROLLBACK";
  }
  if (*name) {
    snprintf(operation, operation_capacity, "%s", name);
    return;
  }
  size_t start = 0;
  while (start < source->length && isspace((unsigned char)source->data[start]))
    ++start;
  size_t end = start;
  while (end < source->length && isalpha((unsigned char)source->data[end]))
    ++end;
  size_t amount = end - start;
  if (amount >= operation_capacity)
    amount = operation_capacity - 1;
  for (size_t i = 0; i < amount; ++i)
    operation[i] = (char)toupper((unsigned char)source->data[start + i]);
  operation[amount] = 0;
}
