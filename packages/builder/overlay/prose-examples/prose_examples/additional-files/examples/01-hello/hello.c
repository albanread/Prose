/*
 * The smallest complete C program, and how to build it by hand:
 *
 *     clang -O2 -Wall -o hello hello.c
 *
 * or, from this folder, just "make".
 */

#include <stdio.h>

int
main(int argc, char** argv)
{
	printf("Hello from Prose.\n");

	if (argc > 1) {
		printf("You gave me %d argument%s:\n", argc - 1, argc == 2 ? "" : "s");
		for (int i = 1; i < argc; i++)
			printf("  %d: %s\n", i, argv[i]);
	}

	return 0;
}
