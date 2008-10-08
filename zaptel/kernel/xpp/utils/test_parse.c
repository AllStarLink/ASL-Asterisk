#include <stdio.h>
#include <stdarg.h>
#include "hexfile.h"

static void default_report_func(int level, const char *msg, ...)
{
	va_list ap;

	va_start(ap, msg);
	vfprintf(stderr, msg, ap);
	va_end(ap);
}

int main(int argc, char *argv[])
{
	struct hexdata	*hd;
	int		i;

	if(argc < 2) {
		fprintf(stderr, "Usage: program hexfile...\n");
		return 1;
	}
	parse_hexfile_set_reporting(default_report_func);
	for(i = 1; i < argc; i++) {
		hd = parse_hexfile(argv[i], 2000);
		if(!hd) {
			fprintf(stderr, "Parsing failed\n");
			return 1;
		}
		fprintf(stderr, "=== %s === (version: %s)\n", argv[i], hd->version_info);
		dump_hexfile2(hd, "-", 60 );
		free_hexdata(hd);
	}
	return 0;
}
