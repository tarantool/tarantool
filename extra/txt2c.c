/*
 * txt2c: Converts text files to C strings
 *
 * Compile with:
 *	gcc txt2cs.c -o txt2cs
 *
 * Public domain.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char** argv) {
	const char *prefix = "";
	const char *suffix = "\n";
	int no_quote = 0; /* if 1, do not prepend and append quotation marks (") */
	FILE *in = stdin;
	FILE *out = stdout;

	int c;
	while ((c = getopt(argc, argv, "np:s:h")) != -1) {
		switch (c) {
		case 'n':
			no_quote = 1;
			break;
		case 'p':
			prefix = optarg;
			break;
		case 's':
			suffix = optarg;
			break;
		case 'h':
			printf("Usage: %s [-n] [-p prefix] [-s suffix] [infile] [outfile]\n", argv[0]);
			exit(0);
			break;
		}
	}

	if (optind < argc) {
		if (strcmp(argv[optind], "-") != 0) {
			if (!(in = fopen(argv[optind], "r"))) {
				fprintf(stderr, "Can't open %s\n",
					argv[optind]);
				perror(argv[0]);
				exit(1);
			}
		}
		if (optind + 1 < argc) {
			if (strcmp(argv[optind + 1], "-") != 0) {
				if (!(out = fopen(argv[optind + 1], "w"))) {
					fprintf(stderr, "Can't open %s\n",
						argv[optind + 1]);
					perror(argv[0]);
					exit(1);
				}
			}
		}
	}

	fputs(prefix, out);
	if (!no_quote)
		fputs("\"", out);

	while ((c = fgetc(in)) != -1) {
		switch (c) {
		case '\0': fputs("\\0", out); break;
		case '\t': fputs("\\t", out); break;
		case '\n': fputs("\\n\"\n\"", out); break;
		case '\r': fputs("\\r", out); break;
		case '\\': fputs("\\\\", out); break;
		case '\"': fputs("\\\"", out); break;
		/* Don't interpret ??X as a trigraph. */
		case '?': fputs("\\\?", out); break;
		default: fputc(c, out); break;
		}
	}
	if (!no_quote)
		fputs("\"", out);
	fputs(suffix, out);
	return 0;
}

