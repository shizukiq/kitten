#ifndef KITTEN_H
#define KITTEN_H

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>

#define KITTEN_VERSION "0.1.0"
#define DEFAULT_PREVIEW_LIMIT ((size_t)256 * 1024)

struct kitten_options {
	size_t preview_limit;
	long max_depth;
	int show_content;
	int raw_content;
	int dirs_only;
	int unicode_tree;
	int human_readable;
	int unsorted;
	int russian;
	char **exclude_patterns;
	size_t exclude_count;
};

struct kitten_totals {
	uintmax_t directories;
	uintmax_t files;
	uintmax_t symlinks;
	uintmax_t special;
	uintmax_t skipped;
	uintmax_t errors;
	uintmax_t bytes;
	int bytes_overflow;
};

struct kitten_context {
	const struct kitten_options *options;
	struct kitten_totals *totals;
};

int kitten_walk_path(struct kitten_context *context, const char *path,
    int is_last);
void kitten_print_summary(const struct kitten_context *context);
void kitten_print_escaped(FILE *out, const char *text);
void kitten_print_preview_text(const struct kitten_context *context, FILE *out,
    const char *text, size_t length);
const char *kitten_tree_branch(const struct kitten_context *context,
    int is_last);
const char *kitten_tree_stem(const struct kitten_context *context,
    int is_last);
void kitten_format_size(const struct kitten_context *context, uintmax_t bytes,
    char *buf, size_t size);

#endif
