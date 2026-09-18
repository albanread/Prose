# UserBootscript for the MIDI path: lists /dev/midi, then plays a scale through
# the Midi Kit (midi_server -> /dev/midi/prose/0 -> the host). Fetch with
#   private_workspace/extract.sh <name> /home/midi.txt
(
	sleep 30
	{
		echo "== $(date)"
		echo "== devices"; ls -R /dev/midi 2>&1
		echo "== midi kit"; prose_midi_test
		echo "== direct"; prose_midi_test --direct
		echo "== done"
	} > /boot/home/midi.txt 2>&1
	sync
) &
