#!/bin/sh
# Sisong, the programmer's editor ported in ProseApps (ports/Sisong), as a
# package in the image: it must be there with its menu entry, open a file
# named on the command line, take typing, save exactly what it shows (a
# line longer than its old 1022-character limit, UTF-8 it does not
# understand but must not damage, no newline at the end),
# take a second file from a second launch into the same window (single
# launch), survive the two things that crashed it most simply (a very long
# word, Find with the find box open), and quit cleanly when asked. It is driven through messages:
# hey sends the window the key-down and menu messages a user would cause
# (a verb of four characters is a message code: _KYD is B_KEY_DOWN, !MnF and
# !MnW are Sisong's File > Save and File > Exit, src/messages.h).
# Runs on the target from boot-test.sh; the last line is PASS or FAIL.
# Only bash, coreutils and hey are used.
#   SISONG: the executable (default /boot/system/apps/Sisong)
fail=0
say() { echo "$*"; echo "sisong probe: $*" > /dev/dprintf 2>/dev/null; }
APP=${SISONG:-/boot/system/apps/Sisong}
if [ -e "$APP" ]; then say "present: $APP"; else say "MISSING: $APP"; say FAIL; exit 1; fi
if [ -z "$SISONG" ]; then
	link=""
	for l in /boot/system/data/deskbar/menu/Applications/*/Sisong /boot/system/data/deskbar/menu/Applications/Sisong; do
		[ -e "$l" ] && link=$l
	done
	if [ -n "$link" ]; then say "menu entry: $link"; else say "MISSING: Deskbar menu entry"; fail=1; fi
fi

# a crash is to end in a report on the Desktop and a dead team, not in the
# debugger's alert waiting for a click until boot-test.sh gives up
mkdir -p /boot/home/config/settings/system/debug_server
printf 'executable_actions {\n\tSisong report\n}\n' > /boot/home/config/settings/system/debug_server/settings

dir=/boot/home/sisong-probe
rm -rf $dir; mkdir -p $dir
long=""; i=0; while [ $i -lt 300 ]; do long="${long}0123456789"; i=$((i + 1)); done
printf 'int main(void)\n{\n\tconst char *s = "%s";\n\t/* caf\303\251 \346\227\245\346\234\254 */\n\treturn 0;\n}' "$long" > $dir/probe.c
printf 'typed' > $dir/expected.c; cat $dir/probe.c >> $dir/expected.c
printf 'second\n' > $dir/second.txt

title() { hey -o Sisong get Title of Window 0 2>/dev/null; }
wait_title() {	# <substring> <seconds>
	n=0
	while [ $n -lt $2 ]; do
		case "$(title)" in *"$1"*) return 0 ;; esac
		sleep 1; n=$((n + 1))
	done
	return 1
}

"$APP" $dir/probe.c > /tmp/sisong.out 2>&1 &
pid=$!
if wait_title probe.c 30; then
	say "opened the file named on the command line: $(title)"
else
	say "FAILED to open probe.c (title: $(title)): $(head -c 300 /tmp/sisong.out | tr '\n' ' ')"; fail=1
fi

if kill -0 $pid 2>/dev/null; then
	# type "typed" at the start of the document, then File > Save
	for c in t y p e d; do hey Sisong _KYD of Window 0 with bytes=$c > /dev/null 2>&1; done
	sleep 1
	hey Sisong '!MnF' of Window 0 > /dev/null 2>&1
	sleep 1
	# (no cmp or diff in the image: compare checksums)
	if [ "$(sha256sum < $dir/probe.c)" = "$(sha256sum < $dir/expected.c)" ]; then
		say "typed and saved: the file is what was typed and nothing else ($(wc -c < $dir/probe.c) bytes)"
	else
		say "SAVED FILE DIFFERS: $(wc -c < $dir/probe.c) bytes, expected $(wc -c < $dir/expected.c); starts: $(head -c 40 $dir/probe.c | tr '\n' ' ')"; fail=1
	fi

	# a second launch hands its file to the running one
	"$APP" $dir/second.txt > /dev/null 2>&1 &
	second=$!
	if wait_title second.txt 15; then say "second launch: its file opened in the running window"; else say "second launch: second.txt did not open (title: $(title))"; fail=1; fi
	sleep 1
	if kill -0 $second 2>/dev/null; then say "second launch is still running: not single launch"; kill -9 $second 2>/dev/null; fail=1; fi
	wait $second 2>/dev/null

	# what used to crash it: a "word" longer than the lexer's 2048-byte stack
	# buffer (opening the file was enough), and Search > Find asked for while
	# the find box is open (its constructor deleted itself: !Mnk is the menu's
	# message)
	i=0; blob=""; while [ $i -lt 60 ]; do blob="${blob}0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef01234567"; i=$((i + 1)); done
	printf 'x = 0x%s;\n' "$blob" > $dir/blob.txt
	"$APP" $dir/blob.txt > /dev/null 2>&1 &
	third=$!
	if wait_title blob.txt 15; then say "opened a file with a $(( $(wc -c < $dir/blob.txt) - 9 ))-character word"; else say "blob.txt did not open (title: $(title))"; fail=1; fi
	wait $third 2>/dev/null
	hey Sisong '!Mnk' of Window 0 > /dev/null 2>&1
	sleep 1
	hey Sisong '!Mnk' of Window 0 > /dev/null 2>&1
	sleep 2
	if kill -0 $pid 2>/dev/null; then say "find box asked for twice: still running"; else say "DIED when the find box was asked for twice"; fail=1; fi

	# File > Exit: everything is saved, so nothing may ask
	hey Sisong '!MnW' of Window 0 > /dev/null 2>&1
	n=0; while kill -0 $pid 2>/dev/null && [ $n -lt 10 ]; do sleep 1; n=$((n + 1)); done
	if kill -0 $pid 2>/dev/null; then
		say "did not quit within 10 s of File > Exit"; kill -9 $pid 2>/dev/null; fail=1
	else
		wait $pid 2>/dev/null; status=$?
		if [ $status = 0 ]; then say "quit cleanly (status 0)"; else say "quit with status $status: $(tail -c 300 /tmp/sisong.out | tr '\n' ' ')"; fail=1; fi
	fi
	if [ -e /boot/home/config/settings/Sisong/settings ]; then say "settings saved"; else say "no settings file written"; fail=1; fi
else
	wait $pid 2>/dev/null; status=$?
	say "EXITED at once (status $status): $(head -c 400 /tmp/sisong.out | tr '\n' ' ')"; fail=1
fi
for r in /boot/home/Desktop/Sisong-*-debug-*.report; do
	[ -e "$r" ] && { say "CRASH REPORT: $r"; fail=1; }
done
[ $fail = 0 ] && say PASS || say FAIL
