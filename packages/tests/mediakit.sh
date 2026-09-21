#!/bin/sh
# The Media Kit must be able to decode, not merely have the libraries: the
# image shipped mpg123, lame, flac, vorbis and opus while
# /boot/system/add-ons/media/plugins held only http_streamer and raw_decoder,
# so MediaPlayer could open nothing and codecs.sh passed all the same. Haiku
# decodes through its ffmpeg plugin, which is built only when ffmpeg6_devel
# is available (build/jam/BuildFeatures) and loads only if every library it
# was linked against is installed -- libgme was missing once, and the kit
# answered "No handler" for every file with no hint as to why.
#
# This probe compiles a small Media Kit program with the compiler in the
# image, gives it an MP3 that travels with this file, and expects frames out.
#   packages/boot-test.sh packages/tests/mediakit.sh
fail=0
say() { echo "$*"; echo "mediakit probe: $*" > /dev/dprintf 2>/dev/null; }

plugins=/boot/system/add-ons/media/plugins
say "plugins: $(ls $plugins | tr '\n' ' ')"
if [ -e $plugins/ffmpeg ]; then
	say "present: the ffmpeg media plugin"
else
	say "MISSING: the ffmpeg media plugin"; fail=1
fi

dir=/boot/home/mediakit-probe
rm -rf $dir; mkdir -p $dir
cat > $dir/tone.mp3.b64 <<'B64'
SUQzBAAAAAAAIlRTU0UAAAAOAAADTGF2ZjYyLjMuMTAwAAAAAAAAAAAAAAD/83DAAAAAAAAAAAAA
SW5mbwAAAA8AAAAOAAAGbQAsLCwsLCwsPDw8PDw8PE1NTU1NTU1dXV1dXV1dbW1tbW1tbX19fX19
fX2Ojo6Ojo6Onp6enp6enp6urq6urq6uvr6+vr6+vs/Pz8/Pz8/f39/f39/f7+/v7+/v7///////
//8AAAAATGF2YzYyLjExAAAAAAAAAAAAAAAAJANpAAAAAAAABm16S8WoAAAAAAAAAAAAAAAAAP/z
QMQAFGiGcBdYGAB/5KAmOmOmOkOkWpvA7oKkLZmE5tic6nWZtSChqbuO7kYhh/H8jEYpLDuBgYGL
B94gBAEMuH+jdy/hjgN/DHL+4MRACYP5MEHYDP935cPggGNKbERQwgYDAgEA//NCxAkWyXKdn5po
AgAAGBJhaT9yXiWSEUWGDSEGzAS0C0xbImtSABPoyJ6JiJb+FuBagVr8kR6j1Mv8cwwxNHqPX/zI
vF4xLpdS//y8SRiXS6ZF4vHf8qEgaEoSBorVFklbEkkH7xxk//NAxAkRoFZYf90AAooEFw2MBQkA
QnGC4vGSY8H26omTQ3lAgNeQlPO/sar2YwVtoaqK6CM6nd/7P86vX5iyvZ+/Z+1Fnb09/97lEkvv
/errRjAVAnsvqpkCgAIwCwAfN5cJrgOIiw//80LEHRSA+hhK/sRkKheaTT50VGpp/m2ez/Tp/G+j
QLqHdvKjmLJvV1FVobL2vihCk48wKKnyDnWIolnL+Ldr3B3AA2VwQWRsV0CzEcx9oy7pbY6lzyRo
DpoMismmUiyfURv/XR5rVXf/80DEJw24Qjm+Lvwg/4qmkjm+zj/X2bPY/u//3ifrHUywoyEssLAG
icYAKAAGAbAG5xCBuEc8DhxUma30mn64rvVWXsooUpqaVd6VfJ6qdkCb+rllOnOM6sadrZSKKOKi
CV40ep3KaP/zQsRLEsBOFALv9kAoJjJ+/3ceky31kql2l4TAGwCA1s8olA8IaCs2gWesCu0UW7fv
9fI/+s9Q19lSG7fo9X6MX/XcqExfXU3DP2UVF+vyqt2MCKCYy5qQwCAAjAKgDk3Uw5COEBw4af/z
QMRcD9hKIEzn9EBevNIZ9Uu1O2/9n2+v/x7P1NGua36U1f+2v+tkczam721+j1rZaYWAmUh1/63k
8Jm3lJfLCl4TAGwCg1ksvFO6AFgrNoFnrwri6NKl7P10+FdEC3f6On9rtdfd//NCxHcQoPoYAv7E
ZO77Voq//5xhP1vVVuxlFKZcFIYwAEAAMAoAQzcjkEg4ANDBpWF1pDb2Qscowpf0dkiX9N16Jr/i
3qr9VLLPt16NP3/XuZl9JtXv5YzTMTMmoAKABd0wAYAHMAzA//NAxJAOwEog8uf0QFY4BFOnOTFA
ghSSbWLWBC+37ElT3el+SlaaNTxh2xj+LqbnfotZFP9AfnELr31arK3bPok1JPCkojcvfRgBgAAB
EoBZgjCDgwJUw/Q1zFWHKMbAEc9eoADGzAz/80LEsBBQThgo7/ZATDkDeMH4EwwRAGDALAdQJqdo
ju5Y1t/vvZt3f+Ki/r7df/2//+j7//6FIEIIIIWHGBMZIC5mEgJzNHCCybD9wOGJqq7r4cE+AIjD
4FO/MkweRIMaqBIGOYXwNvX/80DEyhHgShQA7/ZAAMSzAyyRUyHGUDQLXAGh4DAQmkyktRoeAaCg
NBgwqDcJkmcU5oaOwXSEhDoRNggPVrQXrdyBiwClxkhkR3C5l1rr3ffjkj6HUQ8gBcImRAnf/9v5
sal0yOmJgf/zQsTdE+BeLFVeEACaZobgz//lgZFAcDIBBkJN///EgfMqN4QoTJVjdDUj9eEqElBy
iYqgbwaofx6iaoUpVDEUyHM12EBAIK00AolEoBgrTQCixIGKgrASK//imhTZorIbF6bCf//8Lv/z
QMTpKJoeYFGcoAAdjUxBTUUzLjEwMFVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV
VVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV//NCxKER6LWcAc8w
AVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV
VVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV
B64
base64 -d < $dir/tone.mp3.b64 > $dir/tone.mp3 2>/dev/null \
	|| base64 -D < $dir/tone.mp3.b64 > $dir/tone.mp3
say "sample: $(wc -c < $dir/tone.mp3) bytes of MP3"

cat > $dir/probe.cpp <<'CPP'
#include <stdio.h>
#include <string.h>
#include <Application.h>
#include <Entry.h>
#include <MediaFile.h>
#include <MediaTrack.h>

// Open the file the way MediaPlayer does and decode it to the end.
int main(int argc, char **argv)
{
	if (argc < 2) return 2;
	BApplication app("application/x-vnd.prose-mediakit-probe");

	entry_ref ref;
	if (get_ref_for_path(argv[1], &ref) != B_OK) { printf("no %s\n", argv[1]); return 1; }

	BMediaFile file(&ref);
	if (file.InitCheck() != B_OK) {
		printf("the Media Kit will not open it: %s\n", strerror(file.InitCheck()));
		return 1;
	}

	BMediaTrack *track = NULL;
	media_format format;
	for (int32 i = 0; i < file.CountTracks(); i++) {
		BMediaTrack *t = file.TrackAt(i);
		memset((void *)&format, 0, sizeof(format));
		format.type = B_MEDIA_RAW_AUDIO;
		if (t && t->DecodedFormat(&format) == B_OK && format.type == B_MEDIA_RAW_AUDIO) {
			track = t;
			break;
		}
		if (t) file.ReleaseTrack(t);
	}
	if (!track) { printf("no audio track the kit can decode\n"); return 1; }

	media_codec_info codec;
	if (track->GetCodecInfo(&codec) == B_OK) printf("codec %s, ", codec.pretty_name);
	printf("%d Hz, %d channels, ", (int)format.u.raw_audio.frame_rate,
		(int)format.u.raw_audio.channel_count);

	char buffer[16384];
	int64 total = 0;
	while (true) {
		int64 frames = 0;
		media_header header;
		if (track->ReadFrames(buffer, &frames, &header) != B_OK || frames <= 0) break;
		total += frames;
	}
	printf("%lld frames\n", (long long)total);
	return total > 0 ? 0 : 1;
}
CPP
if clang++ -O2 -o $dir/probe $dir/probe.cpp -lbe -lmedia > $dir/build.log 2>&1; then
	out=$($dir/probe $dir/tone.mp3 2>&1)
	status=$?
	say "$(echo "$out" | tr '\n' ';')"
	if [ $status = 0 ]; then
		say "the Media Kit decoded an MP3"
	else
		say "THE MEDIA KIT COULD NOT DECODE AN MP3"; fail=1
	fi
else
	say "COULD NOT BUILD THE PROBE: $(tail -c 300 $dir/build.log | tr '\n' ' ')"; fail=1
fi

[ $fail = 0 ] && say PASS || say FAIL
