#!/bin/sh
# probe for boot-test.sh: packages/boot-test.sh packages/tests/codecs.sh codec_check
echo "== $(uname -a)"
codec_check
status=$?
echo "codec_check exit status: $status"
[ $status = 0 ] && echo PASS || echo FAIL
