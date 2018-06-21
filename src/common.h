#define __attribute__(_arg_)

#define USAGE_HEADER    "\nUsage:\n"
#define USAGE_OPTIONS   "\nOptions:\n"
#define USAGE_SEPARATOR "\n"
#define USAGE_HELP_OPTIONS(marg_dsc)     \
	"%-" #marg_dsc "s%s\n"                 \
	"%-" #marg_dsc "s%s\n",                \
	" -h, --help",    "display this help", \
	" -v, --version", "display version"
#define USAGE_MAN_TAIL(_man) "\nFor more details see %s.\n", _man
#define UTIL_LINUX_VERSION "%s v1.0\n", program_invocation_short_name

#define errtryhelp(eval) __extension__ ({                    \
	fprintf(stderr, "Try '%s --help' for more information.\n", \
		program_invocation_short_name);                          \
	exit(eval);                                                \
})

#define errexit(msg) do { \
	perror(msg);            \
	exit(EXIT_FAILURE);     \
} while (0)

unsigned long strtoul_or_err(const char *str, const char *errmsg) {
	unsigned long num;
	char *end = NULL;

	errno = 0;
	if (str == NULL || *str == '\0') {
		goto err;
	}
	num = strtoul(str, &end, 10);

	if (errno || str == end || (end && *end)) {
		goto err;
	}
	return num;
err:
	errexit(errmsg);
}

