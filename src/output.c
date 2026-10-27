#define _POSIX_C_SOURCE 200809L

#include "kitten.h"

#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

const char *kitten_tree_branch(const struct kitten_context *context,
    int is_last)
{
	if (!context->options->unicode_tree)
		return is_last ? "`--" : "|--";
	return is_last ? "└──" : "├──";
}

const char *kitten_tree_stem(const struct kitten_context *context, int is_last)
{
	if (!context->options->unicode_tree)
		return is_last ? "    " : "|   ";
	return is_last ? "    " : "│   ";
}

static const char *byte_word(const struct kitten_context *context,
    uintmax_t bytes)
{
	uintmax_t last_two;
	uintmax_t last;

	if (!context->options->russian)
		return bytes == 1 ? "byte" : "bytes";
	last_two = bytes % 100;
	last = bytes % 10;
	if (last_two >= 11 && last_two <= 14)
		return "байт";
	if (last == 1)
		return "байт";
	if (last >= 2 && last <= 4)
		return "байта";
	return "байт";
}

void kitten_format_size(const struct kitten_context *context, uintmax_t bytes,
    char *buf, size_t size)
{
	static const char *units[] = { "KiB", "MiB", "GiB", "TiB", "PiB", "EiB" };
	uintmax_t scale = 1024;
	size_t unit = 0;

	if (!context->options->human_readable || bytes < 1024) {
		snprintf(buf, size, "%" PRIuMAX " %s", bytes,
		    byte_word(context, bytes));
		return;
	}

	while (unit + 1 < sizeof(units) / sizeof(units[0]) &&
	    bytes / scale >= 1024 && scale <= UINTMAX_MAX / 1024) {
		scale *= 1024;
		++unit;
	}

	if (bytes % scale == 0)
		snprintf(buf, size, "%" PRIuMAX " %s", bytes / scale, units[unit]);
	else
		snprintf(buf, size, "%.1f %s", (double)bytes / (double)scale,
		    units[unit]);
}

static void print_escaped_bytes(FILE *out, const char *text, size_t text_len,
    int preview)
{
	const char *p = text;
	const char *end = text + text_len;
	mbstate_t state;

	memset(&state, 0, sizeof(state));
	while (p < end) {
		unsigned char byte = (unsigned char)*p;
		wchar_t wc;
		size_t length;

		if (byte == '\t' && preview) {
			fputc('\t', out);
			++p;
			memset(&state, 0, sizeof(state));
		} else if (byte == '\n') {
			fputs("\\n", out);
			++p;
			memset(&state, 0, sizeof(state));
		} else if (byte == '\r') {
			fputs("\\r", out);
			++p;
			memset(&state, 0, sizeof(state));
		} else if (byte == '\t') {
			fputs("\\t", out);
			++p;
			memset(&state, 0, sizeof(state));
		} else if (byte == '\\' && !preview) {
			fputs("\\\\", out);
			++p;
			memset(&state, 0, sizeof(state));
		} else if (byte < 32 || byte == 127) {
			fprintf(out, "\\x%02x", (unsigned int)byte);
			++p;
			memset(&state, 0, sizeof(state));
		} else {
			length = mbrtowc(&wc, p, (size_t)(end - p), &state);
			if (length == (size_t)-1 || length == (size_t)-2) {
				fprintf(out, "\\x%02x", (unsigned int)byte);
				++p;
				memset(&state, 0, sizeof(state));
			} else if (length == 1 && byte >= 0x80 && byte <= 0x9f) {
				fprintf(out, "\\x%02x", (unsigned int)byte);
				p += length;
			} else if (iswcntrl((wint_t)wc)) {
				if ((uintmax_t)wc <= 0xffff)
					fprintf(out, "\\u%04" PRIxMAX, (uintmax_t)wc);
				else
					fprintf(out, "\\U%08" PRIxMAX, (uintmax_t)wc);
				p += length;
			} else {
				fwrite(p, 1, length, out);
				p += length;
			}
		}
	}
}

void kitten_print_escaped(FILE *out, const char *text)
{
	print_escaped_bytes(out, text, strlen(text), 0);
}

void kitten_print_preview_text(const struct kitten_context *context, FILE *out,
    const char *text, size_t length)
{
	if (context->options->raw_content)
		fwrite(text, 1, length, out);
	else
		print_escaped_bytes(out, text, length, 1);
}

void kitten_print_summary(const struct kitten_context *context)
{
	const struct kitten_options *options = context->options;
	const struct kitten_totals *totals = context->totals;
	char size[64];

	kitten_format_size(context, totals->bytes, size, sizeof(size));
	printf("\n%s:\n", options->russian ? "итого" : "summary");
	printf("  %s: %" PRIuMAX "\n", options->russian ? "каталоги" :
	    "directories", totals->directories);
	printf("  %s: %" PRIuMAX "\n", options->russian ? "файлы" : "files",
	    totals->files);
	printf("  %s: %" PRIuMAX "\n", options->russian ? "ссылки" : "symlinks",
	    totals->symlinks);
	printf("  %s: %" PRIuMAX "\n", options->russian ? "специальные" :
	    "special", totals->special);
	if (options->human_readable)
		printf("  %s: %s", options->russian ? "байты" : "bytes", size);
	else
		printf("  %s: %" PRIuMAX, options->russian ? "байты" : "bytes",
		    totals->bytes);
	if (totals->bytes_overflow)
		printf(" (%s)", options->russian ? "переполнение" : "overflow");
	putchar('\n');
	printf("  %s: %" PRIuMAX "\n", options->russian ? "пропущено" :
	    "skipped", totals->skipped);
	printf("  %s: %" PRIuMAX "\n", options->russian ? "ошибки" : "errors",
	    totals->errors);
}
