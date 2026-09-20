/*
 * A window on screen: the smallest useful Be API program.
 *
 *     clang++ -O2 -Wall -o window window.cpp -lbe
 *
 * Two objects do all of it. A BApplication is the program itself: it talks
 * to the app_server, and its Run() does not return until the program is
 * asked to quit. A BWindow is a window, and it has a thread of its own --
 * everything a window draws or handles happens on that thread, which is why
 * QuitRequested() below is called there and not in main().
 */

#include <Application.h>
#include <StringView.h>
#include <Window.h>

class HelloWindow : public BWindow {
public:
	HelloWindow()
		:
		BWindow(BRect(120, 120, 460, 260), "Hello", B_TITLED_WINDOW,
			B_ASYNCHRONOUS_CONTROLS | B_QUIT_ON_WINDOW_CLOSE)
	{
		BView* background = new BView(Bounds(), "background", B_FOLLOW_ALL, 0);
		background->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		AddChild(background);

		BRect frame = Bounds();
		frame.InsetBy(16, 16);

		BStringView* label = new BStringView(frame, "label",
			"Hello from Prose.", B_FOLLOW_ALL);
		label->SetFontSize(18);
		background->AddChild(label);
	}

	// Closing the window ends the program. B_QUIT_ON_WINDOW_CLOSE above does
	// this for us; the override is here to show where to ask the user first.
	virtual bool QuitRequested()
	{
		return BWindow::QuitRequested();
	}
};

int
main()
{
	BApplication app("application/x-vnd.prose-example-window");

	(new HelloWindow())->Show();

	app.Run();
	return 0;
}
