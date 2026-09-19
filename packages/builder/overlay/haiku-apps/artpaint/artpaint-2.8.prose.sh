# ArtPaint: build.sh is "#!/bin/sh" but uses bash-4 syntax (";;&"). On
# Haiku /bin/sh is bash 5; on macOS it is bash 3.2. Run it with the build
# PATH's bash (5.x), and with -e: build.sh ignores a failed make, and its
# resource steps then create a resource file under the binary's name (the
# 2.8-1 package shipped that as "ArtPaint"). Same as the recipe otherwise.
BUILD()
{
	bash -e ./build.sh all
	head -c 4 dist/ArtPaint | grep -q ELF
}

# The binary build.sh puts in dist/ has no execute bit here, and the
# recipe's INSTALL copies it as it is: "Permission denied" on the target.
INSTALL()
{
	mkdir -p $appsDir/ArtPaint
	cp -r dist/* $appsDir/ArtPaint
	chmod +x $appsDir/ArtPaint/ArtPaint
	addAppDeskbarSymlink $appsDir/ArtPaint/ArtPaint
}
