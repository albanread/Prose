// 07-game: a game pane. The window's pixels are palette indices, written
// straight into memory the Mac's GPU reads, and a fragment function of our own
// runs over the top of them.
//
// Left and right steer, space drops a star. Build with "make" and run it.

#include <Application.h>
#include <GamePane.h>
#include <View.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static const uint32 kWidth = 256;
static const uint32 kHeight = 192;

enum { kInk = 1, kFloor = 16, kStar = 17, kShip = 1 };	// kShip: a sprite palette


class Game : public BGamePane {
public:
	Game();
	virtual ~Game();

	virtual bool QuitRequested();
	void Key(int32 key);
	void Loop();

private:
	thread_id	fThread;
	bool		fQuit;
	int32		fShape;
	float		fX;
	float		fStep;
	float		fStars[16];
};


class Keys : public BView {
public:
	Keys(BRect frame) : BView(frame, "keys", B_FOLLOW_ALL, B_WILL_DRAW)
		{ SetViewColor(0, 0, 0); }
	virtual void AttachedToWindow() { MakeFocus(true); }
	virtual void KeyDown(const char* bytes, int32 count)
	{
		if (count == 1)
			((Game*)Window())->Key(bytes[0]);
		else
			BView::KeyDown(bytes, count);
	}
};


static int32
loop(void* game)
{
	((Game*)game)->Loop();
	return 0;
}


Game::Game()
	:
	// two buffers, so we draw one while the GPU reads the other; the
	// per-scanline palette gives us a sky that changes colour by the row
	BGamePane(BRect(0, 0, kWidth * 2 - 1, kHeight * 2 - 1), "Game",
		kWidth, kHeight, 2, B_GAME_PANE_SCANLINE_PALETTE),
	fThread(-1),
	fQuit(false),
	fShape(-1),
	fX(kWidth / 2),
	fStep(0)
{
	AddChild(new Keys(Bounds()));
	CenterOnScreen();
	if (InitCheck() != B_OK) {
		fprintf(stderr, "no game pane here: %s\n", strerror(InitCheck()));
		return;
	}

	SetEffect(B_GAME_PANE_CRT);

	// The sky: one index, its colour coming from the row. A gradient like this
	// costs 192 palette writes once, not a pixel of drawing ever.
	for (uint32 y = 0; y < kHeight; y++) {
		float k = (float)y / kHeight;
		rgb_color c = { (uint8)(10 + k * 40), (uint8)(10 + k * 30),
			(uint8)(40 + k * 90), 255 };
		SetScanlineColor(y, kInk, c);
	}
	rgb_color floor = { 40, 90, 60, 255 };
	rgb_color star = { 255, 240, 180, 255 };
	SetColor(kFloor, floor);
	SetColor(kStar, star);

	// A sprite palette of its own, so the ship costs the world no colours.
	for (uint32 i = 1; i < 16; i++) {
		float k = (float)i / 15.0f;
		rgb_color c = { (uint8)(200 - k * 40), (uint8)(60 + k * 170),
			(uint8)(90 + k * 140), 255 };
		SetSpriteColor(kShip, i, c);
	}

	// A sixteen-colour shape: one byte a pixel going in, packed by the kit.
	uint8 pixels[12 * 12];
	memset(pixels, 0, sizeof(pixels));
	for (uint32 y = 0; y < 12; y++) {
		uint32 half = y / 2 + 1;
		for (uint32 x = 6 - half; x < 6 + half && x < 12; x++)
			pixels[y * 12 + x] = 1 + (y * 14) / 11;
	}
	fShape = DefineSprite(pixels, 12, 12, 4);

	// And a filter over the lot: the picture bent a little, and brighter
	// pixels bleeding into their neighbours.
	const char* overlay =
		"float4 overlay(float4 colour, pane p)\n"
		"{\n"
		"    float2 d = p.uv * 2.0 - 1.0;\n"
		"    float2 uv = p.uv + d * dot(d, d) * 0.012;\n"
		"    float4 c = p.smooth(uv);\n"
		"    float4 glow = p.colour(uv + float2(0.01, 0.0))\n"
		"                + p.colour(uv - float2(0.01, 0.0));\n"
		"    c.rgb += glow.rgb * 0.12;\n"
		"    return c;\n"
		"}\n";
	if (SetOverlayShader(overlay) != B_OK)
		fprintf(stderr, "the overlay would not compile:\n%s\n", ShaderError());

	for (uint32 i = 0; i < 16; i++)
		fStars[i] = -1.0f;

	fThread = spawn_thread(loop, "game", B_DISPLAY_PRIORITY, this);
	if (fThread >= 0)
		resume_thread(fThread);
}


Game::~Game()
{
	fQuit = true;
	if (fThread >= 0)
		wait_for_thread(fThread, NULL);
}


bool
Game::QuitRequested()
{
	fQuit = true;
	if (fThread >= 0) {
		wait_for_thread(fThread, NULL);
		fThread = -1;
	}
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


void
Game::Key(int32 key)
{
	switch (key) {
		case B_LEFT_ARROW:	fStep = -2.5f; break;
		case B_RIGHT_ARROW:	fStep = 2.5f; break;
		case ' ':
			for (uint32 i = 0; i < 16; i++) {
				if (fStars[i] < 0.0f) { fStars[i] = 0.0f; break; }
			}
			break;
		case B_ESCAPE: PostMessage(B_QUIT_REQUESTED); break;
	}
}


void
Game::Loop()
{
	while (!fQuit) {
		// the sky, then the ground: one byte a pixel, and the fastest way to
		// put a byte somewhere is to put it there
		Clear(kInk);
		FillRect(0, kHeight - 24, kWidth, 24, kFloor);

		for (uint32 i = 0; i < 16; i++) {
			if (fStars[i] < 0.0f)
				continue;
			int32 y = (int32)(kHeight - 32 - fStars[i]);
			FillRect((int32)fX + 5, y, 2, 4, kStar);
			fStars[i] += 3.0f;
			if (y < 0)
				fStars[i] = -1.0f;
		}

		fX += fStep;
		fStep *= 0.85f;
		if (fX < 0) fX = 0;
		if (fX > kWidth - 24) fX = kWidth - 24;

		ClearSprites();
		if (fShape >= 0)
			DrawSprite(fShape, (int32)fX, kHeight - 48, 2.0f, 0.0f, 1.0f, kShip);

		Present();
		if (WaitForRetrace() != B_OK)
			snooze(16000);
	}
}


int
main()
{
	BApplication app("application/x-vnd.Prose-GameExample");
	Game* game = new Game();
	if (game->InitCheck() != B_OK) {
		game->Quit();
		return 1;
	}
	game->Show();
	app.Run();
	return 0;
}
