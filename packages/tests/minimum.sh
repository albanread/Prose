#!/bin/sh
# The packages the minimum profile asks for beyond the base set (patch 0041):
# openssl3 with its CA certificates, bc, wget. Runs on the target from
# boot-test.sh; the last line is PASS or FAIL. The minimum image has no grep,
# sed or awk: only bash, coreutils and what is under test.
fail=0
check() {
	# check <name> <command...>: prints the command's first output line
	name=$1; shift
	if out=$("$@" 2>&1); then
		echo "$name: ok: $(echo "$out" | head -1)"
	else
		echo "$name: FAILED: $(echo "$out" | head -2 | tr '\n' ' ')"
		fail=1
	fi
}
check openssl openssl version
certs=/boot/system/data/ssl/CARootCertificates.pem
if [ -s "$certs" ]; then
	echo "certificates: ok: $(openssl storeutl -noout -certs "$certs" | tail -1), $certs"
	# OpenSSL must trust them without being told where they are, or every
	# HTTPS client fails verification: the first root of the bundle verifies
	# against the default store only if the store holds it
	while IFS= read -r line; do
		echo "$line"
		[ "$line" = "-----END CERTIFICATE-----" ] && break
	done < "$certs" > /tmp/first-root.pem
	check "openssl trusts them by default" openssl verify /tmp/first-root.pem
else
	echo "certificates: FAILED: no $certs"; fail=1
fi
check bc sh -c 'echo "2^100" | bc'
[ "$(echo '2^100' | bc 2>/dev/null)" = "1267650600228229401496703205376" ] \
	|| { echo "bc: FAILED: wrong result for 2^100"; fail=1; }
check dc sh -c 'echo "3 4 * p" | dc'
check wget wget --version
[ $fail = 0 ] && echo PASS || echo FAIL
