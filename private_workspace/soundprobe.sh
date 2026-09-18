# UserBootscript for the audio path: waits for the media server, plays a tone,
# records what the Media Kit and the driver reported. Fetch with
#   private_workspace/extract.sh <name> /home/tone.txt
(
	sleep 30
	{
		echo "== $(date)"
		echo "== devices"; ls -R /dev/audio 2>&1
		echo "== tone"; prose_tone 4 440
		echo "== done"
	} > /boot/home/tone.txt 2>&1
	sync
) &
