#!/bin/sh
# Sisong, the programmer's editor ported in ProseApps (ports/Sisong), as a
# package in the image: it must be there with its menu entry, open a file
# named on the command line, take typing, save exactly what it shows (a
# line longer than its old 1022-character limit, UTF-8 it does not
# understand but must not damage, no newline at the end),
# take a second file from a second launch into the same window (single
# launch), survive the two things that crashed it most simply (a very long
# word, Find with the find box open), complete a word from the api index
# (Edit > Complete Word: !Mnh), and quit cleanly when asked. It is driven
# through messages:
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

	# Edit > Complete Word (!Mnh) completes the word before the caret from the
	# index of the Be/Haiku API (data/sisong/api-index): a word only one thing
	# can be is finished without a list, a word many things can be opens one
	# and the enter key takes the first. The arrow keys walk it: four downs
	# from BText reach BTextView.
	api_idx=""
	for idx in /boot/system/data/sisong/api-index /boot/home/config/non-packaged/data/sisong/api-index; do
		[ -e "$idx" ] && api_idx=$idx
	done
	if [ -n "$api_idx" ]; then
		say "api index: $api_idx ($(wc -l < $api_idx) symbols)"
		printf "" > $dir/apic.cpp
		"$APP" $dir/apic.cpp > /dev/null 2>&1 &
		apicpid=$!
		if wait_title apic.cpp 15; then
			for c in B T e x t V; do hey Sisong _KYD of Window 0 with bytes=$c > /dev/null 2>&1; done
			hey Sisong '!Mnh' of Window 0 > /dev/null 2>&1
			sleep 1
			for c in ' ' B T e x t; do hey Sisong _KYD of Window 0 with bytes="$c" > /dev/null 2>&1; done
			hey Sisong '!Mnh' of Window 0 > /dev/null 2>&1
			sleep 1
			i=0; while [ $i -lt 4 ]; do hey Sisong _KYD of Window 0 with bytes="$(printf '\037')" > /dev/null 2>&1; i=$((i + 1)); done
			hey Sisong _KYD of Window 0 with bytes="
" > /dev/null 2>&1
			hey Sisong '!MnF' of Window 0 > /dev/null 2>&1
			sleep 1
			printf 'BTextView BTextView' > $dir/apic-expected
			if [ "$(sha256sum < $dir/apic.cpp)" = "$(sha256sum < $dir/apic-expected)" ]; then
				say "complete word: BTextV finished itself, BText + 4 downs + enter gave BTextView"
			else
				say "COMPLETE WORD: got '$(cat $dir/apic.cpp)'"; fail=1
			fi
		else
			say "apic.cpp did not open (title: $(title))"; fail=1
		fi
		wait $apicpid 2>/dev/null
	else
		say "NO api index: neither data directory has sisong/api-index"; fail=1
	fi

	# The same command in a C or C++ document also asks clangd, through
	# clangd_server -- which nothing has started: Sisong must start it. The
	# caret goes after "p.x_lo" in a file whose struct has x_location, a
	# name only clangd can know; the first question opens the session, the
	# second carries the text, the answer joins the list, enter takes it.
	printf 'struct Point { int x_location; };\nvoid f() { Point p; p.x_lo' > $dir/lsp.cpp
	"$APP" $dir/lsp.cpp > /dev/null 2>&1 &
	lsppid=$!
	if wait_title lsp.cpp 15; then
		hey Sisong _KYD of Window 0 with bytes="$(printf '\037')" > /dev/null 2>&1
		hey Sisong _KYD of Window 0 with bytes="$(printf '\004')" > /dev/null 2>&1
		hey Sisong '!Mnh' of Window 0 > /dev/null 2>&1
		sleep 4
		hey Sisong '!Mnh' of Window 0 > /dev/null 2>&1
		sleep 6
		hey Sisong _KYD of Window 0 with bytes="
" > /dev/null 2>&1
		sleep 1
		hey Sisong '!MnF' of Window 0 > /dev/null 2>&1
		sleep 1
		if grep -q 'p\.x_location' $dir/lsp.cpp; then
			say "clangd completion: p.x_lo became p.x_location"
		else
			say "CLANGD COMPLETION: got '$(tail -1 $dir/lsp.cpp)'"; fail=1
		fi
		if ps | grep -v grep | grep -q clangd_server; then
			say "clangd_server was started by Sisong"
		else
			say "CLANGD_SERVER IS NOT RUNNING: Sisong did not start it"; fail=1
		fi
	else
		say "lsp.cpp did not open (title: $(title))"; fail=1
	fi
	wait $lsppid 2>/dev/null

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

	# a new user's colours: the scheme Paper, as the owner asked (a light yellow
	# ground; Midnight Blue instead when the desktop's document background is dark)
	selected=""; first=""; second=""
	while IFS= read -r line; do
		case "$line" in
		'%SelectedColorScheme = '*) selected=${line#*= } ;;
		'$scheme0_name = '*) first=${line#*= } ;;
		'$scheme1_name = '*) second=${line#*= } ;;
		esac
	done < /boot/home/config/settings/Sisong/settings
	if [ "$first" = Paper ] && [ "$second" = "Midnight Blue" ] && { [ "$selected" = 0 ] || [ "$selected" = 1 ]; }; then
		say "colour schemes: $first and $second lead, scheme $selected selected"
	else
		say "COLOUR SCHEMES: first '$first', second '$second', selected '$selected'"; fail=1
	fi
else
	wait $pid 2>/dev/null; status=$?
	say "EXITED at once (status $status): $(head -c 400 /tmp/sisong.out | tr '\n' ' ')"; fail=1
fi
# What Run > Compile This File does, checked without the menu: the default
# build command must name a compiler this machine has. It shipped naming g++,
# which Prose does not have, and every test here passed while the editor told
# its reader "there is no compiler on this machine".
if grep -a -q -- '%c -g -Wall -o %e %f -lbe' "$APP"; then
	say "default build command: %c, the compiler for the file"
else
	say "DEFAULT BUILD COMMAND is not the one that uses %c"; fail=1
fi
if grep -a -q prose-examples "$APP"; then say "File > Examples is in"; else say "NO examples menu"; fail=1; fi

ex=/boot/system/data/prose-examples/01-hello/hello.c
if [ -e "$ex" ]; then
	# the command the editor builds, with the same flags and order
	cc=clang
	if "$cc" -g -Wall -o $dir/hello "$ex" -lbe > $dir/cc.out 2>&1; then
		out=$($dir/hello 2>&1)
		case "$out" in
		*"Hello from Prose"*) say "compiled and ran an example with $cc: $out" ;;
		*) say "COMPILED BUT RAN WRONG: '$out'"; fail=1 ;;
		esac
	else
		say "COMPILE FAILED with $cc: $(head -c 300 $dir/cc.out | tr '\n' ' ')"; fail=1
	fi
else
	say "NO example at $ex"; fail=1
fi

for r in /boot/home/Desktop/Sisong-*-debug-*.report; do
	[ -e "$r" ] && { say "CRASH REPORT: $r"; fail=1; }
done
[ $fail = 0 ] && say PASS || say FAIL
