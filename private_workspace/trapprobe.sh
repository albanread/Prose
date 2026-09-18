# UserBootscript for trap-test.sh: runs /boot/home/trap_check (trap_check.c)
# with debug_server told to kill crashed teams instead of asking, writes
# /boot/home/trap_check.txt and powers off.
# (plain sh only: the minimum image has no grep/sed)
settings=/boot/home/config/settings/system/debug_server
mkdir -p $settings
echo "default_action kill" > $settings/settings
(
	sleep 10
	{
		echo "== $(uname -a)"
		chmod +x /boot/home/trap_check
		/boot/home/trap_check
		echo "trap_check exit status: $?"
	} > /boot/home/trap_check.txt 2>&1
	sync
	shutdown -q
) &
