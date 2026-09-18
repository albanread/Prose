/*
 * codec_check — prints the version of every codec library in the Prose
 * package set and runs a small encode/decode round trip in each library
 * that has one. Run on the target: it proves the packages activate and
 * the libraries load, resolve and work on Haiku arm64.
 *
 * Exit status: number of failed checks.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <png.h>
#include <turbojpeg.h>
#include <webp/decode.h>
#include <webp/encode.h>
#include <tiffio.h>
#include <gif_lib.h>
#include <openjpeg.h>
#include <lcms2.h>
#include <ogg/ogg.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisenc.h>
#include <FLAC/stream_decoder.h>
#include <FLAC/stream_encoder.h>
#include <opus/opus.h>
#include <speex/speex.h>
#include <speex/speex_resampler.h>
#include <mpg123.h>
#include <lame/lame.h>
#include <theora/codec.h>
#include <theora/theoraenc.h>
#include <vpx/vpx_decoder.h>
#include <vpx/vpx_encoder.h>
#include <vpx/vp8cx.h>
#include <vpx/vp8dx.h>
#include <dav1d/dav1d.h>
#include <wavpack/wavpack.h>
#include <iconv.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#define W 64
#define H 48

static int sFailures = 0;

static void
report(const char* lib, const char* version, int ok, const char* what)
{
	printf("%-10s %-24s %s%s%s\n", lib, version, ok ? "ok" : "FAILED",
		what ? ": " : "", what ? what : "");
	if (!ok)
		sFailures++;
}

static void
make_image(uint8_t* rgb)
{
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			uint8_t* p = rgb + (y * W + x) * 3;
			p[0] = x * 4;
			p[1] = y * 5;
			p[2] = (x ^ y) * 3;
		}
}

static void
make_tone(int16_t* pcm, int count, int rate, double hz)
{
	for (int i = 0; i < count; i++)
		pcm[i] = (int16_t)(12000 * sin(2 * M_PI * hz * i / rate));
}

/* -- CPU features ----------------------------------------------------- */

/* One instruction per feature, run in a child: an unsupported one raises
 * SIGILL there. The packages are built for the Apple M1 floor
 * (-march=armv8.4-a+fp16+fp16fml+aes+sha2+sha3); this proves the VM's CPU
 * really offers it. */

static unsigned int sWord;

static void f_neon(void) { __asm__ volatile("add v0.4s, v0.4s, v0.4s" ::: "v0"); }
static void f_fp16(void) { __asm__ volatile("fadd h0, h0, h0" ::: "v0"); }
static void f_fhm(void) { __asm__ volatile("fmlal v0.4s, v1.4h, v2.4h" ::: "v0"); }
static void f_dotprod(void) { __asm__ volatile("sdot v0.4s, v1.16b, v2.16b" ::: "v0"); }
static void f_rdm(void) { __asm__ volatile("sqrdmlah v0.4s, v1.4s, v2.4s" ::: "v0"); }
static void f_crc(void) { __asm__ volatile("crc32w w0, w0, w1" ::: "x0"); }
static void f_lse(void)
{
	__asm__ volatile("mov w1, #1\n\tldadd w1, w2, [%0]" :: "r"(&sWord) : "x1", "x2", "memory");
}
static void f_rcpc(void)
{
	__asm__ volatile("ldapr w1, [%0]" :: "r"(&sWord) : "x1", "memory");
}
static void f_jscvt(void) { __asm__ volatile("fjcvtzs w0, d0" ::: "x0", "cc"); }
static void f_fcma(void) { __asm__ volatile("fcadd v0.4s, v1.4s, v2.4s, #90" ::: "v0"); }
static void f_flagm(void) { __asm__ volatile("cfinv" ::: "cc"); }
static void f_aes(void) { __asm__ volatile("aese v0.16b, v1.16b" ::: "v0"); }
static void f_sha2(void) { __asm__ volatile("sha256h q0, q1, v2.4s" ::: "v0"); }
static void f_sha3(void) { __asm__ volatile("eor3 v0.16b, v1.16b, v2.16b, v3.16b" ::: "v0"); }
static void f_sha512(void) { __asm__ volatile("sha512h q0, q1, v2.2d" ::: "v0"); }
/* beyond the floor (M2 and later): reported, not required */
static void f_i8mm(void)
{
	__asm__ volatile(".arch_extension i8mm\n\tsmmla v0.4s, v1.16b, v2.16b" ::: "v0");
}
static void f_bf16(void)
{
	__asm__ volatile(".arch_extension bf16\n\tbfdot v0.4s, v1.8h, v2.8h" ::: "v0");
}

static void
probe_trapped(int signal)
{
	_exit(2);
}

static int
cpu_has(void (*instruction)(void))
{
	fflush(stdout);
	pid_t child = fork();
	if (child == 0) {
		signal(SIGILL, probe_trapped);
		signal(SIGBUS, probe_trapped);
		signal(SIGSEGV, probe_trapped);
		instruction();
		_exit(0);
	}
	if (child < 0)
		return -1;
	/* Haiku arm64 does not turn an undefined instruction into SIGILL
	 * (do_sync_handler has no EXCP_UNKNOWN case): the child can end up
	 * waiting in the debugger instead. Don't wait for it forever. */
	int status = 0;
	for (int i = 0; i < 300; i++) {
		pid_t done = waitpid(child, &status, WNOHANG);
		if (done == child)
			return WIFEXITED(status) && WEXITSTATUS(status) == 0;
		usleep(10000);
	}
	kill(child, SIGKILL);
	waitpid(child, &status, 0);
	return 0;
}

static void
check_cpu(void)
{
	static const struct {
		const char* name;
		void (*instruction)(void);
	} floor[] = {
		{ "neon", f_neon }, { "fp16", f_fp16 }, { "fhm", f_fhm },
		{ "dotprod", f_dotprod }, { "rdm", f_rdm }, { "crc", f_crc },
		{ "lse", f_lse }, { "rcpc", f_rcpc }, { "jscvt", f_jscvt },
		{ "fcma", f_fcma }, { "flagm", f_flagm }, { "aes", f_aes },
		{ "sha2", f_sha2 }, { "sha3", f_sha3 }, { "sha512", f_sha512 },
	}, beyond[] = {
		{ "i8mm", f_i8mm }, { "bf16", f_bf16 },
	};
	char missing[256] = "", extra[128] = "";
	int count = 0;
	for (size_t i = 0; i < sizeof(floor) / sizeof(floor[0]); i++) {
		if (cpu_has(floor[i].instruction) == 1)
			count++;
		else {
			strcat(missing, " ");
			strcat(missing, floor[i].name);
		}
	}
	for (size_t i = 0; i < sizeof(beyond) / sizeof(beyond[0]); i++) {
		strcat(extra, " ");
		strcat(extra, beyond[i].name);
		strcat(extra, cpu_has(beyond[i].instruction) == 1 ? "+" : "-");
	}
	char what[512];
	if (missing[0] == '\0')
		snprintf(what, sizeof(what), "all %d floor features execute; beyond:%s", count, extra);
	else
		snprintf(what, sizeof(what), "missing:%s", missing);
	report("cpu", "armv8.4-a+M1 floor", missing[0] == '\0', what);
}

/* -- images ------------------------------------------------------------ */

static void
check_png(void)
{
	uint8_t rgb[W * H * 3], back[W * H * 3];
	make_image(rgb);
	png_image image;
	memset(&image, 0, sizeof(image));
	image.version = PNG_IMAGE_VERSION;
	image.width = W;
	image.height = H;
	image.format = PNG_FORMAT_RGB;
	png_alloc_size_t size = 0;
	int ok = png_image_write_to_memory(&image, NULL, &size, 0, rgb, 0, NULL);
	void* buffer = ok ? malloc(size) : NULL;
	ok = ok && png_image_write_to_memory(&image, buffer, &size, 0, rgb, 0, NULL);
	png_image read;
	memset(&read, 0, sizeof(read));
	read.version = PNG_IMAGE_VERSION;
	ok = ok && png_image_begin_read_from_memory(&read, buffer, size);
	read.format = PNG_FORMAT_RGB;
	ok = ok && png_image_finish_read(&read, NULL, back, 0, NULL);
	ok = ok && memcmp(rgb, back, sizeof(rgb)) == 0;
	free(buffer);
	char what[64];
	snprintf(what, sizeof(what), "lossless round trip, %u bytes", (unsigned)size);
	report("libpng", png_get_libpng_ver(NULL), ok, what);
}

static void
check_jpeg(void)
{
	uint8_t rgb[W * H * 3], back[W * H * 3];
	make_image(rgb);
	tjhandle c = tj3Init(TJINIT_COMPRESS);
	tjhandle d = tj3Init(TJINIT_DECOMPRESS);
	unsigned char* jpeg = NULL;
	size_t size = 0;
	int ok = c && d;
	ok = ok && tj3Set(c, TJPARAM_QUALITY, 95) == 0;
	ok = ok && tj3Set(c, TJPARAM_SUBSAMP, TJSAMP_444) == 0;
	ok = ok && tj3Compress8(c, rgb, W, 0, H, TJPF_RGB, &jpeg, &size) == 0;
	ok = ok && tj3DecompressHeader(d, jpeg, size) == 0;
	ok = ok && tj3Get(d, TJPARAM_JPEGWIDTH) == W && tj3Get(d, TJPARAM_JPEGHEIGHT) == H;
	ok = ok && tj3Decompress8(d, jpeg, size, back, 0, TJPF_RGB) == 0;
	double error = 0;
	for (int i = 0; ok && i < W * H * 3; i++)
		error += abs(rgb[i] - back[i]);
	error /= W * H * 3;
	ok = ok && error < 4.0;
	tj3Free(jpeg);
	tj3Destroy(c);
	tj3Destroy(d);
	char what[64];
	snprintf(what, sizeof(what), "q95 round trip, %zu bytes, mean error %.2f", size, error);
	report("libjpeg", "turbojpeg 3", ok, what);
}

static void
check_webp(void)
{
	uint8_t rgb[W * H * 3];
	make_image(rgb);
	uint8_t* out = NULL;
	size_t size = WebPEncodeLosslessRGB(rgb, W, H, W * 3, &out);
	int w = 0, h = 0;
	uint8_t* back = size ? WebPDecodeRGB(out, size, &w, &h) : NULL;
	int ok = back && w == W && h == H && memcmp(rgb, back, sizeof(rgb)) == 0;
	WebPFree(back);
	WebPFree(out);
	char version[32], what[64];
	int v = WebPGetDecoderVersion();
	snprintf(version, sizeof(version), "%d.%d.%d", v >> 16, (v >> 8) & 0xff, v & 0xff);
	snprintf(what, sizeof(what), "lossless round trip, %zu bytes", size);
	report("libwebp", version, ok, what);
}

static void
check_tiff(void)
{
	uint8_t rgb[W * H * 3], back[W * H * 3];
	make_image(rgb);
	const char* path = "/tmp/codec_check.tif";
	int ok = 1;
	TIFF* t = TIFFOpen(path, "w");
	ok = t != NULL;
	if (t) {
		TIFFSetField(t, TIFFTAG_IMAGEWIDTH, W);
		TIFFSetField(t, TIFFTAG_IMAGELENGTH, H);
		TIFFSetField(t, TIFFTAG_BITSPERSAMPLE, 8);
		TIFFSetField(t, TIFFTAG_SAMPLESPERPIXEL, 3);
		TIFFSetField(t, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
		TIFFSetField(t, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
		TIFFSetField(t, TIFFTAG_COMPRESSION, COMPRESSION_LZW);
		TIFFSetField(t, TIFFTAG_ROWSPERSTRIP, H);
		for (int y = 0; ok && y < H; y++)
			ok = TIFFWriteScanline(t, rgb + y * W * 3, y, 0) == 1;
		TIFFClose(t);
	}
	t = ok ? TIFFOpen(path, "r") : NULL;
	ok = t != NULL;
	for (int y = 0; ok && y < H; y++)
		ok = TIFFReadScanline(t, back + y * W * 3, y, 0) == 1;
	if (t)
		TIFFClose(t);
	remove(path);
	ok = ok && memcmp(rgb, back, sizeof(rgb)) == 0;
	char version[16];
	const char* v = TIFFGetVersion();
	const char* number = strstr(v, "Version ");
	number = number ? number + 8 : v;
	snprintf(version, sizeof(version), "%.*s", (int)strcspn(number, "\n"), number);
	report("libtiff", version, ok, "LZW file round trip");
}

static void
check_gif(void)
{
	const char* path = "/tmp/codec_check.gif";
	GifColorType colors[256];
	for (int i = 0; i < 256; i++)
		colors[i].Red = colors[i].Green = colors[i].Blue = i;
	ColorMapObject* map = GifMakeMapObject(256, colors);
	GifByteType line[W];
	int error = 0;
	GifFileType* gif = EGifOpenFileName(path, false, &error);
	int ok = gif && map;
	ok = ok && EGifPutScreenDesc(gif, W, H, 8, 0, map) == GIF_OK;
	ok = ok && EGifPutImageDesc(gif, 0, 0, W, H, false, NULL) == GIF_OK;
	for (int y = 0; ok && y < H; y++) {
		for (int x = 0; x < W; x++)
			line[x] = (x * 3 + y) & 0xff;
		ok = EGifPutLine(gif, line, W) == GIF_OK;
	}
	if (gif)
		ok = EGifCloseFile(gif, &error) == GIF_OK && ok;
	GifFreeMapObject(map);
	GifFileType* in = ok ? DGifOpenFileName(path, &error) : NULL;
	ok = in && DGifSlurp(in) == GIF_OK && in->ImageCount == 1;
	for (int y = 0; ok && y < H; y++)
		for (int x = 0; ok && x < W; x++)
			ok = in->SavedImages[0].RasterBits[y * W + x] == ((x * 3 + y) & 0xff);
	if (in)
		DGifCloseFile(in, &error);
	remove(path);
	char version[16];
	snprintf(version, sizeof(version), "%d.%d.%d", GIFLIB_MAJOR, GIFLIB_MINOR,
		GIFLIB_RELEASE);
	report("giflib", version, ok, "file round trip");
}

static void
check_openjpeg(void)
{
	opj_cparameters_t parameters;
	opj_set_default_encoder_parameters(&parameters);
	opj_codec_t* codec = opj_create_compress(OPJ_CODEC_J2K);
	int ok = codec != NULL && parameters.numresolution > 0;
	if (codec)
		opj_destroy_codec(codec);
	report("openjpeg", opj_version(), ok, "encoder created");
}

static void
check_lcms(void)
{
	cmsHPROFILE srgb = cmsCreate_sRGBProfile();
	cmsHPROFILE lab = cmsCreateLab4Profile(NULL);
	cmsHTRANSFORM t = cmsCreateTransform(srgb, TYPE_RGB_8, lab, TYPE_Lab_DBL,
		INTENT_PERCEPTUAL, 0);
	uint8_t white[3] = { 255, 255, 255 };
	cmsCIELab result = { 0, 0, 0 };
	int ok = t != NULL;
	if (t) {
		cmsDoTransform(t, white, &result, 1);
		cmsDeleteTransform(t);
	}
	cmsCloseProfile(srgb);
	cmsCloseProfile(lab);
	ok = ok && fabs(result.L - 100) < 0.5;
	char version[16], what[64];
	int v = cmsGetEncodedCMMversion();
	snprintf(version, sizeof(version), "%d.%d", v / 1000, (v % 1000) / 10);
	snprintf(what, sizeof(what), "sRGB white -> L*=%.2f", result.L);
	report("lcms2", version, ok, what);
}

/* -- audio and video -------------------------------------------------- */

static void
check_ogg(void)
{
	ogg_stream_state out, in;
	ogg_sync_state sync;
	ogg_page page;
	ogg_packet packet, received;
	char data[] = "prose";
	ogg_stream_init(&out, 42);
	ogg_stream_init(&in, 42);
	ogg_sync_init(&sync);
	memset(&packet, 0, sizeof(packet));
	packet.packet = (unsigned char*)data;
	packet.bytes = sizeof(data);
	packet.b_o_s = 1;
	int ok = ogg_stream_packetin(&out, &packet) == 0 && ogg_stream_flush(&out, &page);
	if (ok) {
		char* buffer = ogg_sync_buffer(&sync, page.header_len + page.body_len);
		memcpy(buffer, page.header, page.header_len);
		memcpy(buffer + page.header_len, page.body, page.body_len);
		ogg_sync_wrote(&sync, page.header_len + page.body_len);
		ok = ogg_sync_pageout(&sync, &page) == 1 && ogg_stream_pagein(&in, &page) == 0
			&& ogg_stream_packetout(&in, &received) == 1
			&& received.bytes == sizeof(data) && memcmp(received.packet, data, sizeof(data)) == 0;
	}
	ogg_stream_clear(&out);
	ogg_stream_clear(&in);
	ogg_sync_clear(&sync);
	report("libogg", "-", ok, "page round trip");
}

static void
check_vorbis(void)
{
	vorbis_info info;
	vorbis_info_init(&info);
	int ok = vorbis_encode_init_vbr(&info, 1, 44100, 0.4f) == 0;
	vorbis_info_clear(&info);
	report("libvorbis", vorbis_version_string(), ok, "VBR encoder set up");
}

struct flac_buffer {
	uint8_t data[1 << 20];
	size_t size, read;
	int32_t samples[44100];
	unsigned decoded;
};

static FLAC__StreamEncoderWriteStatus
flac_write(const FLAC__StreamEncoder* e, const FLAC__byte buffer[], size_t bytes,
	uint32_t samples, uint32_t frame, void* cookie)
{
	struct flac_buffer* b = cookie;
	if (b->size + bytes > sizeof(b->data))
		return FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR;
	memcpy(b->data + b->size, buffer, bytes);
	b->size += bytes;
	return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
}

static FLAC__StreamDecoderReadStatus
flac_read(const FLAC__StreamDecoder* d, FLAC__byte buffer[], size_t* bytes, void* cookie)
{
	struct flac_buffer* b = cookie;
	size_t n = b->size - b->read < *bytes ? b->size - b->read : *bytes;
	memcpy(buffer, b->data + b->read, n);
	b->read += n;
	*bytes = n;
	return n ? FLAC__STREAM_DECODER_READ_STATUS_CONTINUE
		: FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
}

static FLAC__StreamDecoderWriteStatus
flac_decoded(const FLAC__StreamDecoder* d, const FLAC__Frame* frame,
	const FLAC__int32* const buffer[], void* cookie)
{
	struct flac_buffer* b = cookie;
	for (unsigned i = 0; i < frame->header.blocksize && b->decoded < 44100; i++)
		b->samples[b->decoded++] = buffer[0][i];
	return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void
flac_error(const FLAC__StreamDecoder* d, FLAC__StreamDecoderErrorStatus s, void* cookie)
{
}

static void
check_flac(void)
{
	static struct flac_buffer b;
	static int16_t tone[44100];
	static int32_t pcm[44100];
	make_tone(tone, 44100, 44100, 440);
	for (int i = 0; i < 44100; i++)
		pcm[i] = tone[i];
	FLAC__StreamEncoder* e = FLAC__stream_encoder_new();
	FLAC__stream_encoder_set_channels(e, 1);
	FLAC__stream_encoder_set_bits_per_sample(e, 16);
	FLAC__stream_encoder_set_sample_rate(e, 44100);
	FLAC__stream_encoder_set_compression_level(e, 5);
	int ok = FLAC__stream_encoder_init_stream(e, flac_write, NULL, NULL, NULL, &b)
		== FLAC__STREAM_ENCODER_INIT_STATUS_OK;
	const FLAC__int32* channels[1] = { pcm };
	ok = ok && FLAC__stream_encoder_process(e, channels, 44100);
	ok = FLAC__stream_encoder_finish(e) && ok;
	FLAC__stream_encoder_delete(e);
	FLAC__StreamDecoder* d = FLAC__stream_decoder_new();
	ok = ok && FLAC__stream_decoder_init_stream(d, flac_read, NULL, NULL, NULL, NULL,
		flac_decoded, NULL, flac_error, &b) == FLAC__STREAM_DECODER_INIT_STATUS_OK;
	ok = ok && FLAC__stream_decoder_process_until_end_of_stream(d);
	FLAC__stream_decoder_delete(d);
	ok = ok && b.decoded == 44100 && memcmp(b.samples, pcm, sizeof(pcm)) == 0;
	char what[64];
	snprintf(what, sizeof(what), "lossless 1 s round trip, %zu bytes", b.size);
	report("libFLAC", FLAC__VERSION_STRING, ok, what);
}

static void
check_opus(void)
{
	int16_t tone[960], back[960];
	unsigned char packet[1500];
	make_tone(tone, 960, 48000, 440);
	int error = 0;
	OpusEncoder* e = opus_encoder_create(48000, 1, OPUS_APPLICATION_AUDIO, &error);
	OpusDecoder* d = opus_decoder_create(48000, 1, &error);
	int ok = e && d;
	int size = ok ? opus_encode(e, tone, 960, packet, sizeof(packet)) : -1;
	int samples = size > 0 ? opus_decode(d, packet, size, back, 960, 0) : -1;
	ok = ok && size > 0 && samples == 960;
	if (e)
		opus_encoder_destroy(e);
	if (d)
		opus_decoder_destroy(d);
	char what[64];
	snprintf(what, sizeof(what), "20 ms frame -> %d bytes -> %d samples", size, samples);
	report("libopus", opus_get_version_string(), ok, what);
}

static void
check_speex(void)
{
	const char* version = "?";
	speex_lib_ctl(SPEEX_LIB_GET_VERSION_STRING, &version);
	int error = 0;
	SpeexResamplerState* r = speex_resampler_init(1, 44100, 48000, 5, &error);
	int16_t in[441], out[600];
	spx_uint32_t inLength = 441, outLength = 600;
	make_tone(in, 441, 44100, 440);
	int ok = r && speex_resampler_process_int(r, 0, in, &inLength, out, &outLength) == 0
		&& inLength == 441 && outLength > 470;
	if (r)
		speex_resampler_destroy(r);
	char what[64];
	snprintf(what, sizeof(what), "speexdsp resampled 441 -> %u samples", (unsigned)outLength);
	report("speex", version, ok, what);
}

static void
check_mp3(void)
{
	static int16_t tone[44100];
	static unsigned char mp3[64 * 1024];
	make_tone(tone, 44100, 44100, 440);
	lame_global_flags* lame = lame_init();
	lame_set_num_channels(lame, 1);
	lame_set_in_samplerate(lame, 44100);
	lame_set_brate(lame, 128);
	lame_set_mode(lame, MONO);
	int ok = lame_init_params(lame) == 0;
	int size = ok ? lame_encode_buffer(lame, tone, tone, 44100, mp3, sizeof(mp3)) : -1;
	if (size >= 0)
		size += lame_encode_flush(lame, mp3 + size, sizeof(mp3) - size);
	lame_close(lame);
	report("lame", get_lame_version(), ok && size > 0, "1 s tone encoded");

	/* decode what lame made */
	unsigned major = 0, minor = 0, patch = 0;
	const char* version = mpg123_distversion(&major, &minor, &patch);
	mpg123_handle* m = mpg123_new(NULL, NULL);
	ok = m && mpg123_open_feed(m) == MPG123_OK && mpg123_feed(m, mp3, size) == MPG123_OK;
	size_t decoded = 0, got = 0;
	unsigned char pcm[16384];
	int result;
	while (ok && (result = mpg123_read(m, pcm, sizeof(pcm), &got)) != MPG123_NEED_MORE) {
		if (result == MPG123_NEW_FORMAT)
			continue;
		if (result != MPG123_OK && result != MPG123_DONE)
			break;
		decoded += got / 2;
		if (result == MPG123_DONE)
			break;
	}
	if (m)
		mpg123_delete(m);
	ok = ok && decoded > 40000;
	char what[64];
	snprintf(what, sizeof(what), "decoded lame's %d bytes -> %zu samples", size, decoded);
	report("mpg123", version, ok, what);
}

static void
check_theora(void)
{
	th_info info;
	th_info_init(&info);
	info.frame_width = 64;
	info.frame_height = 48;
	info.pic_width = 64;
	info.pic_height = 48;
	info.fps_numerator = 25;
	info.fps_denominator = 1;
	info.quality = 32;
	info.pixel_fmt = TH_PF_420;
	info.colorspace = TH_CS_UNSPECIFIED;
	info.keyframe_granule_shift = 6;
	th_enc_ctx* encoder = th_encode_alloc(&info);
	int ok = encoder != NULL;
	if (encoder)
		th_encode_free(encoder);
	th_info_clear(&info);
	report("libtheora", th_version_string(), ok, "encoder allocated");
}

static int
vpx_drain(vpx_codec_ctx_t* encoder, vpx_codec_ctx_t* decoder, int* frames)
{
	/* every vpx_codec_encode() call replaces the pending output: drain it */
	const vpx_codec_cx_pkt_t* packet;
	vpx_codec_iter_t iter = NULL;
	while ((packet = vpx_codec_get_cx_data(encoder, &iter)) != NULL) {
		if (packet->kind != VPX_CODEC_CX_FRAME_PKT)
			continue;
		if (vpx_codec_decode(decoder, packet->data.frame.buf, packet->data.frame.sz,
				NULL, 0) != VPX_CODEC_OK)
			return 0;
		vpx_codec_iter_t diter = NULL;
		vpx_image_t* decoded = vpx_codec_get_frame(decoder, &diter);
		if (decoded == NULL || decoded->d_w != W || decoded->d_h != H)
			return 0;
		(*frames)++;
	}
	return 1;
}

static void
check_vpx(void)
{
	vpx_codec_ctx_t encoder, decoder;
	vpx_codec_enc_cfg_t config;
	vpx_image_t image;
	const char* step = "config";
	int frames = 0;
	int ok = vpx_codec_enc_config_default(vpx_codec_vp8_cx(), &config, 0) == VPX_CODEC_OK;
	config.g_w = W;
	config.g_h = H;
	if (ok && (step = "image", vpx_img_alloc(&image, VPX_IMG_FMT_I420, W, H, 1) == NULL))
		ok = 0;
	if (ok)
		memset(image.img_data, 128, W * H * 3 / 2);
	ok = ok && (step = "encoder", vpx_codec_enc_init(&encoder, vpx_codec_vp8_cx(),
		&config, 0) == VPX_CODEC_OK);
	ok = ok && (step = "decoder", vpx_codec_dec_init(&decoder, vpx_codec_vp8_dx(),
		NULL, 0) == VPX_CODEC_OK);
	ok = ok && (step = "encode", vpx_codec_encode(&encoder, &image, 0, 1,
		VPX_EFLAG_FORCE_KF, VPX_DL_GOOD_QUALITY) == VPX_CODEC_OK);
	ok = ok && (step = "decode", vpx_drain(&encoder, &decoder, &frames));
	ok = ok && (step = "flush", vpx_codec_encode(&encoder, NULL, 0, 1, 0,
		VPX_DL_GOOD_QUALITY) == VPX_CODEC_OK);
	ok = ok && (step = "decode", vpx_drain(&encoder, &decoder, &frames));
	ok = ok && (step = "frame count", frames == 1);
	char what[96];
	if (ok)
		snprintf(what, sizeof(what), "VP8 key frame round trip");
	else
		snprintf(what, sizeof(what), "VP8 round trip failed at %s (%d frames)", step, frames);
	vpx_codec_destroy(&encoder);
	vpx_codec_destroy(&decoder);
	vpx_img_free(&image);
	report("libvpx", vpx_codec_version_str(), ok, what);
}


static void
check_dav1d(void)
{
	Dav1dSettings settings;
	Dav1dContext* context = NULL;
	dav1d_default_settings(&settings);
	settings.n_threads = 2;
	int ok = dav1d_open(&context, &settings) == 0;
	if (context)
		dav1d_close(&context);
	report("dav1d", dav1d_version(), ok, "decoder opened");
}

static void
check_wavpack(void)
{
	report("wavpack", WavpackGetLibraryVersionString(), 1, NULL);
}

static void
check_iconv(void)
{
	iconv_t cd = iconv_open("UTF-8", "ISO-8859-1");
	char latin1[] = "Caf\xe9", utf8[16] = { 0 };
	char* in = latin1;
	char* out = utf8;
	size_t inLeft = 4, outLeft = sizeof(utf8) - 1;
	int ok = cd != (iconv_t)-1 && iconv(cd, &in, &inLeft, &out, &outLeft) != (size_t)-1
		&& strcmp(utf8, "Caf\xc3\xa9") == 0;
	if (cd != (iconv_t)-1)
		iconv_close(cd);
	char version[16];
	snprintf(version, sizeof(version), "%d.%d", _libiconv_version >> 8,
		_libiconv_version & 0xff);
	report("libiconv", version, ok, "Latin-1 -> UTF-8");
}

int
main(void)
{
	printf("codec_check: codec libraries on this system\n\n");
	check_cpu();
	check_png();
	check_jpeg();
	check_webp();
	check_tiff();
	check_gif();
	check_openjpeg();
	check_lcms();
	check_ogg();
	check_vorbis();
	check_flac();
	check_opus();
	check_speex();
	check_mp3();
	check_theora();
	check_vpx();
	check_dav1d();
	check_wavpack();
	check_iconv();
	printf("\n%d check(s) failed\n", sFailures);
	return sFailures;
}
