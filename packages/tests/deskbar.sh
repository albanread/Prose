#!/bin/sh
# The Deskbar's Applications menu in folders by category (patches 0053,
# 0055): lists the menu as the packages lay it out
# (/boot/system/data/deskbar/menu, one of the directories the Deskbar
# merges), folder by folder, and checks that every entry leads to an
# application and every folder has its icon (BEOS:ICON). Entries outside the
# folders are listed but allowed (an application DeskbarCategories does not
# know).
# Run it as
#   packages/boot-test.sh packages/tests/deskbar.sh
# Only bash and coreutils are used.
menu=/boot/system/data/deskbar/menu/Applications
fail=0
top=0
total=0
for entry in "$menu"/*; do
	name=$(basename "$entry")
	if [ -d "$entry" ] && [ ! -L "$entry" ]; then
		count=0
		list=""
		for app in "$entry"/*; do
			if [ ! -e "$app" ]; then
				echo "BROKEN: $app -> $(readlink "$app")"
				fail=1
			fi
			count=$((count + 1))
			list="$list, $(basename "$app")"
		done
		total=$((total + count))
		attrs=$(listattr "$entry" 2>&1)
		case "$attrs" in
		*BEOS:ICON*) icon="icon" ;;
		*) icon="NO ICON"; fail=1 ;;
		esac
		echo "$name ($count, $icon): ${list#, }"
	else
		if [ ! -e "$entry" ]; then
			echo "BROKEN: $entry -> $(readlink "$entry")"
			fail=1
		fi
		echo "not in a folder: $name"
		top=$((top + 1))
		total=$((total + 1))
	fi
done
echo "$total applications, $top of them outside the folders"
[ $total -gt 0 ] || fail=1
[ $fail = 0 ] && echo PASS || echo FAIL
