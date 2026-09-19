#include <Application.h>
#include <Font.h>
#include <cstdio>
int main(int argc, char** argv)
{
	printf("probe: start\n"); fflush(stdout);
	{
		BApplication app("application/x-vnd.prose.ProseWriter");
		printf("probe: app ok\n"); fflush(stdout);
		BFont f(be_plain_font);
		float w = f.StringWidth("hello");
		printf("probe: width %.1f\n", w); fflush(stdout);
	}
	printf("probe: done\n"); fflush(stdout);
	return 0;
}
