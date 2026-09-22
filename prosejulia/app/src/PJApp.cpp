// PJApp — ProseJulia: an animated Julia fractal for Prose. c travels
// the classic 0.7885·e^{iθ} orbit; the picture is escape-time smooth
// colouring. Space pauses, +/− zoom, arrows pan, menus for palette,
// speed and detail. Scripting is the harness's eye (Frame, Pixel).
#include <Application.h>
#include <Bitmap.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <Directory.h>
#include <Path.h>
#include <File.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Message.h>
#include <PropertyInfo.h>
#include <Screen.h>
#include <ScrollView.h>
#include <String.h>
#include <Window.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <zlib.h>

#include "PJFractal.h"

// ---------------------------------------------------------------- PDF --
// the postcard: the current frame as one true-size image page
// (the writer pattern every sibling proved)
static bool
PjWriteAll(BFile* file, const char* data, ssize_t length)
{
	ssize_t written = 0;
	while (written < length) {
		ssize_t n = file->Write(data + written, length - written);
		if (n <= 0)
			return false;
		written += n;
	}
	return true;
}

static status_t
PjWritePDF(const BBitmap* image, float pageW, float pageH, const char* path)
{
	if (image == NULL || path == NULL || path[0] == '\0')
		return B_BAD_VALUE;
	int32 w = image->Bounds().IntegerWidth() + 1;
	int32 h = image->Bounds().IntegerHeight() + 1;
	uint32 bpr = image->BytesPerRow();
	const uint8* bits = (const uint8*)image->Bits();
	// RGB rows, Flate-compressed
	std::vector<uint8> rgb((size_t)w * h * 3);
	for (int32 y = 0; y < h; y++) {
		const uint8* src = bits + (size_t)y * bpr;
		uint8* dst = rgb.data() + (size_t)y * w * 3;
		for (int32 x = 0; x < w; x++) {
			dst[x * 3 + 0] = src[x * 4 + 2];
			dst[x * 3 + 1] = src[x * 4 + 1];
			dst[x * 3 + 2] = src[x * 4 + 0];
		}
	}
	// zlib here (Flate)
	uLongf flateLen = compressBound((uLong)rgb.size());
	std::vector<uint8> flate(flateLen);
	if (compress2((Bytef*)flate.data(), &flateLen,
			(const Bytef*)rgb.data(), (uLong)rgb.size(), 6) != Z_OK)
		return B_ERROR;
	flate.resize(flateLen);

	BFile file;
	status_t err = file.SetTo(path,
		B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (err != B_OK)
		return err;
	off_t pos = 0;
	off_t offsets[5];
	auto write = [&](const char* data, ssize_t len) -> status_t {
		if (!PjWriteAll(&file, data, len))
			return B_ERROR;
		pos += len;
		return B_OK;
	};
	auto writeStr = [&](const BString& s) -> status_t {
		return write(s.String(), s.Length());
	};
	const char header[] = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
	err = write(header, (ssize_t)strlen(header));
	if (err == B_OK) {
		offsets[0] = pos;
		err = writeStr("1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\n"
			"endobj\n");
	}
	if (err == B_OK) {
		offsets[1] = pos;
		err = writeStr("2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1"
			" >>\nendobj\n");
	}
	if (err == B_OK) {
		offsets[2] = pos;
		BString page;
		page << "3 0 obj\n<< /Type /Page /Parent 2 0 R\n"
			<< "  /MediaBox [0 0 " << (int)pageW << " " << (int)pageH
			<< "]\n"
			<< "  /Resources << /XObject << /Im0 5 0 R >> >>\n"
			<< "  /Contents 4 0 R >>\nendobj\n";
		err = writeStr(page);
	}
	if (err == B_OK) {
		offsets[3] = pos;
		BString content;
		content << "q " << (int)pageW << " 0 0 " << (int)pageH
			<< " 0 0 cm /Im0 Do Q\n";
		BString obj;
		obj << "4 0 obj\n<< /Length " << content.Length()
			<< " >>\nstream\n" << content << "endstream\nendobj\n";
		err = writeStr(obj);
	}
	if (err == B_OK) {
		offsets[4] = pos;
		BString head;
		head << "5 0 obj\n<< /Type /XObject /Subtype /Image /Width " << w
			<< " /Height " << h
			<< " /ColorSpace /DeviceRGB /BitsPerComponent 8"
			<< " /Filter /FlateDecode /Length " << (ssize_t)flate.size()
			<< " >>\nstream\n";
		err = writeStr(head);
		if (err == B_OK)
			err = write((const char*)flate.data(),
				(ssize_t)flate.size());
		if (err == B_OK)
			err = writeStr("\nendstream\nendobj\n");
	}
	if (err == B_OK) {
		off_t xrefAt = pos;
		BString xref;
		xref << "xref\n0 6\n0000000000 65535 f \n";
		char line[24];
		for (int i = 0; i < 5; i++) {
			snprintf(line, sizeof(line), "%010lld 00000 n \n",
				(long long)offsets[i]);
			xref << line;
		}
		xref << "trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n"
			<< (long long)xrefAt << "\n%%EOF\n";
		err = writeStr(xref);
	}
	file.Unset();
	if (err != B_OK) {
		BEntry corpse;
		if (corpse.SetTo(path) == B_OK)
			corpse.Remove();
	}
	return err;
}

// ---------------------------------------------------------------- view --
class PJView : public BView {
public:
			PJView(BRect frame);

	void	Draw(BRect updateRect) override;
	void	KeyDown(const char* bytes, int32 numBytes) override;
	void	Pulse() override;
	void	MakeFocus(bool focus = true) override;
	void	AttachedToWindow() override;

	PJFractal&	Fractal() { return fFrac; }
	bool	Paused() const { return fPaused; }
	void	SetPaused(bool paused);
	double	Speed() const { return fSpeed; }		// deg per frame
	void	SetSpeed(double deg) { fSpeed = deg; }
	int32	FrameSize() const { return fRenderW; }

	// render now (full detail) — after any parameter change
	void	RenderFull();

	// read a pixel from the rendered bitmap into a reply message;
	// false when the coordinates are outside the render
	bool	PixelAt(int32 x, int32 y, BMessage* reply);

private:
	void	RenderInto(int32 w, int32 h);
	void	UpdateTitle();

	PJFractal	fFrac;
	bool		fPaused = false;
	double		fSpeed = 1.0;			// degrees of θ per frame
	BBitmap*	fBitmap = NULL;
	int32		fRenderW = 0, fRenderH = 0;
	bool		fLowRes = false;
};

PJView::PJView(BRect frame)
	:
	BView(frame, "julia", B_FOLLOW_ALL_SIDES,
		B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE | B_PULSE_NEEDED)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	SetLowColor(0, 0, 0);
}

void
PJView::AttachedToWindow()
{
	BView::AttachedToWindow();
	if (Window() != NULL)
		Window()->SetPulseRate(50000);		// ~20 frames/s
}

void
PJView::MakeFocus(bool focus)
{
	BView::MakeFocus(focus);
	Invalidate();
}

void
PJView::SetPaused(bool paused)
{
	fPaused = paused;
	if (paused)
		RenderFull();		// beauty pass at full detail
	else
		fLowRes = true;		// animation renders coarse
	Invalidate();
	UpdateTitle();
}

bool
PJView::PixelAt(int32 x, int32 y, BMessage* reply)
{
	if (fBitmap == NULL || x < 0 || y < 0 || x >= fRenderW
		|| y >= fRenderH)
		return false;
	const uint8* p = (const uint8*)fBitmap->Bits()
		+ (size_t)y * fBitmap->BytesPerRow() + (size_t)x * 4;
	BString s;
	s.SetToFormat("%d %d %d", (int)p[2], (int)p[1], (int)p[0]);
	reply->what = B_REPLY;
	reply->AddString("result", s.String());
	return true;
}

void
PJView::RenderInto(int32 w, int32 h)
{
	if (fBitmap == NULL || fRenderW != w || fRenderH != h) {
		delete fBitmap;
		fBitmap = new BBitmap(BRect(0, 0, w - 1, h - 1), B_RGBA32);
		fRenderW = w;
		fRenderH = h;
	}
	if (fBitmap != NULL && fBitmap->IsValid())
		fFrac.Render((uint8*)fBitmap->Bits(), fBitmap->BytesPerRow(),
			w, h);
}

void
PJView::RenderFull()
{
	fLowRes = false;
	int32 w = (int32)Bounds().Width() + 1;
	int32 h = (int32)Bounds().Height() + 1;
	if (w < 2 || h < 2)
		return;
	RenderInto(w, h);
	Invalidate();
}

void
PJView::Pulse()
{
	if (fPaused)
		return;
	// animate: advance θ, render coarse for framerate
	fFrac.SetFrame(fFrac.Frame() + fSpeed);
	int32 w = ((int32)Bounds().Width() + 1) / 3;
	int32 h = ((int32)Bounds().Height() + 1) / 3;
	if (w >= 2 && h >= 2) {
		fLowRes = true;
		RenderInto(w, h);
		Invalidate();
	}
}

void
PJView::Draw(BRect)
{
	if (fBitmap == NULL)
		return;
	SetDrawingMode(B_OP_COPY);
	// letterbox to keep the complex plane's aspect true
	float vw = Bounds().Width() + 1, vh = Bounds().Height() + 1;
	float bw = fRenderW, bh = fRenderH;
	float scale = std::min(vw / bw, vh / bh);
	BRect dst(0, 0, bw * scale - 1, bh * scale - 1);
	dst.OffsetBy((vw - bw * scale) / 2, (vh - bh * scale) / 2);
	DrawBitmap(fBitmap, fBitmap->Bounds(), dst);
	// the set's boundary is delicate: hint what coarse mode hides
	if (fLowRes && !fPaused) {
		SetHighColor(255, 255, 255, 160);
		SetDrawingMode(B_OP_ALPHA);
		DrawString("animating (space pauses for a full-detail pass)",
			BPoint(12, 20));
		SetDrawingMode(B_OP_COPY);
	}
}

void
PJView::KeyDown(const char* bytes, int32 numBytes)
{
	if (numBytes != 1) {
		BView::KeyDown(bytes, numBytes);
		return;
	}
	switch (bytes[0]) {
		case ' ':
			SetPaused(!fPaused);
			return;
		case '+':
		case '=':
			fFrac.ZoomBy(0.7f);
			RenderFull();
			return;
		case '-':
			fFrac.ZoomBy(1.0f / 0.7f);
			RenderFull();
			return;
		case B_LEFT_ARROW:
			fFrac.PanBy(-fFrac.Span() / 10.0f, 0);
			RenderFull();
			return;
		case B_RIGHT_ARROW:
			fFrac.PanBy(fFrac.Span() / 10.0f, 0);
			RenderFull();
			return;
		case B_UP_ARROW:
			fFrac.PanBy(0, -fFrac.Span() / 10.0f);
			RenderFull();
			return;
		case B_DOWN_ARROW:
			fFrac.PanBy(0, fFrac.Span() / 10.0f);
			RenderFull();
			return;
		default:
			BView::KeyDown(bytes, numBytes);
	}
}

void
PJView::UpdateTitle()
{
	if (Window() != NULL)
		Window()->PostMessage('pjTi');
}

// --------------------------------------------------------------- window --
class PJWindow : public BWindow {
public:
			PJWindow(BRect frame);

	void	MessageReceived(BMessage* message) override;
	bool	QuitRequested() override;

	PJView*	View() { return fView; }

	// render the current frame at postcard resolution and write it;
	// called on the window looper (menu) and under Lock() from the
	// app looper (scripting)
	status_t	SavePostcard(const char* path);

	enum {
		PALETTE_MSG = 'pjPa', ITER_MSG = 'pjIt', SPEED_MSG = 'pjSp',
		RESET_MSG = 'pjRz', POSTCARD_MSG = 'pjPc'
	};

private:
	void	BuildMenus();
	void	UpdateTitle();

	PJView*		fView = NULL;
	BMenuBar*	fMenuBar = NULL;
};

PJWindow::PJWindow(BRect frame)
	:
	BWindow(frame, "ProseJulia", B_TITLED_WINDOW,
		B_QUIT_ON_WINDOW_CLOSE | B_ASYNCHRONOUS_CONTROLS)
{
	fView = new PJView(Bounds());
	fMenuBar = new BMenuBar(Bounds(), "menubar");
	BuildMenus();
	AddChild(fMenuBar);
	// the view fills the window below the menu bar
	fView->MoveTo(0, fMenuBar->Bounds().Height() + 1);
	fView->ResizeTo(Bounds().Width(),
		Bounds().Height() - fMenuBar->Bounds().Height() - 1);
	AddChild(fView);
	fView->MakeFocus();
	fView->RenderFull();
	UpdateTitle();
}

void
PJWindow::BuildMenus()
{
	BMenu* menu = new BMenu("Fractal");
	menu->AddItem(new BMenuItem("Pause" B_UTF8_ELLIPSIS " (space)",
		new BMessage('pjQq')));
	menu->AddItem(new BMenuItem("Reset view", new BMessage(RESET_MSG),
		'R', B_COMMAND_KEY));
	menu->AddSeparatorItem();

	BMenu* palette = new BMenu("Palette");
	palette->SetRadioMode(true);
	for (int32 i = 0; i < PJFractal::kPaletteCount; i++) {
		BMessage* m = new BMessage(PALETTE_MSG);
		m->AddInt32("index", i);
		BMenuItem* it = new BMenuItem(PJFractal::PaletteName(i), m);
		if (i == 0)
			it->SetMarked(true);
		palette->AddItem(it);
	}
	menu->AddItem(palette);

	BMenu* detail = new BMenu("Detail");
	detail->SetRadioMode(true);
	const int32 iters[] = { 64, 200, 500, 1000 };
	for (int32 i = 0; i < 4; i++) {
		BMessage* m = new BMessage(ITER_MSG);
		m->AddInt32("iters", iters[i]);
		char label[16];
		snprintf(label, sizeof(label), "%d", (int)iters[i]);
		BMenuItem* it = new BMenuItem(label, m);
		if (iters[i] == 200)
			it->SetMarked(true);
		detail->AddItem(it);
	}
	menu->AddItem(detail);

	BMenu* speed = new BMenu("Speed");
	speed->SetRadioMode(true);
	const double speeds[] = { 0.25, 1.0, 3.0, 8.0 };
	for (int32 i = 0; i < 4; i++) {
		BMessage* m = new BMessage(SPEED_MSG);
		m->AddDouble("speed", speeds[i]);
		char label[16];
		snprintf(label, sizeof(label), "%.2g°/f", speeds[i]);
		BMenuItem* it = new BMenuItem(label, m);
		if (speeds[i] == 1.0)
			it->SetMarked(true);
		speed->AddItem(it);
	}
	menu->AddItem(speed);
	fMenuBar->AddItem(menu);

	menu = new BMenu("File");
	menu->AddItem(new BMenuItem("Save postcard as PDF"
		B_UTF8_ELLIPSIS, new BMessage(POSTCARD_MSG), 'S',
		B_COMMAND_KEY));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Quit", new BMessage(B_QUIT_REQUESTED),
		'Q', B_COMMAND_KEY));
	fMenuBar->AddItem(menu);
}

void
PJWindow::UpdateTitle()
{
	BString s;
	s.SetToFormat("ProseJulia  —  θ=%.0f°  c=%.4f%+.4fi  span=%.3g"
		"  %s  %s",
		fView->Fractal().Frame(),
		PJFractal::kOrbitRadius
			* cos(fView->Fractal().Frame() * M_PI / 180.0),
		PJFractal::kOrbitRadius
			* sin(fView->Fractal().Frame() * M_PI / 180.0),
		fView->Fractal().Span(),
		PJFractal::PaletteName(fView->Fractal().Palette()),
		fView->Paused() ? "paused" : "animating");
	SetTitle(s.String());
}

bool
PJWindow::QuitRequested()
{
	return true;
}

status_t
PJWindow::SavePostcard(const char* path)
{
	// 512² at full detail, whatever the window size
	const int32 kSize = 512;
	BBitmap card(BRect(0, 0, kSize - 1, kSize - 1), B_RGBA32);
	if (!card.IsValid())
		return B_NO_MEMORY;
	fView->Fractal().Render((uint8*)card.Bits(), card.BytesPerRow(),
		kSize, kSize);
	// the page matches the square image: a 512pt postcard
	return PjWritePDF(&card, kSize, kSize, path);
}

void
PJWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case 'pjQq':
			fView->SetPaused(!fView->Paused());
			UpdateTitle();
			break;
		case RESET_MSG:
			fView->Fractal().SetView(0.0f, 0.0f, 3.0f);
			fView->RenderFull();
			UpdateTitle();
			break;
		case PALETTE_MSG:
		{
			int32 index = 0;
			message->FindInt32("index", &index);
			fView->Fractal().SetPalette(index);
			fView->RenderFull();
			UpdateTitle();
			break;
		}
		case ITER_MSG:
		{
			int32 n = 200;
			message->FindInt32("iters", &n);
			fView->Fractal().SetIterations(n);
			fView->RenderFull();
			break;
		}
		case SPEED_MSG:
		{
			double s = 1.0;
			message->FindDouble("speed", &s);
			fView->SetSpeed(s);
			break;
		}
		case POSTCARD_MSG:
		{
			// the beauty pass, saved beside the app settings
			BPath path;
			if (find_directory(B_USER_NONPACKAGED_DATA_DIRECTORY, &path)
					== B_OK) {
				path.Append("ProseJulia");
				create_directory(path.Path(), 0755);
				path.Append("postcard.pdf");
				SavePostcard(path.Path());
			}
			break;
		}
		case 'pjRd':
			fView->RenderFull();
			UpdateTitle();
			break;
		case 'pjTi':
			UpdateTitle();
			break;
		default:
			BWindow::MessageReceived(message);
	}
}

// ------------------------------------------------------------------ app --
static property_info sPJProperties[] = {
	{ "Frame",
		{ B_GET_PROPERTY, B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"animation angle θ in degrees (the c-orbit position)", 0,
		{ B_INT32_TYPE, B_INT32_TYPE } },
	{ "Paused",
		{ B_GET_PROPERTY, B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"animation paused (pause renders full detail)", 0,
		{ B_BOOL_TYPE, B_BOOL_TYPE } },
	{ "Palette",
		{ B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"palette by name: inferno/ocean/ember/ultraviolet", 0,
		{ B_STRING_TYPE } },
	{ "Iterations",
		{ B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"escape iterations (16..2048)", 0, { B_STRING_TYPE } },
	{ "Zoom",
		{ B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"zoom factor (2 = twice as close)", 0, { B_STRING_TYPE } },
	{ "Centre",
		{ B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"view centre: \"re im\"", 0, { B_STRING_TYPE } },
	{ "Pixel",
		{ B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"frame colour at \"x y\" (render resolution), as \"r g b\"", 0,
		{ B_STRING_TYPE } },
	{ "Activate",
		{ B_EXECUTE_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"bring the window forward and focus the view", 0, { 0 } },
	{ "Postcard",
		{ B_EXECUTE_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"write the current frame as a PDF (data: path)", 0,
		{ B_STRING_TYPE } },
	{ "Quit",
		{ B_EXECUTE_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"close and quit", 0, { 0 } },
	{ 0 }
};

const BPropertyInfo kPJScriptingProperties(sPJProperties);

static void
ReplyString(BMessage* message, const char* value)
{
	BMessage reply(B_REPLY);
	reply.AddString("result", value);
	message->SendReply(&reply);
}

static void
ReplyError(BMessage* message, const char* error)
{
	BMessage reply(B_ERROR);
	reply.AddString("error", error);
	message->SendReply(&reply);
}

static void
ReplyInt(BMessage* message, int32 value)
{
	BMessage reply(B_REPLY);
	reply.AddInt32("result", value);
	message->SendReply(&reply);
}

static bool
HandleScripting(PJWindow* window, BMessage* message, const char* prop)
{
	if (prop == NULL || !prop[0])
		return false;
	PJView* view = window->View();
	PJFractal& frac = view->Fractal();
	BString p = prop;
	bool isGet = message->what == B_GET_PROPERTY;
	bool isSet = message->what == B_SET_PROPERTY;
	bool isExec = message->what == B_EXECUTE_PROPERTY;

	if (p == "Frame" && isGet) {
		ReplyInt(message, (int32)(frac.Frame() + 0.5));
		return true;
	}
	if (p == "Frame" && isSet) {
		// int, float or string, all accepted
		double theta = 0;
		float f = 0;
		int32 i = 0;
		BString s;
		if (message->FindFloat("data", &f) == B_OK)
			theta = f;
		else if (message->FindInt32("data", &i) == B_OK)
			theta = i;
		else if (message->FindString("data", &s) == B_OK)
			theta = atof(s.String());
		frac.SetFrame(theta);
		window->PostMessage('pjRd');	// re-render + title
		ReplyInt(message, (int32)(frac.Frame() + 0.5));
		return true;
	}
	if (p == "Paused" && isGet) {
		ReplyString(message, view->Paused() ? "true" : "false");
		return true;
	}
	if (p == "Paused" && isSet) {
		bool pause = true;
		const char* s = NULL;
		int32 i = 0;
		if (message->FindString("data", &s) == B_OK)
			pause = !(strcasecmp(s, "false") == 0 || strcmp(s, "0") == 0);
		else if (message->FindInt32("data", &i) == B_OK)
			pause = i != 0;
		// set, not toggle: act only when it differs
		if (pause != view->Paused())
			window->PostMessage('pjQq');
		ReplyString(message, "");
		return true;
	}
	if (p == "Palette" && isSet) {
		BString s;
		if (message->FindString("data", &s) != B_OK) {
			ReplyError(message, "data: palette name required");
			return true;
		}
		int32 index = -1;
		for (int32 i = 0; i < PJFractal::kPaletteCount; i++)
			if (s.ICompare(PJFractal::PaletteName(i)) == 0)
				index = i;
		if (index < 0) {
			ReplyError(message, "inferno/ocean/ember/ultraviolet");
			return true;
		}
		frac.SetPalette(index);
		window->PostMessage('pjRd');
		ReplyString(message, "");
		return true;
	}
	if (p == "Iterations" && isSet) {
		BString s;
		int32 i = 0;
		if (message->FindInt32("data", &i) != B_OK
			|| (message->FindString("data", &s) == B_OK && s.Length()))
			i = s.Length() ? atoi(s.String()) : i;
		frac.SetIterations(i);
		window->PostMessage('pjRd');
		ReplyString(message, "");
		return true;
	}
	if (p == "Zoom" && isSet) {
		float f = 1;
		int32 i = 0;
		BString s;
		if (message->FindFloat("data", &f) != B_OK) {
			if (message->FindInt32("data", &i) == B_OK)
				f = i;
			else if (message->FindString("data", &s) == B_OK)
				f = atof(s.String());
		}
		if (f > 0)
			frac.ZoomBy(1.0f / f);
		window->PostMessage('pjRd');
		ReplyString(message, "");
		return true;
	}
	if (p == "Centre" && isSet) {
		BString s;
		float re = 0, im = 0;
		if (message->FindString("data", &s) != B_OK
			|| sscanf(s.String(), "%f %f", &re, &im) != 2) {
			ReplyError(message, "data: \"re im\" required");
			return true;
		}
		frac.SetView(re, im, frac.Span());
		window->PostMessage('pjRd');
		ReplyString(message, "");
		return true;
	}
	if (p == "Pixel" && isGet) {
		BString s;
		int32 x = -1, y = -1;
		if (message->FindString("data", &s) != B_OK
			|| sscanf(s.String(), "%d %d", &x, &y) != 2) {
			ReplyError(message, "data: \"x y\" required");
			return true;
		}
		// Answer synchronously from the original message. A reply sent
		// from a copy carried in another message never arrives: the
		// AddMessage/FindMessage round trip flattens the header away,
		// so the copy has no reply address left.
		BMessage reply(B_ERROR);
		if (window->Lock()) {
			if (!view->Paused())
				view->SetPaused(true);	// stop the pulse, full-res pass
			if (!view->PixelAt(x, y, &reply))
				reply.AddString("error", "x y outside the render");
			window->Unlock();
		} else
			reply.AddString("error", "window closed");
		message->SendReply(&reply);
		return true;
	}
	if (p == "Activate" && isExec) {
		if (window->Lock()) {
			window->Activate();
			if (view->Window() != NULL)
				view->MakeFocus();
			window->Unlock();
		}
		ReplyString(message, "");
		return true;
	}
	if (p == "Postcard" && isExec) {
		BString path;
		if (message->FindString("data", &path) == B_OK
				&& path.Length()) {
			// synchronous like Pixel — same carried-copy reply trap
			status_t err = B_ERROR;
			if (window->Lock()) {
				err = window->SavePostcard(path.String());
				window->Unlock();
			}
			if (err == B_OK)
				ReplyString(message, path.String());
			else
				ReplyError(message, strerror(err));
		} else
			ReplyError(message, "data: path required");
		return true;
	}
	if (p == "Quit" && isExec) {
		window->PostMessage(B_QUIT_REQUESTED);
		ReplyString(message, "");
		return true;
	}
	return false;
}

class PJApp : public BApplication {
public:
			PJApp()
				:
				BApplication("application/x-vnd.prose.ProseJulia")
			{
			}

	status_t GetSupportedSuites(BMessage* message) override
	{
		message->AddString("suites", "suite/x-vnd.prose.ProseJulia");
		message->AddFlat("messages", &kPJScriptingProperties);
		return BApplication::GetSupportedSuites(message);
	}

	BHandler* ResolveSpecifier(BMessage* message, int32 index,
		BMessage* specifier, int32 what, const char* property) override
	{
		if (kPJScriptingProperties.FindMatch(message, index, specifier,
				what, property) >= 0)
			return this;
		return BApplication::ResolveSpecifier(message, index, specifier,
			what, property);
	}

	void	ReadyToRun() override
	{
		BScreen screen(B_MAIN_SCREEN_ID);
		BRect avail = screen.Frame().InsetByCopy(40, 36);
		PJWindow* window = new PJWindow(
			BRect(avail.left, avail.top, avail.left + 559,
				avail.top + 479));
		window->Show();
		SetPreferredHandler(this);
	}

	void	MessageReceived(BMessage* message) override;
};

void
PJApp::MessageReceived(BMessage* message)
{
	if (message->HasSpecifiers()) {
		PJWindow* w = dynamic_cast<PJWindow*>(WindowAt(0));
		if (w != NULL) {
			BMessage spec;
			int32 what = 0;
			int32 index = 0;
			const char* prop = NULL;
			if (message->GetCurrentSpecifier(&index, &spec, &what,
					&prop) == B_OK
				&& kPJScriptingProperties.FindMatch(message, index, &spec,
					what, prop) >= 0) {
				if (HandleScripting(w, message, prop))
					return;
			}
		}
	}
	BApplication::MessageReceived(message);
}

// --------------------------------------------------------------- selftest --
struct PjCase { const char* name; bool ok; };
static std::vector<PjCase> sCases;
static bool sAll = true;
#define CHECK(label, cond) { bool ok_ = (cond); \
	sCases.push_back(PjCase{ label, ok_ }); if (!ok_) sAll = false; }

static int
SelfTest()
{
	printf("block: math\n"); fflush(stdout);
	{
		PJFractal frac;
		CHECK("frame wraps to [0,360)", (frac.SetFrame(725.0),
			frac.Frame() >= 0.0 && frac.Frame() < 360.0
			&& fabs(frac.Frame() - 5.0) < 1e-9));
		CHECK("frame wraps negatives",
			(frac.SetFrame(-30.0), fabs(frac.Frame() - 330.0) < 1e-9));
		frac.SetFrame(90.0);
		CHECK("orbit at 90° is (0,R)",
			fabs(frac.CRe()) < 1e-9
			&& fabs(frac.CIm() - PJFractal::kOrbitRadius) < 1e-9);
		frac.SetFrame(0.0);
		CHECK("orbit at 0° is (R,0)",
			fabs(frac.CRe() - PJFractal::kOrbitRadius) < 1e-9
			&& fabs(frac.CIm()) < 1e-9);
		// far-away points escape immediately regardless of c (the
		// smooth count can be small-negative for instant escapes; it
		// only must not be the -1 "inside" marker)
		double far1 = frac.SmoothIter(-1000, 0);
		CHECK("far point escapes fast", far1 != -1.0 && far1 < 3.0);
		// c at theta=180 is -0.7885, inside the Mandelbrot set on the
		// real axis, so the Julia set has interior and the orbit of
		// 0 never escapes: the -1 "inside" marker
		frac.SetFrame(180.0);
		CHECK("inside reports -1", frac.SmoothIter(0.0, 0.0) == -1.0);
		frac.SetFrame(0.0);
		// determinism
		double a = frac.SmoothIter(0.35, -0.2);
		double b2 = frac.SmoothIter(0.35, -0.2);
		CHECK("math is deterministic", a == b2);
		// zoom clamps
		frac.SetView(0, 0, 3);
		frac.ZoomBy(0.5f);
		CHECK("zoom in halves span", fabs(frac.Span() - 1.5f) < 1e-4);
		frac.ZoomBy(1.0f / 0.5f);
		CHECK("zoom out restores", fabs(frac.Span() - 3.0f) < 1e-4);
		frac.SetView(0, 0, 1e-9f);
		CHECK("degenerate span refused", fabs(frac.Span() - 3.0f) < 1e-4);
		// iterations clamp
		frac.SetIterations(4);
		CHECK("iterations refused below floor", frac.Iterations() == 200);
		frac.SetIterations(100000);
		CHECK("iterations refused above ceiling",
			frac.Iterations() == 200);
		frac.SetIterations(500);
		CHECK("iterations set", frac.Iterations() == 500);
	}

	printf("block: render\n"); fflush(stdout);
	{
		PJFractal frac;
		const int32 w = 96, h = 72;
		std::vector<uint8> bits((size_t)w * h * 4, 255);
		frac.Render(bits.data(), w * 4, w, h);
		// per-pixel math and the render agree
		PJColour c0 = frac.PixelColour(10, 10, w, h);
		uint8* p = bits.data() + ((size_t)10 * w + 10) * 4;
		CHECK("pixel colour matches render",
			p[0] == c0.b && p[1] == c0.g && p[2] == c0.r);
		// every pixel opaque
		bool opaque = true;
		for (int32 y = 0; y < h && opaque; y++)
			for (int32 x = 0; x < w; x++)
				if (bits[((size_t)y * w + x) * 4 + 3] != 255) {
					opaque = false;
					break;
				}
		CHECK("render is opaque", opaque);
		// the frame changes the picture (θ=0 vs θ=180°)
		std::vector<uint8> bits2((size_t)w * h * 4, 255);
		frac.SetFrame(180.0);
		frac.Render(bits2.data(), w * 4, w, h);
		bool differs = false;
		for (size_t i = 0; i < bits.size(); i += 4)
			if (bits[i] != bits2[i] || bits[i + 1] != bits2[i + 1]
				|| bits[i + 2] != bits2[i + 2]) {
				differs = true;
				break;
			}
		CHECK("animation changes the picture", differs);
		// and the palette does too
		frac.SetFrame(0.0);
		frac.SetPalette(1);
		frac.Render(bits2.data(), w * 4, w, h);
		differs = false;
		for (size_t i = 0; i < bits.size(); i += 4)
			if (bits[i + 2] != bits2[i + 2]) {
				differs = true;
				break;
			}
		CHECK("palette changes the picture", differs);
		CHECK("palette index clamps",
			(frac.SetPalette(-1), frac.Palette() == 1));
		CHECK("palette names resolve",
			strcmp(PJFractal::PaletteName(0), "Inferno") == 0);
	}

	int passed = 0;
	for (const PjCase& c : sCases) {
		printf("  %-42s %s\n", c.name, c.ok ? "PASS" : "FAIL");
		if (c.ok) passed++;
	}
	printf("SELFTEST %s %d/%d\n", sAll ? "PASS" : "FAIL", passed,
		(int)sCases.size());
	return sAll ? 0 : 1;
}

int
main(int argc, char** argv)
{
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--selftest") == 0) {
			PJApp app;
			return SelfTest();
		}
	}
	PJApp app;
	app.Run();
	return 0;
}
