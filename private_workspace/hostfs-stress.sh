# UserBootscript: many small HostFS requests from 8 threads at once, so that
# completions arrive while a queue's interrupt is being handled -- the pattern
# that exposes completions arriving while the interrupt is off. Results in
# /boot/home/stress.txt (extract.sh <name> /home/stress.txt); the virtio_fs
# driver logs "completion(s) found only by polling" to the RAM console.
(
	sleep 25
	H=/HostFS
	{
		mkdir -p $H/stress
		i=0; while [ $i -lt 2000 ]; do echo $i > $H/stress/f$i; i=$((i+1)); done
		echo "== 8 readers x 2000 files (open, read, close; bash builtins, no processes)"
		time {
			for r in 1 2 3 4 5 6 7 8; do
				( i=0; while [ $i -lt 2000 ]; do read x < $H/stress/f$i; i=$((i+1)); done ) &
			done
			wait
		}
		echo "== 8 writers x 500 files"
		time {
			for r in 1 2 3 4 5 6 7 8; do
				( i=0; while [ $i -lt 500 ]; do echo $r > $H/stress/w$r-$i; i=$((i+1)); done ) &
			done
			wait
		}
		echo "== files: $(ls $H/stress | wc -l) (expect 6000)"
		rm -r $H/stress
		echo "== done"
	} > /boot/home/stress.txt 2>&1
) &
