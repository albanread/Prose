#include <Application.h>
#include <TextView.h>
#include <Window.h>
#include <cstdio>
int main() {
    BApplication app("application/x-vnd.prose.hello");
    BWindow* w = new BWindow(BRect(100, 100, 400, 300), "Hello Prose",
        B_TITLED_WINDOW, B_QUIT_ON_WINDOW_CLOSE);
    BTextView* tv = new BTextView(BRect(10, 10, 280, 180), "tv", BRect(0,0,270,170),
        B_FOLLOW_ALL, B_WILL_DRAW);
    tv->SetText("Cross-compiled on macOS, running under QEMU.");
    tv->MakeEditable(false);
    w->AddChild(tv);
    w->Show();
    app.Run();
    return 0;
}
