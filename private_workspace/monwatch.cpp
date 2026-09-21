// monwatch: what the node monitors of a directory (and, named, of single
// files) say, printed as lines. Built and run inside the guest by
// private_workspace/hostfs-appear.sh, to verify that what the Mac does to the
// shared folder reaches the guest:
//   MON created <name> / MON removed <name>      (the directory's entries)
//   MON stat-changed [size] [mtime] <name>       (a named file's own stat)
// usage: monwatch <directory> <seconds> [file ...]
#include <Entry.h>
#include <Handler.h>
#include <Looper.h>
#include <Message.h>
#include <Messenger.h>
#include <Node.h>
#include <NodeMonitor.h>
#include <String.h>
#include <stdio.h>
#include <stdlib.h>

struct Watched {
	ino_t	node;
	BString	name;
};

static BString s;
static Watched sWatched[8];
static int sWatchedCount = 0;

static const char*
name_of(ino_t node)
{
	for (int i = 0; i < sWatchedCount; i++)
		if (sWatched[i].node == node)
			return sWatched[i].name.String();
	return "?";
}

class Watcher : public BHandler {
public:
	void MessageReceived(BMessage* message) override
	{
		if (message->what != B_NODE_MONITOR)
			return;
		int32 opcode = 0;
		message->FindInt32("opcode", &opcode);
		BString line;
		switch (opcode) {
			case B_ENTRY_CREATED: {
				const char* name = NULL;
				message->FindString("name", &name);
				line << "created " << (name != NULL ? name : "?");
				break;
			}
			case B_ENTRY_REMOVED: {
				const char* name = NULL;
				message->FindString("name", &name);
				line << "removed " << (name != NULL ? name : "?");
				break;
			}
			case B_STAT_CHANGED: {
				int32 fields = 0;
				ino_t node = 0;
				message->FindInt32("fields", &fields);
				message->FindInt64("node", (int64*)&node);
				line << "stat-changed";
				if ((fields & B_STAT_SIZE) != 0)
					line << " size";
				if ((fields & B_STAT_MODIFICATION_TIME) != 0)
					line << " mtime";
				line << " " << name_of(node);
				break;
			}
			default:
				line << "opcode " << (int)opcode;
		}
		printf("MON %s\n", line.String());
		fflush(stdout);
	}
};

int
main(int argc, char** argv)
{
	if (argc < 3) {
		printf("MON usage: monwatch <directory> <seconds> [file ...]\n");
		return 1;
	}
	const char* path = argv[1];
	const int seconds = atoi(argv[2]);

	BLooper* looper = new BLooper("monwatch");
	Watcher* watcher = new Watcher();
	looper->AddHandler(watcher);
	looper->Run();
	BMessenger messenger(watcher, looper);

	BEntry entry(path);
	if (entry.InitCheck() != B_OK || !entry.Exists()) {
		printf("MON cannot watch %s\n", path);
		return 1;
	}
	node_ref ref;
	entry.GetNodeRef(&ref);
	status_t status = watch_node(&ref, B_WATCH_DIRECTORY, messenger);
	if (status != B_OK) {
		printf("MON watch_node %s: %s\n", path, strerror(status));
		return 1;
	}
	printf("MON watching %s for %d s\n", path, seconds);

	for (int i = 3; i < argc && sWatchedCount < 8; i++) {
		BEntry file(argv[i]);
		struct stat st;
		if (file.InitCheck() != B_OK || !file.Exists()
			|| file.GetStat(&st) != B_OK) {
			printf("MON cannot watch %s\n", argv[i]);
			continue;
		}
		node_ref fileRef;
		file.GetNodeRef(&fileRef);
		if (watch_node(&fileRef, B_WATCH_STAT, messenger) == B_OK) {
			sWatched[sWatchedCount].node = st.st_ino;
			sWatched[sWatchedCount].name = argv[i];
			sWatchedCount++;
			printf("MON watching %s (node %lld)\n", argv[i], (long long)st.st_ino);
		}
	}
	fflush(stdout);

	bigtime_t until = system_time() + (bigtime_t)seconds * 1000000;
	while (system_time() < until)
		snooze(200000);
	stop_watching(messenger);
	printf("MON done\n");
	return 0;
}
