# Pe 2.5.0: build/BuildSettings GLOBs /boot/system/develop/headers for
# pcre.h. Those paths exist in a Haiku chroot, here they are in the build
# sysroot.
PATCH()
{
	sed -i "s|/boot/system/develop/headers|$PROSE_SYSROOT/boot/system/develop/headers|" \
		build/BuildSettings
}
