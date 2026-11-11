#include <sequel.h>

int main(void) {
  const char sql[] = "SELECT Y',Y/ (a:/\n-+.@\";,\nY'0\n\\?[3-+Z3\\.1Zb2:/Z[+";
  sequel_options_t options = {.dialect = SEQUEL_POSTGRESQL};
  sequel_metadata_t metadata = {0};
  sequel_status_t status = sequel_inspect(sql, &options, &metadata);
  sequel_metadata_free(&metadata);
  (void)status;
  return 0;
}
