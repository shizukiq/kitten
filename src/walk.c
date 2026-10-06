#define _POSIX_C_SOURCE 200809L

#include "kitten.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static void add_file_size(struct kitten_context *context, off_t size)
{
	uintmax_t value;

	if (size <= 0)
		return;
	value = (uintmax_t)size;
	if (UINTMAX_MAX - context->totals->bytes < value) {
		context->totals->bytes = UINTMAX_MAX;
		context->totals->bytes_overflow = 1;
	} else {
		context->totals->bytes += value;
	}
}

static int is_excluded(const struct kitten_context *context, const char *path,
    const char *name)
{
	size_t i;
	const struct kitten_options *options = context->options;

	for (i = 0; i < options->exclude_count; ++i) {
		if (fnmatch(options->exclude_patterns[i], name, 0) == 0 ||
		    fnmatch(options->exclude_patterns[i], path, FNM_PATHNAME) == 0)
			return 1;
		if (path[0] == '.' && path[1] == '/' &&
		    fnmatch(options->exclude_patterns[i], path + 2, FNM_PATHNAME) == 0)
			return 1;
	}
	return 0;
}

static char *join_path(const char *left, const char *right)
{
	size_t left_len = strlen(left);
	size_t right_len = strlen(right);
	int need_slash = left_len > 0 && left[left_len - 1] != '/';
	size_t extra = need_slash ? 2 : 1;
	char *path;

	if (left_len > SIZE_MAX - right_len || left_len + right_len > SIZE_MAX - extra)
		return NULL;
	path = malloc(left_len + right_len + extra);
	if (!path)
		return NULL;

	memcpy(path, left, left_len);
	if (need_slash)
		path[left_len++] = '/';
	memcpy(path + left_len, right, right_len);
	path[left_len + right_len] = '\0';
	return path;
}

static char *append_prefix(const char *prefix, const char *tail)
{
	size_t prefix_len = strlen(prefix);
	size_t tail_len = strlen(tail);
	char *result;

	if (prefix_len > SIZE_MAX - tail_len - 1)
		return NULL;
	result = malloc(prefix_len + tail_len + 1);
	if (!result)
		return NULL;
	memcpy(result, prefix, prefix_len);
	memcpy(result + prefix_len, tail, tail_len + 1);
	return result;
}

static void print_tree_status(const struct kitten_context *context,
    const char *prefix, const char *line, const char *english,
    const char *russian)
{
	printf("%s%s %s\n", prefix, line,
	    context->options->russian ? russian : english);
}

enum preview_scan {
	PREVIEW_TEXT,
	PREVIEW_BINARY,
	PREVIEW_GREW,
	PREVIEW_READ_ERROR
};

enum preview_open {
	PREVIEW_OPENED,
	PREVIEW_OPEN_ERROR,
	PREVIEW_OPEN_CHANGED
};

enum preview_line {
	PREVIEW_LINE_ERROR = -1,
	PREVIEW_LINE_EOF,
	PREVIEW_LINE_READY,
	PREVIEW_LINE_TRUNCATED
};

static FILE *open_preview(const char *path, const struct stat *expected,
    off_t *size, enum preview_open *result)
{
	struct stat st;
	FILE *fp;
	int fd;
	int flags;
	int saved_errno;

	/* Avoid blocking if a regular path is replaced with a FIFO before fstat. */
	fd = open(path, O_RDONLY | O_NONBLOCK | O_NOFOLLOW);
	if (fd < 0) {
		*result = PREVIEW_OPEN_ERROR;
		return NULL;
	}
	if (fstat(fd, &st) != 0) {
		saved_errno = errno;
		close(fd);
		errno = saved_errno;
		*result = PREVIEW_OPEN_ERROR;
		return NULL;
	}
	if (!S_ISREG(st.st_mode) || st.st_dev != expected->st_dev ||
	    st.st_ino != expected->st_ino) {
		close(fd);
		*result = PREVIEW_OPEN_CHANGED;
		return NULL;
	}

	flags = fcntl(fd, F_GETFL);
	if (flags < 0 || fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) != 0) {
		saved_errno = errno;
		close(fd);
		errno = saved_errno;
		*result = PREVIEW_OPEN_ERROR;
		return NULL;
	}

	fp = fdopen(fd, "rb");
	if (!fp) {
		saved_errno = errno;
		close(fd);
		errno = saved_errno;
		*result = PREVIEW_OPEN_ERROR;
		return NULL;
	}

	*size = st.st_size;
	*result = PREVIEW_OPENED;
	return fp;
}

static enum preview_scan scan_preview(const struct kitten_context *context,
    FILE *fp)
{
	unsigned char buf[4096];
	size_t total = 0;
	size_t nread;

	for (;;) {
		nread = fread(buf, 1, sizeof(buf), fp);
		if (ferror(fp))
			return PREVIEW_READ_ERROR;
		if (nread == 0)
			break;
		if (nread > context->options->preview_limit - total)
			return PREVIEW_GREW;
		if (memchr(buf, '\0', nread))
			return PREVIEW_BINARY;
		total += nread;
	}

	if (fseek(fp, 0, SEEK_SET) != 0)
		return PREVIEW_READ_ERROR;
	return PREVIEW_TEXT;
}

static enum preview_line read_preview_line(FILE *fp, char **line,
    size_t *capacity, size_t limit, size_t *length)
{
	size_t used = 0;
	int byte;

	while (used < limit) {
		byte = getc(fp);
		if (byte == EOF) {
			if (ferror(fp))
				return PREVIEW_LINE_ERROR;
			*length = used;
			return used ? PREVIEW_LINE_READY : PREVIEW_LINE_EOF;
		}
		if (used == *capacity) {
			size_t next = *capacity ? *capacity * 2 : 256;
			char *larger;

			if (next < *capacity || next > limit)
				next = limit;
			larger = realloc(*line, next);
			if (!larger)
				return PREVIEW_LINE_ERROR;
			*line = larger;
			*capacity = next;
		}
		(*line)[used++] = (char)byte;
		if (byte == '\n') {
			*length = used;
			return PREVIEW_LINE_READY;
		}
	}

	byte = getc(fp);
	if (byte == EOF) {
		if (ferror(fp))
			return PREVIEW_LINE_ERROR;
		*length = used;
		return used ? PREVIEW_LINE_READY : PREVIEW_LINE_EOF;
	}
	*length = used;
	return PREVIEW_LINE_TRUNCATED;
}

static int print_file_preview(struct kitten_context *context, const char *path,
    const char *prefix, const struct stat *expected)
{
	const struct kitten_options *options = context->options;
	struct kitten_totals *totals = context->totals;
	FILE *fp;
	char *line = NULL;
	char file_size[64];
	char limit_size[64];
	size_t cap = 0;
	size_t bytes_read = 0;
	size_t line_bytes;
	enum preview_scan scan;
	enum preview_open opened;
	enum preview_line line_status = PREVIEW_LINE_EOF;
	off_t size;
	int saw_content = 0;
	int had_error = 0;
	int close_failed;
	uintmax_t value;
	const char *bar = options->unicode_tree ? "│" : "|";
	const char *end = options->unicode_tree ? "└──" : "`--";

	printf("%s%s %s\n", prefix, options->unicode_tree ? "┌──" : "+--",
	    options->russian ? "содержимое" : "contents");
	fp = open_preview(path, expected, &size, &opened);
	if (!fp) {
		int open_error = errno;

		printf("%s%s ", prefix, bar);
		if (opened == PREVIEW_OPEN_CHANGED) {
			fputs(options->russian ? "[файл изменился во время открытия]" :
			    "[file changed while opening]", stdout);
		} else if (options->russian) {
			printf("[не удалось открыть: %s]", strerror(open_error));
		} else {
			printf("[cannot open: %s]", strerror(open_error));
		}
		putchar('\n');
		printf("%s%s\n", prefix, end);
		++totals->errors;
		return 1;
	}

	value = size > 0 ? (uintmax_t)size : 0;
	kitten_format_size(context, value, file_size, sizeof(file_size));
	kitten_format_size(context, (uintmax_t)options->preview_limit, limit_size,
	    sizeof(limit_size));
	if (value > (uintmax_t)options->preview_limit) {
		printf("%s%s ", prefix, bar);
		if (options->russian)
			printf("[пропущено: размер %s превышает лимит %s]\n",
			    file_size, limit_size);
		else
			printf("[skipped: %s exceeds %s limit]\n", file_size, limit_size);
		close_failed = fclose(fp) != 0;
		if (close_failed) {
			print_tree_status(context, prefix, bar, "[read error]", "[ошибка чтения]");
			++totals->errors;
		}
		printf("%s%s\n", prefix, end);
		++totals->skipped;
		return close_failed;
	}

	scan = scan_preview(context, fp);
	if (scan != PREVIEW_TEXT) {
		printf("%s%s ", prefix, bar);
		if (scan == PREVIEW_BINARY) {
			fputs(options->russian ? "[двоичный файл]" : "[binary file]", stdout);
		} else if (scan == PREVIEW_GREW) {
			if (options->russian)
				printf("[пропущено: файл вырос сверх лимита %s]", limit_size);
			else
				printf("[skipped: file grew beyond %s limit]", limit_size);
		} else {
			fputs(options->russian ? "[ошибка чтения]" : "[read error]", stdout);
		}
		putchar('\n');
		close_failed = fclose(fp) != 0;
		if (close_failed && scan != PREVIEW_READ_ERROR) {
			print_tree_status(context, prefix, bar, "[read error]", "[ошибка чтения]");
			++totals->errors;
		}
		printf("%s%s\n", prefix, end);
		if (scan == PREVIEW_READ_ERROR) {
			++totals->errors;
			return 1;
		}
		++totals->skipped;
		return close_failed;
	}

	while ((line_status = read_preview_line(fp, &line, &cap,
	    options->preview_limit - bytes_read, &line_bytes)) > PREVIEW_LINE_EOF) {
		bytes_read += line_bytes;
		if (line_bytes == 0)
			break;
		while (line_bytes > 0 && (line[line_bytes - 1] == '\n' ||
		    line[line_bytes - 1] == '\r')) {
			--line_bytes;
		}
		if (line_bytes == 0) {
			printf("%s%s\n", prefix, bar);
		} else {
			printf("%s%s ", prefix, bar);
			kitten_print_preview_text(context, stdout, line, line_bytes);
			putchar('\n');
		}
		saw_content = 1;
		if (line_status == PREVIEW_LINE_TRUNCATED)
			break;
	}

	if (line_status == PREVIEW_LINE_TRUNCATED) {
		printf("%s%s ", prefix, bar);
		if (options->russian)
			printf("[обрезано после %s]\n", limit_size);
		else
			printf("[truncated after %s]\n", limit_size);
		++totals->skipped;
	} else if (line_status == PREVIEW_LINE_ERROR || ferror(fp)) {
		print_tree_status(context, prefix, bar, "[read error]", "[ошибка чтения]");
		had_error = 1;
		++totals->errors;
	} else if (!saw_content) {
		print_tree_status(context, prefix, bar, "[empty file]", "[пустой файл]");
	}

	free(line);
	if (fclose(fp) != 0 && !had_error) {
		print_tree_status(context, prefix, bar, "[read error]", "[ошибка чтения]");
		had_error = 1;
		++totals->errors;
	}
	printf("%s%s\n", prefix, end);
	return had_error;
}

static int print_entry(struct kitten_context *context, const char *path,
    const char *prefix, int is_last, int is_root, long depth);

static int show_dirent(const struct dirent *entry)
{
	return strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0;
}

static DIR *open_directory(const char *path, const struct stat *expected,
    int *changed)
{
	struct stat st;
	DIR *dir;
	int fd;
	int saved_errno;

	*changed = 0;
	fd = open(path, O_RDONLY | O_NONBLOCK | O_DIRECTORY | O_NOFOLLOW);
	if (fd < 0)
		return NULL;
	if (fstat(fd, &st) != 0) {
		saved_errno = errno;
		close(fd);
		errno = saved_errno;
		return NULL;
	}
	if (!S_ISDIR(st.st_mode) || st.st_dev != expected->st_dev ||
	    st.st_ino != expected->st_ino) {
		close(fd);
		*changed = 1;
		return NULL;
	}

	dir = fdopendir(fd);
	if (!dir) {
		saved_errno = errno;
		close(fd);
		errno = saved_errno;
	}
	return dir;
}

static void report_directory_error(struct kitten_context *context,
    const char *prefix, int error, int changed)
{
	printf("%s%s ", prefix, kitten_tree_branch(context, 1));
	if (changed) {
		fputs(context->options->russian ?
		    "[каталог изменился во время открытия]" :
		    "[directory changed while opening]", stdout);
	} else if (context->options->russian) {
		printf("[не удалось прочитать каталог: %s]", strerror(error));
	} else {
		printf("[cannot read directory: %s]", strerror(error));
	}
	putchar('\n');
	++context->totals->errors;
}

static int entry_is_visible(const struct kitten_context *context,
    const char *path, const char *name)
{
	struct stat st;

	if (is_excluded(context, path, name))
		return 0;
	if (!context->options->dirs_only)
		return 1;
	return lstat(path, &st) != 0 || S_ISDIR(st.st_mode);
}

static int compare_names(const void *left, const void *right)
{
	const char *const *a = left;
	const char *const *b = right;

	return strcoll(*a, *b);
}

static int print_sorted_directory(struct kitten_context *context,
    const char *path, const char *prefix, long depth,
    const struct stat *expected)
{
	struct dirent *entry;
	char **entries = NULL;
	size_t count = 0;
	size_t capacity = 0;
	size_t i;
	int had_error = 0;
	int read_error = 0;
	int changed;
	DIR *dir = open_directory(path, expected, &changed);

	if (!dir) {
		report_directory_error(context, prefix, errno, changed);
		return 1;
	}

	errno = 0;
	while ((entry = readdir(dir)) != NULL) {
		char **larger;
		char *child_path;

		if (!show_dirent(entry))
			continue;
		child_path = join_path(path, entry->d_name);
		if (!child_path) {
			errno = ENOMEM;
			break;
		}
		if (!entry_is_visible(context, child_path, entry->d_name)) {
			++context->totals->skipped;
			free(child_path);
			errno = 0;
			continue;
		}
		free(child_path);
		if (count == capacity) {
			size_t next = capacity ? capacity * 2 : 64;

			if (next < capacity || next > SIZE_MAX / sizeof(*entries)) {
				errno = ENOMEM;
				break;
			}
			larger = realloc(entries, next * sizeof(*entries));
			if (!larger)
				break;
			entries = larger;
			capacity = next;
		}
		entries[count] = strdup(entry->d_name);
		if (!entries[count])
			break;
		++count;
		errno = 0;
	}
	if (errno)
		read_error = errno;
	if (closedir(dir) != 0 && !read_error)
		read_error = errno;
	if (read_error)
		had_error = 1;

	if (count == 0) {
		if (read_error)
			report_directory_error(context, prefix, read_error, 0);
		else
			print_tree_status(context, prefix, kitten_tree_branch(context, 1),
			    "[empty directory]", "[пустой каталог]");
		free(entries);
		return had_error;
	}

	qsort(entries, count, sizeof(*entries), compare_names);
	for (i = 0; i < count; ++i) {
		char *child_path = join_path(path, entries[i]);
		int is_last_visible;

		if (!child_path) {
			printf("%s%s ", prefix,
			    kitten_tree_branch(context, i == count - 1));
			kitten_print_escaped(stdout, entries[i]);
			printf(" %s\n", context->options->russian ? "[недостаточно памяти]" :
			    "[out of memory]");
			had_error = 1;
			++context->totals->errors;
			continue;
		}
		is_last_visible = i == count - 1;
		if (read_error)
			is_last_visible = 0;
		if (print_entry(context, child_path, prefix, is_last_visible, 0,
		    depth) != 0)
			had_error = 1;
		free(child_path);
	}

	for (i = 0; i < count; ++i)
		free(entries[i]);
	free(entries);
	if (read_error)
		report_directory_error(context, prefix, read_error, 0);
	return had_error;
}

static int print_unsorted_directory(struct kitten_context *context,
    const char *path, const char *prefix, long depth,
    const struct stat *expected)
{
	struct dirent *entry;
	char *pending = NULL;
	int had_error = 0;
	int saw_entry = 0;
	int read_error = 0;
	int changed;
	DIR *dir = open_directory(path, expected, &changed);

	if (!dir) {
		report_directory_error(context, prefix, errno, changed);
		return 1;
	}

	errno = 0;
	while ((entry = readdir(dir)) != NULL) {
		char *child_path;

		if (!show_dirent(entry))
			continue;
		child_path = join_path(path, entry->d_name);
		if (!child_path) {
			printf("%s%s ", prefix, kitten_tree_branch(context, 0));
			kitten_print_escaped(stdout, entry->d_name);
			printf(" %s\n", context->options->russian ? "[недостаточно памяти]" :
			    "[out of memory]");
			++context->totals->errors;
			had_error = 1;
			errno = 0;
			continue;
		}
		if (!entry_is_visible(context, child_path, entry->d_name)) {
			++context->totals->skipped;
			free(child_path);
			errno = 0;
			continue;
		}
		saw_entry = 1;

		if (pending) {
			if (print_entry(context, pending, prefix, 0, 0, depth) != 0)
				had_error = 1;
			free(pending);
		}
		pending = child_path;
		errno = 0;
	}
	if (errno)
		read_error = errno;
	if (closedir(dir) != 0 && !read_error)
		read_error = errno;

	if (pending) {
		if (print_entry(context, pending, prefix, !read_error, 0, depth) != 0)
			had_error = 1;
		free(pending);
	} else if (!saw_entry && !read_error) {
		print_tree_status(context, prefix, kitten_tree_branch(context, 1),
		    "[empty directory]", "[пустой каталог]");
	}
	if (read_error) {
		report_directory_error(context, prefix, read_error, 0);
		had_error = 1;
	}
	return had_error;
}

static int print_directory(struct kitten_context *context, const char *path,
    const char *prefix, long depth, const struct stat *expected)
{
	if (context->options->unsorted)
		return print_unsorted_directory(context, path, prefix, depth, expected);
	return print_sorted_directory(context, path, prefix, depth, expected);
}

static char *read_symlink_target(const char *path, off_t hint)
{
	size_t capacity = 128;
	char *target;
	ssize_t length;

	if (hint > 0 && (uintmax_t)hint < SIZE_MAX - 1)
		capacity = (size_t)hint + 1;
	if (capacity < 2)
		capacity = 2;
	target = malloc(capacity);
	if (!target)
		return NULL;

	for (;;) {
		length = readlink(path, target, capacity - 1);
		if (length < 0) {
			int saved_errno = errno;

			free(target);
			errno = saved_errno;
			return NULL;
		}
		if ((size_t)length < capacity - 1) {
			target[(size_t)length] = '\0';
			return target;
		}
		if (capacity > SIZE_MAX / 2) {
			free(target);
			errno = ENAMETOOLONG;
			return NULL;
		}
		capacity *= 2;
		{
			char *larger = realloc(target, capacity);

			if (!larger) {
				int saved_errno = errno;

				free(target);
				errno = saved_errno;
				return NULL;
			}
			target = larger;
		}
	}
}

static int print_entry(struct kitten_context *context, const char *path,
    const char *prefix, int is_last, int is_root, long depth)
{
	const struct kitten_options *options = context->options;
	struct kitten_totals *totals = context->totals;
	struct stat st;
	const char *slash = strrchr(path, '/');
	const char *name = is_root || !slash ? path : slash + 1;
	const char *branch = kitten_tree_branch(context, is_last);
	char *next_prefix;
	char size[64];
	uintmax_t value;

	if (lstat(path, &st) != 0) {
		int stat_error = errno;

		if (!is_root)
			printf("%s%s ", prefix, branch);
		kitten_print_escaped(stdout, name);
		putchar(' ');
		if (options->russian)
			printf("[не найдено: %s]\n", strerror(stat_error));
		else
			printf("[not found: %s]\n", strerror(stat_error));
		++totals->errors;
		return 1;
	}
	if (options->dirs_only && !S_ISDIR(st.st_mode)) {
		++totals->skipped;
		return 0;
	}

	if (S_ISLNK(st.st_mode)) {
		char *target = read_symlink_target(path, st.st_size);
		int saved_errno = errno;
		int target_error = target == NULL;

		if (!is_root)
			printf("%s%s ", prefix, branch);
		kitten_print_escaped(stdout, name);
		fputs(" -> ", stdout);
		if (target)
			kitten_print_escaped(stdout, target);
		else
			fputs(strerror(saved_errno), stdout);
		printf(" [%s]\n", options->russian ? "символическая ссылка" : "symlink");
		++totals->symlinks;
		free(target);
		if (target_error)
			++totals->errors;
		return target_error;
	}

	if (is_root && S_ISDIR(st.st_mode)) {
		++totals->directories;
		kitten_print_escaped(stdout, path);
		fputs("/\n", stdout);
		if (options->max_depth == 0)
			return 0;
		return print_directory(context, path, "", 1, &st);
	}

	value = st.st_size > 0 ? (uintmax_t)st.st_size : 0;
	kitten_format_size(context, value, size, sizeof(size));
	if (is_root) {
		kitten_print_escaped(stdout, path);
		if (S_ISREG(st.st_mode)) {
			++totals->files;
			add_file_size(context, st.st_size);
			printf(" [%s, %s]\n", options->russian ? "файл" : "file", size);
			if (!options->show_content)
				return 0;
			return print_file_preview(context, path, "    ", &st);
		}
		++totals->special;
		printf(" [%s, %s]\n", options->russian ? "специальный файл" :
		    "special file", size);
		return 0;
	}

	if (S_ISDIR(st.st_mode)) {
		++totals->directories;
		printf("%s%s ", prefix, branch);
		kitten_print_escaped(stdout, name);
		fputs("/\n", stdout);
		if (options->max_depth >= 0 && depth >= options->max_depth)
			return 0;
		next_prefix = append_prefix(prefix, kitten_tree_stem(context, is_last));
		if (!next_prefix) {
			printf("%s%s %s\n", prefix, kitten_tree_stem(context, is_last),
			    options->russian ? "[недостаточно памяти]" : "[out of memory]");
			++totals->errors;
			return 1;
		}
		{
			int result = print_directory(context, path, next_prefix, depth + 1,
			    &st);

			free(next_prefix);
			return result;
		}
	}

	printf("%s%s ", prefix, branch);
	kitten_print_escaped(stdout, name);
	if (S_ISREG(st.st_mode)) {
		++totals->files;
		add_file_size(context, st.st_size);
		printf(" [%s, %s]\n", options->russian ? "файл" : "file", size);
		if (!options->show_content)
			return 0;
		next_prefix = append_prefix(prefix, kitten_tree_stem(context, is_last));
		if (!next_prefix) {
			printf("%s%s %s\n", prefix, kitten_tree_stem(context, is_last),
			    options->russian ? "[недостаточно памяти]" : "[out of memory]");
			++totals->errors;
			return 1;
		}
		{
			int result = print_file_preview(context, path, next_prefix, &st);

			free(next_prefix);
			return result;
		}
	}

	++totals->special;
	printf(" [%s, %s]\n", options->russian ? "специальный файл" :
	    "special file", size);
	return 0;
}

int kitten_walk_path(struct kitten_context *context, const char *path,
    int is_last)
{
	return print_entry(context, path, "", is_last, 1, 0);
}
