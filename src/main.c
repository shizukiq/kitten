#define _POSIX_C_SOURCE 200809L

#include "kitten.h"

#include <errno.h>
#include <fcntl.h>
#include <langinfo.h>
#include <limits.h>
#include <locale.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

enum argument_error_id {
	ERROR_PREVIEW_LIMIT,
	ERROR_DEPTH,
	ERROR_EXCLUDE,
	ERROR_CONTENT_MODE,
	ERROR_LANGUAGE,
	ERROR_UNKNOWN_OPTION
};

static int locale_is_russian(void)
{
	const char *name = setlocale(LC_MESSAGES, NULL);

	return name && (name[0] == 'r' || name[0] == 'R') &&
	    (name[1] == 'u' || name[1] == 'U') &&
	    (!name[2] || name[2] == '_' || name[2] == '-' || name[2] == '.');
}

static int locale_is_utf8(void)
{
	const char *codeset = nl_langinfo(CODESET);

	return codeset &&
	    (strcasecmp(codeset, "UTF-8") == 0 || strcasecmp(codeset, "UTF8") == 0);
}

static int set_language(struct kitten_options *options, const char *name)
{
	if (strcmp(name, "auto") == 0)
		options->russian = locale_is_russian();
	else if (strcmp(name, "en") == 0)
		options->russian = 0;
	else if (strcmp(name, "ru") == 0)
		options->russian = 1;
	else
		return -1;
	return 0;
}

static int set_depth_limit(struct kitten_options *options, const char *text)
{
	char *end;
	unsigned long value;

	if (!text[0] || text[0] < '0' || text[0] > '9')
		return -1;
	errno = 0;
	value = strtoul(text, &end, 10);
	if (errno == ERANGE || end == text || *end || value > LONG_MAX)
		return -1;
	options->max_depth = (long)value;
	return 0;
}

static int set_preview_limit(struct kitten_options *options, const char *text)
{
	char *end;
	unsigned long long value;
	unsigned long long scale = 1;
	unsigned long long total;

	if (!text[0] || text[0] < '0' || text[0] > '9')
		return -1;
	errno = 0;
	value = strtoull(text, &end, 10);
	if (errno == ERANGE || end == text)
		return -1;

	if (*end) {
		if (end[1])
			return -1;
		if (*end == 'k' || *end == 'K')
			scale = 1024;
		else if (*end == 'm' || *end == 'M')
			scale = 1024 * 1024;
		else if (*end == 'g' || *end == 'G')
			scale = 1024 * 1024 * 1024ULL;
		else
			return -1;
	}

	if (value > ULLONG_MAX / scale)
		return -1;
	total = value * scale;
	if (total > (unsigned long long)(size_t)-1)
		return -1;
	options->preview_limit = (size_t)total;
	return 0;
}

static int set_content_mode(struct kitten_options *options, const char *text)
{
	if (strcmp(text, "auto") == 0)
		options->show_content = 1;
	else if (strcmp(text, "never") == 0)
		options->show_content = 0;
	else
		return -1;
	return 0;
}

static int add_exclude(struct kitten_options *options, const char *pattern)
{
	char **patterns;
	char *copy;

	if (!pattern[0] || options->exclude_count >= SIZE_MAX / sizeof(*patterns))
		return -1;
	copy = strdup(pattern);
	if (!copy)
		return -1;
	patterns = realloc(options->exclude_patterns,
	    (options->exclude_count + 1) * sizeof(*patterns));
	if (!patterns) {
		free(copy);
		return -1;
	}
	options->exclude_patterns = patterns;
	options->exclude_patterns[options->exclude_count++] = copy;
	return 0;
}

static void print_usage(const char *argv0, int russian)
{
	if (russian) {
		fputs("Использование: ", stdout);
		kitten_print_escaped(stdout, argv0);
		fputs(" [ПАРАМЕТР]... [ПУТЬ]...\n\n"
		    "Печатает дерево путей с метаданными и просмотром текста.\n"
		    "Без путей просматривается текущий каталог. Параметры можно\n"
		    "указывать до или после путей.\n\n"
		    "  -m, --max-bytes=БАЙТЫ  лимит просмотра файла (по умолчанию 256K)\n"
		    "  -L, --depth=ГЛУБИНА     максимальная глубина обхода\n"
		    "      --exclude=ШАБЛОН    исключить совпавшие имена или пути\n"
		    "      --no-content        не показывать содержимое файлов\n"
		    "      --content=РЕЖИМ     режим просмотра: auto или never\n"
		    "      --raw-content       не экранировать содержимое файлов\n"
		    "      --dirs-only         показывать только каталоги\n"
		    "  -H, --human-readable    размеры в KiB, MiB, GiB и TiB\n"
		    "  -U, --unsorted          обходить каталог без сортировки\n"
		    "      --ascii             использовать ASCII-ветви\n"
		    "      --unicode           использовать Unicode-ветви\n"
		    "      --summary           напечатать итоговые счётчики\n"
		    "      --language=ЯЗЫК     язык сообщений: auto, en или ru\n"
		    "      --version           напечатать версию и лицензию\n"
		    "  -h, --help              показать эту справку\n\n"
		    "БАЙТЫ принимают суффикс K, M или G. '--' завершает разбор\n"
		    "параметров. Unicode автоматически выбирается в UTF-8-локали.\n\n"
		    "Примеры:\n"
		    "  kitten --no-content --summary src\n"
		    "  kitten -L 2 -m 64K --exclude=.git .\n"
		    "  kitten --language=ru -H README.md\n",
		    stdout);
	} else {
		fputs("Usage: ", stdout);
		kitten_print_escaped(stdout, argv0);
		fputs(" [OPTION]... [PATH]...\n\n"
		    "Print a tree of paths with metadata and text previews.\n"
		    "With no path, inspect the current directory. Options may appear\n"
		    "before or after path operands.\n\n"
		    "  -m, --max-bytes=BYTES   per-file preview limit (default: 256K)\n"
		    "  -L, --depth=DEPTH       maximum traversal depth\n"
		    "      --exclude=PATTERN   skip matching names or paths\n"
		    "      --no-content        do not show file contents\n"
		    "      --content=WHEN      preview mode: auto or never\n"
		    "      --raw-content       do not escape file contents\n"
		    "      --dirs-only         show directories only\n"
		    "  -H, --human-readable    print sizes in KiB, MiB, GiB, and TiB\n"
		    "  -U, --unsorted          traverse in filesystem order\n"
		    "      --ascii             use ASCII tree drawing\n"
		    "      --unicode           use Unicode tree drawing\n"
		    "      --summary           print totals after the tree\n"
		    "      --language=LANG     message language: auto, en, or ru\n"
		    "      --version           print version and license information\n"
		    "  -h, --help              display this help and exit\n\n"
		    "BYTES accepts a K, M, or G suffix. '--' ends option parsing.\n"
		    "Unicode drawing is selected automatically in a UTF-8 locale.\n\n"
		    "Examples:\n"
		    "  kitten --no-content --summary src\n"
		    "  kitten -L 2 -m 64K --exclude=.git .\n"
		    "  kitten --language=ru -H README.md\n",
		    stdout);
	}
}

static void print_version(int russian)
{
	printf("kitten %s\n", KITTEN_VERSION);
	if (russian) {
		fputs("Copyright (c) 2026, участники kitten\n"
		    "Лицензия BSD-2-Clause. Программа предоставляется без гарантий.\n",
		    stdout);
	} else {
		fputs("Copyright (c) 2026, kitten contributors\n"
		    "License BSD-2-Clause. This software comes with no warranty.\n",
		    stdout);
	}
}

static void select_argument_language(struct kitten_options *options,
    int argc, char **argv)
{
	int i;

	for (i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--") == 0)
			break;
		if (strcmp(argv[i], "--language") == 0) {
			if (i + 1 < argc)
				set_language(options, argv[++i]);
		} else if (strncmp(argv[i], "--language=", 11) == 0) {
			set_language(options, argv[i] + 11);
		} else if (strcmp(argv[i], "-m") == 0 ||
		    strcmp(argv[i], "--max-bytes") == 0 ||
		    strcmp(argv[i], "-L") == 0 ||
		    strcmp(argv[i], "--depth") == 0 ||
		    strcmp(argv[i], "--exclude") == 0 ||
		    strcmp(argv[i], "--content") == 0) {
			if (i + 1 < argc)
				++i;
		}
	}
}

static const char *argument_error_text(enum argument_error_id id, int russian)
{
	if (russian) {
		switch (id) {
		case ERROR_PREVIEW_LIMIT: return "недопустимый лимит просмотра";
		case ERROR_DEPTH: return "недопустимая глубина";
		case ERROR_EXCLUDE: return "недопустимый шаблон исключения";
		case ERROR_CONTENT_MODE: return "недопустимый режим содержимого";
		case ERROR_LANGUAGE: return "недопустимый язык";
		case ERROR_UNKNOWN_OPTION: return "неизвестный параметр";
		}
	}

	switch (id) {
	case ERROR_PREVIEW_LIMIT: return "invalid preview limit";
	case ERROR_DEPTH: return "invalid depth";
	case ERROR_EXCLUDE: return "invalid exclude pattern";
	case ERROR_CONTENT_MODE: return "invalid content mode";
	case ERROR_LANGUAGE: return "invalid language";
	case ERROR_UNKNOWN_OPTION: return "unknown option";
	}
	return "invalid argument";
}

static void free_options(struct kitten_options *options)
{
	size_t i;

	for (i = 0; i < options->exclude_count; ++i)
		free(options->exclude_patterns[i]);
	free(options->exclude_patterns);
}

static int finish_program(struct kitten_options *options, int status)
{
	if (fflush(stdout) == EOF || ferror(stdout))
		status = 1;
	if (fflush(stderr) == EOF || ferror(stderr))
		status = 1;
	free_options(options);
	return status;
}

static int argument_error(struct kitten_options *options,
    enum argument_error_id id, const char *argument)
{
	fprintf(stderr, "kitten: %s", argument_error_text(id, options->russian));
	if (argument) {
		fputs(": ", stderr);
		kitten_print_escaped(stderr, argument);
	}
	fputc('\n', stderr);
	return finish_program(options, 1);
}

static int enter_directory(const char *path, const struct stat *expected,
    int *changed)
{
	struct stat st;
	int fd;
	int saved_errno;

	*changed = 0;
	fd = open(path, O_RDONLY | O_NONBLOCK | O_DIRECTORY | O_NOFOLLOW);
	if (fd < 0)
		return -1;
	if (fstat(fd, &st) != 0) {
		saved_errno = errno;
		close(fd);
		errno = saved_errno;
		return -1;
	}
	if (!S_ISDIR(st.st_mode) || st.st_dev != expected->st_dev ||
	    st.st_ino != expected->st_ino) {
		close(fd);
		*changed = 1;
		return -1;
	}
	if (fchdir(fd) != 0) {
		saved_errno = errno;
		close(fd);
		errno = saved_errno;
		return -1;
	}
	close(fd);
	return 0;
}

int main(int argc, char **argv)
{
	struct kitten_options options;
	struct kitten_totals totals;
	struct kitten_context context;
	struct stat st;
	int parse_options = 1;
	int path_count = 0;
	int had_error = 0;
	int show_summary = 0;
	int changed;
	int saved_errno;
	int i;

	memset(&options, 0, sizeof(options));
	memset(&totals, 0, sizeof(totals));
	options.preview_limit = DEFAULT_PREVIEW_LIMIT;
	options.max_depth = -1;
	options.show_content = 1;
	context.options = &options;
	context.totals = &totals;

	setlocale(LC_ALL, "");
	options.russian = locale_is_russian();
	options.unicode_tree = locale_is_utf8();
	/* Diagnostics should honor --language even when it follows a bad option. */
	select_argument_language(&options, argc, argv);

	for (i = 1; i < argc; ++i) {
		const char *argument = argv[i];

		if (parse_options && strcmp(argument, "--") == 0) {
			parse_options = 0;
			continue;
		}
		if (!parse_options || argument[0] != '-' || argument[1] == '\0') {
			argv[++path_count] = argv[i];
			continue;
		}
		if (strcmp(argument, "-h") == 0 || strcmp(argument, "--help") == 0) {
			print_usage(argv[0], options.russian);
			return finish_program(&options, 0);
		}
		if (strcmp(argument, "--version") == 0) {
			print_version(options.russian);
			return finish_program(&options, 0);
		}
		if (strcmp(argument, "-m") == 0 || strcmp(argument, "--max-bytes") == 0) {
			if (++i == argc || set_preview_limit(&options, argv[i]) != 0)
				return argument_error(&options, ERROR_PREVIEW_LIMIT,
				    i < argc ? argv[i] : NULL);
			continue;
		}
		if (strncmp(argument, "--max-bytes=", 12) == 0) {
			if (set_preview_limit(&options, argument + 12) != 0)
				return argument_error(&options, ERROR_PREVIEW_LIMIT, argument + 12);
			continue;
		}
		if (strcmp(argument, "-L") == 0 || strcmp(argument, "--depth") == 0) {
			if (++i == argc || set_depth_limit(&options, argv[i]) != 0)
				return argument_error(&options, ERROR_DEPTH, i < argc ? argv[i] : NULL);
			continue;
		}
		if (strncmp(argument, "--depth=", 8) == 0) {
			if (set_depth_limit(&options, argument + 8) != 0)
				return argument_error(&options, ERROR_DEPTH, argument + 8);
			continue;
		}
		if (strcmp(argument, "--exclude") == 0) {
			if (++i == argc || add_exclude(&options, argv[i]) != 0)
				return argument_error(&options, ERROR_EXCLUDE, i < argc ? argv[i] : NULL);
			continue;
		}
		if (strncmp(argument, "--exclude=", 10) == 0) {
			if (add_exclude(&options, argument + 10) != 0)
				return argument_error(&options, ERROR_EXCLUDE, argument + 10);
			continue;
		}
		if (strcmp(argument, "--no-content") == 0) {
			options.show_content = 0;
			continue;
		}
		if (strcmp(argument, "--content") == 0) {
			if (++i == argc || set_content_mode(&options, argv[i]) != 0)
				return argument_error(&options, ERROR_CONTENT_MODE,
				    i < argc ? argv[i] : NULL);
			continue;
		}
		if (strncmp(argument, "--content=", 10) == 0) {
			if (set_content_mode(&options, argument + 10) != 0)
				return argument_error(&options, ERROR_CONTENT_MODE, argument + 10);
			continue;
		}
		if (strcmp(argument, "--raw-content") == 0) {
			options.raw_content = 1;
			continue;
		}
		if (strcmp(argument, "--dirs-only") == 0) {
			options.dirs_only = 1;
			continue;
		}
		if (strcmp(argument, "--language") == 0) {
			if (++i == argc || set_language(&options, argv[i]) != 0)
				return argument_error(&options, ERROR_LANGUAGE, i < argc ? argv[i] : NULL);
			continue;
		}
		if (strncmp(argument, "--language=", 11) == 0) {
			if (set_language(&options, argument + 11) != 0)
				return argument_error(&options, ERROR_LANGUAGE, argument + 11);
			continue;
		}
		if (strcmp(argument, "--ascii") == 0) {
			options.unicode_tree = 0;
			continue;
		}
		if (strcmp(argument, "--unicode") == 0) {
			options.unicode_tree = 1;
			continue;
		}
		if (strcmp(argument, "--summary") == 0) {
			show_summary = 1;
			continue;
		}
		if (strcmp(argument, "-H") == 0 ||
		    strcmp(argument, "--human-readable") == 0) {
			options.human_readable = 1;
			continue;
		}
		if (strcmp(argument, "-U") == 0 || strcmp(argument, "--unsorted") == 0) {
			options.unsorted = 1;
			continue;
		}
		return argument_error(&options, ERROR_UNKNOWN_OPTION, argument);
	}

	if (path_count == 0) {
		had_error = kitten_walk_path(&context, ".", 1) != 0;
	} else if (path_count == 1 && lstat(argv[1], &st) == 0 &&
	    S_ISDIR(st.st_mode)) {
		if (enter_directory(argv[1], &st, &changed) != 0) {
			saved_errno = errno;
			fputs("kitten: ", stderr);
			kitten_print_escaped(stderr, argv[1]);
			if (changed)
				fputs(options.russian ? ": каталог изменился во время открытия\n" :
				    ": directory changed while opening\n", stderr);
			else
				fprintf(stderr, ": %s\n", strerror(saved_errno));
			return finish_program(&options, 1);
		}
		had_error = kitten_walk_path(&context, ".", 1) != 0;
	} else {
		for (i = 1; i <= path_count; ++i) {
			if (i > 1)
				putchar('\n');
			if (kitten_walk_path(&context, argv[i], i == path_count) != 0)
				had_error = 1;
		}
	}

	if (show_summary)
		kitten_print_summary(&context);
	return finish_program(&options, had_error);
}
