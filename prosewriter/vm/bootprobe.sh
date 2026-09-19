#!/bin/sh
# prosewriter boot probe: network state + host reachability, written to the
# home directory so the host can extract it (or the guest can push it).
{
  echo "=== prose boot probe $(date) ==="
  echo "--- uname"; uname -a
  echo "--- ifconfig -a"; ifconfig -a
  echo "--- route"; route 2>/dev/null || netstat -rn 2>/dev/null
  echo "--- resolv"; cat /boot/system/settings/network/resolv.conf 2>/dev/null
  echo "--- ping 10.0.2.2"; ping -c 2 -t 5 10.0.2.2 2>&1
  echo "--- wget ping.txt"
  wget -q -t 2 -T 5 -O /boot/home/hostping.txt http://10.0.2.2:8000/ping.txt 2>&1 \
    && cat /boot/home/hostping.txt || echo "wget failed"
  echo "--- end"
} > /boot/home/boot-probe.txt 2>&1
# push the report to the host so results are readable without a shutdown
wget -q -t 2 -T 5 --post-file=/boot/home/boot-probe.txt \
  http://10.0.2.2:8000/up/boot-probe.txt 2>/dev/null || true
