# UserBootscript for HostFS: injected into a run's image copy by run-vz.sh when
# PW_INJECT_SCRIPT points here. Exercises the volume mount_server mounted from
# the host's shared folder (run-vz.sh ... --share DIR) and writes the results
# into the share itself (probe-result.txt, readable on the Mac straight away)
# and to /boot/home/hostfsprobe.txt (private_workspace/extract.sh <name>
# /home/hostfsprobe.txt).
#
# The host side prepares DIR with hello.txt, random.bin (+ random.bin.sha256)
# and tree/; see private_workspace/hostfs-test.sh.
(
	sleep 25
	H=/HostFS
	{
		echo "== $(date)"
		echo "== df"; df
		echo "== mounted?"; ls -la "$H"
		echo "== read hello.txt"; cat "$H/hello.txt"
		echo "== sha256 of random.bin (host says $(cat "$H/random.bin.sha256"))"
		sha256sum "$H/random.bin"
		echo "== write"; echo "written by Haiku" > "$H/from-haiku.txt"; cat "$H/from-haiku.txt"
		echo "== append"; echo "second line" >> "$H/from-haiku.txt"; cat "$H/from-haiku.txt"
		echo "== mkdir, rename, rmdir"
		mkdir "$H/dir1" && mv "$H/dir1" "$H/dir2" && ls -la "$H" && rmdir "$H/dir2" && echo "rmdir ok"
		echo "== rename a file"; mv "$H/from-haiku.txt" "$H/from-haiku-renamed.txt" && cat "$H/from-haiku-renamed.txt"
		echo "== symlink"; ln -s hello.txt "$H/link.txt" && ls -la "$H/link.txt" && cat "$H/link.txt"
		echo "== hard link"; ln "$H/hello.txt" "$H/hello-hard.txt" && ls -li "$H/hello.txt" "$H/hello-hard.txt"
		echo "== unlink"; rm "$H/link.txt" "$H/hello-hard.txt" && echo "rm ok"
		echo "== truncate"; cp "$H/hello.txt" "$H/trunc.txt" && truncate -s 5 "$H/trunc.txt" && cat "$H/trunc.txt" && echo && stat -c "%s bytes" "$H/trunc.txt"
		echo "== chmod"; chmod 600 "$H/trunc.txt" && stat -c "%A %n" "$H/trunc.txt"
		echo "== 64 MiB write"; time dd if=/dev/zero of="$H/zero.bin" bs=1048576 count=64
		ls -l "$H/zero.bin"
		echo "== 64 MiB read"; time dd if="$H/random.bin" of=/dev/null bs=1048576
		echo "== copy in and out"
		cp "$H/random.bin" /boot/home/random.bin && cp /boot/home/random.bin "$H/random-copy.bin"
		sha256sum /boot/home/random.bin "$H/random-copy.bin"
		echo "== run a program from the host"
		cp /boot/system/bin/listdev "$H/listdev-copy" && "$H/listdev-copy" > /dev/null && echo "exec ok"
		echo "== tree"; ls -laR "$H/tree"
		echo "== find"; find "$H/tree" -type f | wc -l
		echo "== error: no such file"; cat "$H/does-not-exist" 2>&1
		echo "== error: rmdir non-empty"; rmdir "$H/tree" 2>&1
		echo "== done $(date)"
	} > /boot/home/hostfsprobe.txt 2>&1
	cp /boot/home/hostfsprobe.txt "$H/probe-result.txt"
	sync
) &
