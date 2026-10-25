#include <sequel.h>
#include <string.h>
int main(void) {
  sequel_metadata_t metadata;
  sequel_options_t options = {.dialect = SEQUEL_POSTGRESQL};
  if (sequel_inspect("SELECT 'secret'", &options, &metadata) != SEQUEL_OK)
    return 1;
  return strcmp(metadata.text, "SELECT ?") != 0;
}
