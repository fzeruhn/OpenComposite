// A program for the applications case to launch (tests/unit/data/echo.vrmanifest): writes its arguments into the
// file named by its first argument, and exits.

#include <cstdio>

int main(int argc, char** argv)
{
	if (argc < 2)
		return 2;
	FILE* f = fopen(argv[1], "w");
	if (!f)
		return 1;
	for (int i = 2; i < argc; i++)
		fprintf(f, "%s%s", i > 2 ? " " : "", argv[i]);
	fprintf(f, "\n");
	fclose(f);
	return 0;
}
