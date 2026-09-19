#!/bin/sh
# Supervisor for proseagent, started by UserBootscript. A file (not an
# inline subshell) so it can be read, and so the loop restarts the agent
# whatever took it down. The agent's own bind retry handles races.
while true; do
	/boot/home/apps/proseagent >> /boot/home/proseagent.log 2>&1
	sleep 2
done
