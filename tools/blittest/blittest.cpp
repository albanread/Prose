// app_server's copies and fills, through the real server. A BView drawing into
// a BBitmap goes through the same Painter and DrawingEngine code as one on
// screen: FillRect ends in gfxset32, FillRegion in FillRectNoClipping and
// gfxset32, CopyBits in _CopyRect's memcpy and memmove, and DrawBitmap in
// DrawBitmapNoScale's memcpy. Each check draws, reads the bitmap back, and
// compares every pixel with what it has to be. Patches 0130 and 0131 changed
// every one of those instructions; this is their regression test.
//
// Build and run it in the guest:
//
//	clang++ -O2 -Wall -Wextra -o blittest blittest.cpp -lbe && ./blittest
#include <stdio.h>
#include <string.h>

#include <Application.h>
#include <Bitmap.h>
#include <Region.h>
#include <View.h>


static const int32 kWidth = 400;
static const int32 kHeight = 1200;
static const uint32 kBackground = 0xffa0b0c0;
static const uint32 kColor = 0xff123456;
static const rgb_color kHighColor = { 0x12, 0x34, 0x56, 0xff };

static int sPassed;
static int sTotal;


static void
report(bool ok, const char* what)
{
	sTotal++;
	if (ok)
		sPassed++;
	printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
}


class Canvas {
public:
	Canvas()
		:
		fBitmap(new BBitmap(BRect(0, 0, kWidth - 1, kHeight - 1),
			B_BITMAP_ACCEPTS_VIEWS, B_RGB32)),
		fView(new BView(fBitmap->Bounds(), "canvas", B_FOLLOW_NONE, 0))
	{
		fBitmap->AddChild(fView);
		fModel = new uint32[kWidth * kHeight];
	}

	~Canvas()
	{
		delete fBitmap;
		delete[] fModel;
	}

	bool InitCheck() const
	{
		return fBitmap->InitCheck() == B_OK && fBitmap->IsValid();
	}

	BView* View() const { return fView; }
	uint32* Model() const { return fModel; }

	uint32* Row(int32 y) const
	{
		return reinterpret_cast<uint32*>(static_cast<uint8*>(fBitmap->Bits())
			+ y * fBitmap->BytesPerRow());
	}

	// Paints the bitmap and the model alike, before a check draws.
	void Paint(bool distinct)
	{
		fBitmap->Lock();
		fView->Sync();
		for (int32 y = 0; y < kHeight; y++) {
			for (int32 x = 0; x < kWidth; x++) {
				uint32 pixel = distinct
					? 0xff000000 | static_cast<uint32>(y << 12) | x : kBackground;
				Row(y)[x] = pixel;
				fModel[y * kWidth + x] = pixel;
			}
		}
		fBitmap->Unlock();
	}

	// The number of pixels that differ from the model once the server is done.
	int32 Differences(const char* what)
	{
		fBitmap->Lock();
		fView->Sync();
		int32 differences = 0;
		for (int32 y = 0; y < kHeight; y++) {
			for (int32 x = 0; x < kWidth; x++) {
				if (Row(y)[x] != fModel[y * kWidth + x]) {
					if (differences++ < 3) {
						printf("  %s: (%" B_PRId32 ", %" B_PRId32 ") is %08" B_PRIx32
							", should be %08" B_PRIx32 "\n", what, x, y, Row(y)[x],
							fModel[y * kWidth + x]);
					}
				}
			}
		}
		fBitmap->Unlock();
		return differences;
	}

	void Lock() { fBitmap->Lock(); }
	void Unlock() { fBitmap->Unlock(); }

private:
	BBitmap*	fBitmap;
	BView*		fView;
	uint32*		fModel;
};


// FillRect in the high colour, B_OP_COPY: Painter::FillRect, gfxset32 per row.
// One rectangle a row, every width from 1 to 140 pixels at every x from 0 to
// 7, then wide ones.
static bool
check_fill_rect(Canvas& canvas)
{
	canvas.Paint(false);
	canvas.Lock();
	BView* view = canvas.View();
	view->SetDrawingMode(B_OP_COPY);
	view->SetHighColor(kHighColor);

	int32 y = 0;
	for (int32 width = 1; width <= 140; width++) {
		for (int32 x = 0; x < 8; x++, y++) {
			view->FillRect(BRect(x, y, x + width - 1, y));
			for (int32 i = x; i < x + width; i++)
				canvas.Model()[y * kWidth + i] = kColor;
		}
	}
	static const int32 kWide[] = { 200, 255, 256, 257, 391 };
	for (int32 width : kWide) {
		for (int32 x = 0; x < 4; x++, y++) {
			view->FillRect(BRect(x, y, x + width - 1, y));
			for (int32 i = x; i < x + width; i++)
				canvas.Model()[y * kWidth + i] = kColor;
		}
	}
	// and a tall one, which is one call for many rows
	view->FillRect(BRect(3, y, 300, y + 49));
	for (int32 row = y; row < y + 50; row++) {
		for (int32 i = 3; i <= 300; i++)
			canvas.Model()[row * kWidth + i] = kColor;
	}
	canvas.Unlock();

	return canvas.Differences("FillRect") == 0;
}


// FillRegion: DrawingEngine::FillRegion, FillRectNoClipping per rectangle.
static bool
check_fill_region(Canvas& canvas)
{
	canvas.Paint(false);
	BRegion region;
	int32 y = 0;
	for (int32 width = 1; width <= 70; width++, y += 2) {
		int32 x = (width * 7) % 13;
		region.Include(BRect(x, y, x + width - 1, y));
	}

	canvas.Lock();
	BView* view = canvas.View();
	view->SetDrawingMode(B_OP_COPY);
	view->SetHighColor(kHighColor);
	view->FillRegion(&region);
	canvas.Unlock();

	for (int32 i = 0; i < region.CountRects(); i++) {
		clipping_rect rect = region.RectAtInt(i);
		for (int32 row = rect.top; row <= rect.bottom; row++) {
			for (int32 x = rect.left; x <= rect.right; x++)
				canvas.Model()[row * kWidth + x] = kColor;
		}
	}
	return canvas.Differences("FillRegion") == 0;
}


static void
model_copy(Canvas& canvas, BRect source, int32 dx, int32 dy)
{
	int32 width = source.IntegerWidth() + 1;
	int32 height = source.IntegerHeight() + 1;
	uint32* saved = new uint32[width * height];
	for (int32 y = 0; y < height; y++) {
		for (int32 x = 0; x < width; x++) {
			saved[y * width + x] = canvas.Model()[(int32(source.top) + y) * kWidth
				+ int32(source.left) + x];
		}
	}
	for (int32 y = 0; y < height; y++) {
		for (int32 x = 0; x < width; x++) {
			canvas.Model()[(int32(source.top) + y + dy) * kWidth
				+ int32(source.left) + x + dx] = saved[y * width + x];
		}
	}
	delete[] saved;
}


// CopyBits within a row: DrawingEngine::_CopyRect, memmove to the right and
// memcpy, over its own source, to the left. Every length from 1 to 64 pixels,
// then long ones, each at ten distances.
static bool
check_copy_bits_sideways(Canvas& canvas)
{
	canvas.Paint(true);
	static const int32 kDistances[] = { -9, -5, -3, -2, -1, 1, 2, 3, 5, 9 };
	static const int32 kLong[] = { 100, 150, 200, 301 };

	canvas.Lock();
	BView* view = canvas.View();
	int32 y = 0;
	for (int32 length = 1; length <= 64 + 4; length++) {
		int32 pixels = length <= 64 ? length : kLong[length - 65];
		for (int32 dx : kDistances) {
			int32 left = 20 + y % 8;
			BRect source(left, y, left + pixels - 1, y);
			BRect destination = source.OffsetByCopy(dx, 0);
			view->CopyBits(source, destination);
			model_copy(canvas, source, dx, 0);
			y++;
		}
	}
	canvas.Unlock();

	return canvas.Differences("CopyBits sideways") == 0;
}


// CopyBits between rows, straight and diagonally: memcpy row by row, bottom
// up or top down as the direction needs.
static bool
check_copy_bits_vertical(Canvas& canvas)
{
	canvas.Paint(true);
	static const int32 kMoves[][2] = { { 0, 1 }, { 0, -1 }, { 0, 3 },
		{ 1, 1 }, { -1, 1 }, { 1, -2 }, { -3, -1 }, { 5, 2 } };

	canvas.Lock();
	BView* view = canvas.View();
	int32 top = 10;
	for (const int32* move : kMoves) {
		BRect source(10 + top % 7, top, 300, top + 19);
		view->CopyBits(source, source.OffsetByCopy(move[0], move[1]));
		model_copy(canvas, source, move[0], move[1]);
		top += 40;
	}
	canvas.Unlock();

	return canvas.Differences("CopyBits between rows") == 0;
}


// DrawBitmap, B_OP_COPY, unscaled: DrawBitmapNoScale's memcpy per row. The
// bitmap is 37 pixels wide, so its rows alternate between 8-byte alignments,
// and it is drawn at odd and even x.
static bool
check_draw_bitmap(Canvas& canvas)
{
	canvas.Paint(false);
	BBitmap source(BRect(0, 0, 36, 23), B_RGB32);
	if (source.InitCheck() != B_OK)
		return false;
	for (int32 y = 0; y < 24; y++) {
		uint32* row = reinterpret_cast<uint32*>(static_cast<uint8*>(source.Bits())
			+ y * source.BytesPerRow());
		for (int32 x = 0; x < 37; x++)
			row[x] = 0xff000000 | static_cast<uint32>((y + 1) << 12) | (x + 1);
	}

	static const int32 kX[] = { 0, 1, 2, 3, 4, 5, 7, 13, 250, 251 };
	canvas.Lock();
	BView* view = canvas.View();
	view->SetDrawingMode(B_OP_COPY);
	int32 top = 0;
	for (int32 x : kX) {
		view->DrawBitmap(&source, BPoint(x, top));
		for (int32 y = 0; y < 24; y++) {
			for (int32 i = 0; i < 37; i++) {
				canvas.Model()[(top + y) * kWidth + x + i]
					= 0xff000000 | static_cast<uint32>((y + 1) << 12) | (i + 1);
			}
		}
		top += 30;
	}
	canvas.Unlock();

	return canvas.Differences("DrawBitmap") == 0;
}


int
main()
{
	BApplication application("application/x-vnd.Prose-blittest");
	Canvas canvas;
	if (!canvas.InitCheck()) {
		printf("FAIL: no bitmap to draw in\n");
		return 1;
	}

	report(check_fill_rect(canvas),
		"FillRect, 1-140 pixels at x 0-7, wide and tall ones");
	report(check_fill_region(canvas), "FillRegion, 70 rectangles at odd x");
	report(check_copy_bits_sideways(canvas),
		"CopyBits sideways, 1-64 pixels and long runs, 10 distances");
	report(check_copy_bits_vertical(canvas),
		"CopyBits between rows, straight and diagonal");
	report(check_draw_bitmap(canvas),
		"DrawBitmap B_OP_COPY, 37 pixels wide, at odd and even x");

	printf("SELFTEST %s %d/%d\n", sPassed == sTotal ? "PASS" : "FAIL", sPassed,
		sTotal);
	return sPassed == sTotal ? 0 : 1;
}
