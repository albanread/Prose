/*
 * Drawing: a view that paints itself.
 *
 *     clang++ -O2 -Wall -o drawing drawing.cpp -lbe
 *
 * A BView draws in Draw(), which the app_server calls whenever part of the
 * view needs to appear -- when the window opens, is resized, or is uncovered.
 * Nothing is kept as a picture: the view is asked to paint again, so Draw()
 * should be able to run at any moment and should not change anything.
 */

#include <Application.h>
#include <View.h>
#include <Window.h>

#include <math.h>

class CanvasView : public BView {
public:
	CanvasView(BRect frame)
		:
		BView(frame, "canvas", B_FOLLOW_ALL, B_WILL_DRAW | B_FRAME_EVENTS)
	{
		SetViewColor(B_TRANSPARENT_COLOR);	// Draw() paints every pixel
	}

	virtual void Draw(BRect updateRect)
	{
		BRect bounds(Bounds());

		// a wash of colour, drawn as a column of lines
		for (float y = bounds.top; y <= bounds.bottom; y++) {
			float t = (bounds.Height() > 0) ? y / bounds.Height() : 0;
			rgb_color sky = { (uint8)(40 + 60 * t), (uint8)(60 + 90 * t),
				(uint8)(120 + 110 * t), 255 };
			SetHighColor(sky);
			StrokeLine(BPoint(bounds.left, y), BPoint(bounds.right, y));
		}

		// a filled circle with a soft edge
		SetDrawingMode(B_OP_ALPHA);
		SetHighColor(255, 220, 120, 230);
		BPoint centre(bounds.Width() * 0.30, bounds.Height() * 0.35);
		FillEllipse(centre, 46, 46);

		// a curve, stroked
		SetHighColor(255, 255, 255, 200);
		SetPenSize(3);
		BPoint last(bounds.left, bounds.bottom - 40);
		for (float x = bounds.left; x <= bounds.right; x += 4) {
			float phase = (x / bounds.Width()) * 2 * M_PI;
			BPoint here(x, bounds.bottom - 40 - sinf(phase) * 28);
			StrokeLine(last, here);
			last = here;
		}

		// and some text, right-aligned along the bottom
		SetDrawingMode(B_OP_OVER);
		SetHighColor(255, 255, 255, 255);
		SetFontSize(16);

		const char* text = "drawn by Prose";
		float width = StringWidth(text);
		MovePenTo(bounds.right - width - 14, bounds.bottom - 12);
		DrawString(text);
	}

	virtual void FrameResized(float width, float height)
	{
		Invalidate();	// the drawing depends on the size, so paint it again
		BView::FrameResized(width, height);
	}
};

int
main()
{
	BApplication app("application/x-vnd.prose-example-drawing");

	BWindow* window = new BWindow(BRect(140, 140, 580, 440), "Drawing",
		B_TITLED_WINDOW, B_ASYNCHRONOUS_CONTROLS | B_QUIT_ON_WINDOW_CLOSE);
	window->AddChild(new CanvasView(window->Bounds()));
	window->Show();

	app.Run();
	return 0;
}
