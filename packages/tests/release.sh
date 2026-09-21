#!/bin/sh
# What a release promises, checked inside the machine -- run it on the machine
# the installer carries, not on the build tree's image, so the test is of the
# bits people get:
#
#   mount the DMG; ditto Installer.app/Contents/Resources/Prose.app out, and cp
#   its Contents/Resources/prose.image; put this file, packages/tests/mediakit.sh
#   and a file from-mac.txt containing "from the Mac" in a folder; then
#   Prose.app/Contents/MacOS/hvgpu <copy of prose.image> --headless \
#     --size 2560x1600 --automation --share <folder> --seconds 175 --script \
#     "35:click x=1691 y=1069,45:click x=1691 y=1069,60:run command=sh /HostFS/release.sh >/dev/null 2>&1 &,165:shutdown"
#   and read <folder>/release-check.log; its last line is PASS or FAIL.
#
# It checks: ProseWriter and ProseDraw installed, in the Deskbar's Office folder,
# and staying up; clang, clang++, lld and make, with a Be API program built and
# run; the examples; Sisong with its clang build command and File > Examples;
# every shipped theme and window frame, and a theme applying; an MP3 decoded
# through the Media Kit, ffmpeg and ffprobe, the MIDI soundfont; HostFS written
# from the guest and read from the Mac's side.
exec > /boot/home/release-check.log 2>&1
fail=0
ok()  { echo "ok    $*"; }
bad() { echo "FAIL  $*"; fail=1; }
until df | grep -q HostFS; do sleep 1; done

echo "== applications in Office"
office=/boot/system/data/deskbar/menu/Applications/Office
for app in ProseWriter ProseDraw; do
	if [ -e "$office/$app" ]; then ok "Office menu: $app"; else bad "Office menu has no $app ($(ls $office 2>/dev/null | tr '\n' ' '))"; fi
	if [ -x /boot/system/apps/$app ]; then ok "installed: /boot/system/apps/$app"; else bad "not installed: $app"; fi
done
for app in ProseWriter ProseDraw; do
	/boot/system/apps/$app >/dev/null 2>&1 &
	pid=$!
	sleep 6
	if kill -0 $pid 2>/dev/null; then ok "$app launches and stays up"; else bad "$app exited at once"; fi
	hey $app quit >/dev/null 2>&1; sleep 2; kill -9 $pid 2>/dev/null
done

echo "== developer tools and Sisong"
for t in clang clang++ lld ld.lld make; do
	if which $t >/dev/null 2>&1; then ok "$t: $(which $t)"; else bad "no $t"; fi
done
cat > /tmp/win.cpp <<'CPP'
#include <Application.h>
#include <stdio.h>
int main() { BApplication a("application/x-vnd.release-check"); puts("built against the Be API"); return 0; }
CPP
if clang++ -o /tmp/win /tmp/win.cpp -lbe && /tmp/win | grep -q "Be API"; then ok "clang++ builds and runs a Be API program"; else bad "clang++ could not build a Be API program"; fi
if make -C /boot/system/data/prose-examples/01-hello -f Makefile -n >/dev/null 2>&1; then ok "examples: $(ls /boot/system/data/prose-examples | grep -c -)"; else bad "examples missing"; fi
if [ -x /boot/system/apps/Sisong ]; then ok "Sisong installed ($(ls /boot/system/packages | grep -o 'sisong-[^ ]*hpkg'))"; else bad "Sisong missing"; fi
if grep -a -q -- '%c -g -Wall -o %e %f -lbe' /boot/system/apps/Sisong; then ok "Sisong compiles with clang (%c)"; else bad "Sisong's build command is not the clang one"; fi
if grep -a -q prose-examples /boot/system/apps/Sisong; then ok "Sisong has File > Examples"; else bad "Sisong has no Examples menu"; fi

echo "== themes"
themes=$(prosetheme --list 2>&1 | tr '\n' ',')
ok "themes: $themes"
for t in "Prose Light" "Prose Dark" Classic Platinum Win2k NeXTSTEP; do
	case ",$themes" in *",$t,"*|*",* $t,"*) ok "theme: $t" ;; *) bad "no theme $t" ;; esac
done
if prosetheme Platinum >/dev/null 2>&1 && [ "$(prosetheme --current)" = Platinum ]; then ok "Platinum applies"; else bad "Platinum did not apply"; fi
prosetheme "Prose Light" >/dev/null 2>&1
# setdecor -s marks the current one with a "*", so the name may follow one
for d in ProseDecorator ProseRightDecorator PlatinumDecorator Win2kDecorator NextDecorator; do
	if setdecor -s 2>/dev/null | grep -q "^\*\{0,1\}$d"; then ok "decorator: $d"; else bad "decorator $d not installed"; fi
done

echo "== sound: codecs through the Media Kit"
if sh /HostFS/mediakit.sh > /tmp/mediakit.out 2>&1 && tail -1 /tmp/mediakit.out | grep -q PASS; then
	ok "MP3 decodes through the Media Kit: $(grep -o 'codec [^;]*' /tmp/mediakit.out | head -1)"
else
	bad "Media Kit decode: $(tail -3 /tmp/mediakit.out | tr '\n' ' ')"
fi
for f in ffmpeg ffprobe; do if which $f >/dev/null 2>&1; then ok "$f on the command line"; else bad "no $f"; fi; done
ls /boot/system/data/synth 2>/dev/null | grep -qi sf2 && ok "General MIDI soundfont present" || echo "note  no soundfont found in data/synth"

echo "== HostFS"
if echo "from the guest" > /HostFS/from-guest.txt && [ "$(cat /HostFS/from-guest.txt)" = "from the guest" ]; then ok "HostFS writes and reads back"; else bad "HostFS write/read"; fi
if [ -f /HostFS/from-mac.txt ] && grep -q "from the Mac" /HostFS/from-mac.txt; then ok "HostFS sees a file the Mac wrote"; else bad "HostFS does not show the Mac's file"; fi

echo "== summary"
[ $fail = 0 ] && echo PASS || echo FAIL
cp /boot/home/release-check.log /HostFS/release-check.log
