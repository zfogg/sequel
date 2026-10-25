#ifndef SEQUEL_EMBEDDED_H
#define SEQUEL_EMBEDDED_H
/* Returns zero for an unknown host language. */
int embedded_language(const char *name, const char *path);
/* Returns a newly allocated complete source file, or NULL on a lexical error.
 */
char *transform_embedded(const char *source, int language, int dialect, int sanitize, unsigned flags);
#endif
