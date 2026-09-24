#!/bin/sh
# On Prose: the boot test for patches 0142 and 0150. The boot loader and
# packagefs read the system's packages settings at boot. "block" writes
# settings with an empty blocked entry -- which both followed through an
# unset pointer: without 0150 the boot loader stops the machine, without
# 0142 the kernel writes through garbage -- and bin/fortune. After a
# reboot, "check" finds the system up and bin/fortune blocked, and restores
# the settings.
#
# Usage: packagefs-boot-test.sh block|check
SETTINGS=/boot/system/settings/packages
SAVED=/boot/system/settings/packages.afxtest-saved

case "$1" in
	block)
		[ -e $SETTINGS ] && mv $SETTINGS $SAVED
		cat > $SETTINGS <<'END'
Package haiku {
	BlockedEntries {
		""
		bin/fortune
	}
}
END
		sync
		echo "packagefs-blocked-entries: settings written, reboot to test"
		;;
	check)
		if [ -e /boot/system/bin/fortune ]; then
			echo "FAIL packagefs-blocked-entries: bin/fortune is not blocked"
		else
			echo "PASS packagefs-blocked-entries"
		fi
		rm -f $SETTINGS
		[ -e $SAVED ] && mv $SAVED $SETTINGS
		sync
		;;
	*)
		echo "usage: $0 block|check" >&2
		exit 1
		;;
esac
