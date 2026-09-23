/*
 * Copyright 2026, HaikuArmQemu (PROSE fork). All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*!	The program spawntest spawns. It exits with 7 when its argv[0] is what
	SPAWN_EXPECT_ARGV0 says it should be, and with 8 when it is not, so the
	spawner learns both that this program ran and what it was called.
*/

#include <stdlib.h>
#include <string.h>


int
main(int argc, char** argv)
{
	const char* expected = getenv("SPAWN_EXPECT_ARGV0");
	if (argc < 1 || expected == NULL)
		return 9;
	return strcmp(argv[0], expected) == 0 ? 7 : 8;
}
