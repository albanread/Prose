#!/bin/sh
# probe for boot-test.sh: packages/boot-test.sh packages/tests/openssl.sh
# OpenSSL must start (no SIGILL probing on Haiku), use the ARMv8 crypto
# instructions it was built for, and compute right: known-answer digests
# exercise the SHA-512/SHA-3 hardware paths. The minimum image has no grep,
# sed or awk: only bash, coreutils and openssl itself.
echo "== $(uname -a)"
openssl version || { echo FAIL; exit; }
ok=1
check() {
	got=$(printf abc | openssl dgst -"$1"); got=${got##*= }
	if [ "$got" = "$2" ]; then echo "$1(abc) ok"; else echo "$1(abc) WRONG: $got"; ok=0; fi
}
check sha256 ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
check sha512 ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f
check sha3-256 3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532
key=000102030405060708090a0b0c0d0e0f
enc=$(printf 'Prose on arm64!!' | openssl enc -aes-128-ecb -K $key -nopad | od -An -tx1 | tr -d ' \n')
dec=$(printf 'Prose on arm64!!' | openssl enc -aes-128-ecb -K $key -nopad \
	| openssl enc -d -aes-128-ecb -K $key -nopad)
[ "$dec" = 'Prose on arm64!!' ] && echo "aes-128 round trip ok ($enc)" || { echo "aes-128 round trip WRONG"; ok=0; }
echo "== throughput (wall clock), 16 KiB blocks, 1 s: capabilities from the build vs OPENSSL_armcap=0 (plain C)"
for alg in aes-128-gcm sha256 sha512; do
	fast=$(openssl speed -elapsed -seconds 1 -bytes 16384 -evp $alg 2>/dev/null | tail -1); fast=${fast##* }
	slow=$(OPENSSL_armcap=0 openssl speed -elapsed -seconds 1 -bytes 16384 -evp $alg 2>/dev/null | tail -1); slow=${slow##* }
	echo "$alg: $fast vs $slow (kB/s)"
done
[ $ok = 1 ] && echo PASS || echo FAIL
