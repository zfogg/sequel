#include "../lib/access.c"
#define malloc sequel_alloc
#define calloc sequel_calloc
#define realloc sequel_realloc
#define free sequel_dealloc
#define while(condition) while ((sequel_work_tick(1), (condition)))
#include "../lib/analysis/binding_token.c"
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef while
#define malloc sequel_alloc
#define calloc sequel_calloc
#define realloc sequel_realloc
#define free sequel_dealloc
#define while(condition) while ((sequel_work_tick(1), (condition)))
#include "../lib/analysis/bindings.c"
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef while
#define malloc sequel_alloc
#define calloc sequel_calloc
#define realloc sequel_realloc
#define free sequel_dealloc
#define while(condition) while ((sequel_work_tick(1), (condition)))
#include "../lib/analysis/core.c"
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef while
#define malloc sequel_alloc
#define calloc sequel_calloc
#define realloc sequel_realloc
#define free sequel_dealloc
#define while(condition) while ((sequel_work_tick(1), (condition)))
#include "../lib/analysis/diagnostics.c"
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef while
#define malloc sequel_alloc
#define calloc sequel_calloc
#define realloc sequel_realloc
#define free sequel_dealloc
#define while(condition) while ((sequel_work_tick(1), (condition)))
#include "../lib/analysis/document.c"
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef while
#define malloc sequel_alloc
#define calloc sequel_calloc
#define realloc sequel_realloc
#define free sequel_dealloc
#define while(condition) while ((sequel_work_tick(1), (condition)))
#include "../lib/analysis/fingerprint.c"
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef while
#define malloc sequel_alloc
#define calloc sequel_calloc
#define realloc sequel_realloc
#define free sequel_dealloc
#define while(condition) while ((sequel_work_tick(1), (condition)))
#include "../lib/analysis/format.c"
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef while
#define malloc sequel_alloc
#define calloc sequel_calloc
#define realloc sequel_realloc
#define free sequel_dealloc
#define while(condition) while ((sequel_work_tick(1), (condition)))
#include "../lib/analysis/layout.c"
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef while
#define malloc sequel_alloc
#define calloc sequel_calloc
#define realloc sequel_realloc
#define free sequel_dealloc
#define while(condition) while ((sequel_work_tick(1), (condition)))
#include "../lib/analysis/lifecycle.c"
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef while
#define malloc sequel_alloc
#define calloc sequel_calloc
#define realloc sequel_realloc
#define free sequel_dealloc
#define while(condition) while ((sequel_work_tick(1), (condition)))
#include "../lib/analysis/literal.c"
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef while
#define malloc sequel_alloc
#define calloc sequel_calloc
#define realloc sequel_realloc
#define free sequel_dealloc
#define while(condition) while ((sequel_work_tick(1), (condition)))
#include "../lib/analysis/locations.c"
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef while
#define malloc sequel_alloc
#define calloc sequel_calloc
#define realloc sequel_realloc
#define free sequel_dealloc
#define while(condition) while ((sequel_work_tick(1), (condition)))
#include "../lib/analysis/maps.c"
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef while
#define malloc sequel_alloc
#define calloc sequel_calloc
#define realloc sequel_realloc
#define free sequel_dealloc
#define while(condition) while ((sequel_work_tick(1), (condition)))
#include "../lib/analysis/operation.c"
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef while
#define malloc sequel_alloc
#define calloc sequel_calloc
#define realloc sequel_realloc
#define free sequel_dealloc
#define while(condition) while ((sequel_work_tick(1), (condition)))
#include "../lib/analysis/policy.c"
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef while
#define malloc sequel_alloc
#define calloc sequel_calloc
#define realloc sequel_realloc
#define free sequel_dealloc
#define while(condition) while ((sequel_work_tick(1), (condition)))
#include "../lib/analysis/sanitize.c"
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef while
#define malloc sequel_alloc
#define calloc sequel_calloc
#define realloc sequel_realloc
#define free sequel_dealloc
#define while(condition) while ((sequel_work_tick(1), (condition)))
#include "../lib/analysis/statements.c"
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef while
#define malloc sequel_alloc
#define calloc sequel_calloc
#define realloc sequel_realloc
#define free sequel_dealloc
#define while(condition) while ((sequel_work_tick(1), (condition)))
#include "../lib/analysis/summary.c"
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef while
#define malloc sequel_alloc
#define calloc sequel_calloc
#define realloc sequel_realloc
#define free sequel_dealloc
#define while(condition) while ((sequel_work_tick(1), (condition)))
#include "../lib/analysis/text.c"
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef while
#include "../lib/api.c"
#include "../lib/arrays.c"
#include "../lib/bindings.c"
#include "../lib/compact.c"
#include "../lib/contracts.c"
#include "../lib/cte.c"
#include "../lib/diagnostics/diagnose.c"
#include "../lib/diagnostics/diagnostic_catalog.c"
#include "../lib/diagnostics/diagnostic_report.c"
#include "../lib/diagnostics/diagnostics.c"
#include "../lib/dialect.c"
#include "../lib/expressions.c"
#include "../lib/fingerprint.c"
#include "../lib/format_buffer.c"
#include "../lib/format_rules.c"
#include "../lib/formatter.c"
#include "../lib/highlight.c"
#include "../lib/joins.c"
#include "../lib/lexer.c"
#include "../lib/metadata.c"
#include "../lib/metadata_document.c"
#include "../lib/metadata_text.c"
#include "../lib/parse_entry.c"
#include "../lib/parser.c"
#include "../lib/parser_accessors.c"
#include "../lib/query.c"
#include "../lib/reparse.c"
#include "../lib/runtime.c"
#include "../lib/sanitizer.c"
#include "../lib/serialization.c"
#include "../lib/sha256.c"
#include "../lib/source.c"
#include "../lib/statements.c"
#define summary_ellipsis summary_body_ellipsis
#include "../lib/summary/body.c"
#undef summary_ellipsis
#include "../lib/summary/buffer.c"
#include "../lib/summary/clause.c"
#define summary_ellipsis summary_cte_ellipsis
#include "../lib/summary/cte.c"
#undef summary_ellipsis
#include "../lib/summary/document.c"
#include "../lib/summary/entry.c"
#include "../lib/summary/lexical.c"
#include "../lib/summary/limits.c"
#define summary_ellipsis summary_normalize_ellipsis
#define ellipsis summary_normalize_ellipsis
#include "../lib/summary/normalize.c"
#undef summary_ellipsis
#undef ellipsis
#include "../lib/summary/operation.c"
#include "../lib/summary/options.c"
#include "../lib/summary/postprocess.c"
#include "../lib/summary/prepare.c"
#define summary_ellipsis summary_recovery_ellipsis
#include "../lib/summary/recovery.c"
#undef summary_ellipsis
#include "../lib/summary/relation.c"
#include "../lib/summary/render/clauses.c"
#include "../lib/summary/render/cte.c"
#include "../lib/summary/render/postprocess.c"
#define summary_ellipsis summary_render_ellipsis
#include "../lib/summary/render/query.c"
#undef summary_ellipsis
#include "../lib/summary/render/render.c"
#include "../lib/summary/render/repairs.c"
#include "../lib/summary/render/shape.c"
#include "../lib/summary/repair.c"
#include "../lib/summary/routes.c"
#define summary_ellipsis summary_special_ellipsis
#include "../lib/summary/special.c"
#undef summary_ellipsis
#define ellipsis summary_statement_ellipsis
#include "../lib/summary/statement.c"
#undef ellipsis
#define ellipsis summary_subquery_ellipsis
#include "../lib/summary/subquery.c"
#undef ellipsis
#define malloc sequel_alloc
#define calloc sequel_calloc
#define realloc sequel_realloc
#define free sequel_dealloc
#define while(condition) while ((sequel_work_tick(1), (condition)))
#include "../lib/tokens.c"
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef while
#include "../lib/visit/query.c"
#include "../lib/visit/syntax.c"
