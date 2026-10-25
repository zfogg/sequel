#ifndef GOTEL_EMBEDDED_H
#define GOTEL_EMBEDDED_H
/* Returns zero for an unknown host language. */
int embedded_language(const char *name, const char *path);
/* Returns a newly allocated complete source file, or NULL on a lexical error.
 */
char *format_embedded(const char *source, int language, int dialect);
#endif
