#!/bin/sh
# HTTPS from the target: curl's CA bundle path and a real fetch (boot-test.sh
# gives the VM QEMU's user-mode network). Also shows the package links
# directory packagefs makes for curl, which haikuporter's recipes rely on.
# The last line is PASS or FAIL. Only bash and coreutils are used.
fail=0
say() { echo "$*"; echo "network probe: $*" > /dev/dprintf 2>/dev/null; }
say "curl: $(curl --version | head -1)"
# curl's CA bundle path is built in as haikuporter's recipe gives it: through
# the package links directory packagefs makes for curl (curl-config --ca on
# a Haiku system prints it; the tool itself is in the devel package)
d=$(ls -d /packages/curl-* 2>/dev/null | head -1)
say "package links of curl: $d: $(ls -A $d 2>/dev/null | tr '\n' ' ')"
ca=$d/ca_root_certificates/data/ssl/CARootCertificates.pem
[ -f "$ca" ] && say "CA bundle: $ca" || { say "CA bundle MISSING: $ca"; fail=1; }
for i in 1 2 3 4 5 6; do ifconfig 2>/dev/null | grep -q -i 'inet.*10\.0\.' && break; sleep 5; done
say "address: $(ifconfig 2>/dev/null | grep -i 'inet' | grep -v -i 'inet6\|127\.0' | head -1 | tr -s ' ')"
out=$(curl -sS -o /dev/null -w '%{http_code}' --max-time 40 https://www.haiku-os.org/ 2>&1)
case "$out" in
200|301|302) say "https fetch: HTTP $out" ;;
*) say "https fetch FAILED: $out"; fail=1 ;;
esac
[ $fail = 0 ] && say PASS || say FAIL
