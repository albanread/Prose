#!/bin/sh
# On Prose: build cpuhog with Prose's compiler, profile it, and check that
# the profiler sees what the program is known to do.
cd "$(dirname "$0")"
cc -O1 -fno-omit-frame-pointer -o cpuhog cpuhog.c || exit 1
profile -S -o user.out ./cpuhog > /dev/null || exit 1
profile -k -S -o kernel.out ./cpuhog > /dev/null || exit 1

passed=0
failed=0
check() {
	if [ "$1" = yes ]; then
		echo "PASS $2"
		passed=$((passed + 1))
	else
		echo "FAIL $2 ($3)"
		failed=$((failed + 1))
	fi
}
hits() {
	awk -v name="$2" '$NF == name && $1 ~ /^[0-9]+$/ { print $1; exit }' "$1"
}

total=$(awk '/total ticks:/ { print $3; exit }' user.out)
unknown=$(awk '/unknown ticks:/ { print $3; exit }' user.out)
three=$(hits user.out hog_three_quarters)
one=$(hits user.out hog_one_quarter)
stub=$(hits user.out _kern_get_thread_info)
kernel=$(awk '$NF == "kernel_arm64" { print $1; exit }' kernel.out)
kernelSyscall=$(hits kernel.out _user_get_thread_info)

check "$([ "${unknown:-1}" -eq 0 ] && echo yes)" \
	"every tick is attributed" "$unknown of $total unknown"
check "$(awk -v a="${three:-0}" -v b="${one:-0}" \
		'BEGIN { if (b > 0 && a / b > 2.5 && a / b < 3.5) print "yes" }')" \
	"the hot functions come out 3:1" "${three:-0} to ${one:-0}"
check "$([ "${stub:-0}" -gt $((total / 10)) ] && echo yes)" \
	"the syscall phase is in the syscall stub" "${stub:-0} of $total"
check "$([ "${kernel:-0}" -gt $((total / 10)) ] && echo yes)" \
	"with -k the kernel has hits" "${kernel:-0} of $total"
check "$([ -n "$kernelSyscall" ] && echo yes)" \
	"with -k the syscall's kernel side is named" "no _user_get_thread_info"

if [ $failed -eq 0 ]; then result=PASS; else result=FAIL; fi
echo "SELFTEST $result $passed/$((passed + failed))"
[ $failed -eq 0 ]
