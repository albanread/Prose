// font_check: what the Be API answers about this machine's fonts.
//
// Prints the three system fonts with the family and style each resolves to
// and whether that family is of one width, then every installed family. An
// editor that lays text out on a grid needs a fixed family; the app_server
// gives be_fixed_font whatever it can find when the family it wants is not
// installed, and that may be a proportional one.
//
// Exit status: the number of things that are wrong (a fixed font that is not
// fixed, no fixed family at all).

#include <Application.h>
#include <Font.h>
#include <stdio.h>
#include <string.h>

static int ShowSystemFont(const char *name, const BFont *font, bool mustBeFixed)
{
	font_family family;
	font_style style;
	font->GetFamilyAndStyle(&family, &style);

	printf("%s: \"%s\" \"%s\" %.1fpt fixed=%s  width(i)=%.2f width(M)=%.2f\n",
		name, family, style, font->Size(), font->IsFixed() ? "yes" : "no",
		font->StringWidth("i"), font->StringWidth("M"));

	if (mustBeFixed && !font->IsFixed())
	{
		printf("  WRONG: %s is not a font of one width\n", name);
		return 1;
	}

	return 0;
}

int main()
{
	BApplication app("application/x-vnd.prose-font_check");
	int wrong = 0;

	wrong += ShowSystemFont("be_plain_font", be_plain_font, false);
	wrong += ShowSystemFont("be_bold_font", be_bold_font, false);
	wrong += ShowSystemFont("be_fixed_font", be_fixed_font, true);

	int32 families = count_font_families();
	int fixed = 0;

	printf("%" B_PRId32 " font families:\n", families);

	for (int32 i = 0; i < families; i++)
	{
		font_family family;
		uint32 flags = 0;

		if (get_font_family(i, &family, &flags) != B_OK)
			continue;

		bool isFixed = (flags & B_IS_FIXED) != 0;
		if (isFixed) fixed++;

		printf("  \"%s\"%s%s\n", family, isFixed ? " [fixed]" : "",
			(flags & B_HAS_TUNED_FONT) ? " [tuned]" : "");
	}

	if (fixed == 0)
	{
		printf("  WRONG: not one family of a single width is installed\n");
		wrong++;
	}

	printf("%d fixed %s, %d wrong\n", fixed, fixed == 1 ? "family" : "families", wrong);
	return wrong;
}
