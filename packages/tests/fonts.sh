#!/bin/sh
# The fonts to read code in (patch 0075): JetBrains Mono and Fira Code must be
# installed, and the system must see each as a family of one width -- an
# editor that lays text out on a grid asks for a fixed family and has to be
# given one. font_check prints what the Be API answers.
# Runs on the target from boot-test.sh; the last line is PASS or FAIL.
#   packages/boot-test.sh packages/tests/fonts.sh font_check
fail=0
say() { echo "$*"; echo "fonts probe: $*" > /dev/dprintf 2>/dev/null; }

for f in /boot/system/data/fonts/otfonts/JetBrainsMono-Regular.otf \
		/boot/system/data/fonts/otfonts/FiraCode-Regular.otf; do
	if [ -e "$f" ]; then say "present: $f"; else say "MISSING: $f"; fail=1; fi
done

# the program that asks the Be API comes from its own package: without it
# this probe can say nothing about the fonts, and should say that rather
# than report them as not fixed
if ! font_check > /boot/home/fonts.txt 2>&1; then
	if ! command -v font_check > /dev/null 2>&1; then
		say "no font_check on this machine: run this probe with it"
		say "  packages/boot-test.sh packages/tests/fonts.sh font_check"
		say FAIL
		exit 1
	fi
fi

while IFS= read -r line; do say "$line"; done < /boot/home/fonts.txt

for want in "JetBrains Mono" "Fira Code"; do
	found=0
	while IFS= read -r line; do
		case "$line" in
		*"\"$want\""*"[fixed]"*) found=1 ;;
		esac
	done < /boot/home/fonts.txt
	if [ $found = 1 ]; then say "$want: a family of one width"; else say "$want: NOT a fixed family"; fail=1; fi
done

[ $fail = 0 ] && say PASS || say FAIL
