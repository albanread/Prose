# ArtPaint: build.sh is "#!/bin/sh" but uses bash-4 syntax (";;&"). On
# Haiku /bin/sh is bash 5; on macOS it is bash 3.2. Run it with the build
# PATH's bash (5.x). Same as the recipe's BUILD otherwise.
BUILD()
{
	bash ./build.sh all
}
