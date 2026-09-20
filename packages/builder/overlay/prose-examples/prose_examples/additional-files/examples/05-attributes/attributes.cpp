/*
 * File attributes: what makes this file system different.
 *
 *     clang++ -O2 -Wall -o attributes attributes.cpp -lbe
 *
 * Every file here can carry named, typed values beside its contents -- a
 * rating, an author, a date -- and the file system indexes them, so they can
 * be searched without reading any file. This writes a few, reads them back,
 * and lists what a file carries. Tracker shows the same values as columns.
 */

#include <fs_attr.h>	// attr_info, what an attribute is
#include <Node.h>
#include <StorageDefs.h>
#include <String.h>
#include <TypeConstants.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void
list_attributes(BNode& node)
{
	char name[B_ATTR_NAME_LENGTH];

	node.RewindAttrs();
	while (node.GetNextAttrName(name) == B_OK) {
		attr_info info;
		if (node.GetAttrInfo(name, &info) != B_OK)
			continue;

		printf("  %-24s %8" B_PRIdOFF " bytes  type ", name, info.size);

		switch (info.type) {
			case B_STRING_TYPE:
			{
				BString value;
				node.ReadAttrString(name, &value);
				printf("string   \"%s\"\n", value.String());
				break;
			}
			case B_INT32_TYPE:
			{
				int32 value = 0;
				node.ReadAttr(name, B_INT32_TYPE, 0, &value, sizeof(value));
				printf("int32    %" B_PRId32 "\n", value);
				break;
			}
			case B_TIME_TYPE:
			{
				time_t value = 0;
				node.ReadAttr(name, B_TIME_TYPE, 0, &value, sizeof(value));
				printf("time     %s", ctime(&value));
				break;
			}
			default:
				printf("0x%08" B_PRIx32 "\n", info.type);
				break;
		}
	}
}

int
main(int argc, char** argv)
{
	const char* path = (argc > 1) ? argv[1] : "/boot/home/attribute-example";

	// make the file if it is not there yet
	FILE* file = fopen(path, "a");
	if (file == NULL) {
		fprintf(stderr, "cannot open %s\n", path);
		return 1;
	}
	fclose(file);

	BNode node(path);
	if (node.InitCheck() != B_OK) {
		fprintf(stderr, "cannot open %s as a node\n", path);
		return 1;
	}

	// write three attributes of three different types
	node.WriteAttrString("Media:Title", new BString("An example"));

	int32 rating = 5;
	node.WriteAttr("Media:Rating", B_INT32_TYPE, 0, &rating, sizeof(rating));

	time_t now = time(NULL);
	node.WriteAttr("Example:Written", B_TIME_TYPE, 0, &now, sizeof(now));

	printf("%s carries:\n", path);
	list_attributes(node);

	printf("\nTry: listattr \"%s\"\n", path);
	printf("and: query 'Media:Rating == 5'\n");
	return 0;
}
