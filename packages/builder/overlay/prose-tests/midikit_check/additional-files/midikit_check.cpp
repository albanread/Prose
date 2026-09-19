/*
 * Copyright 2026, HaikuArmQemu (PROSE fork). All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * midikit_check: the Midi Kit's file player, BMidiSynthFile, must survive
 * being deleted while it plays, and must stop at once (patches 0050, 0051).
 * Two threads work for it:
 *  - its BMidiStore sends the song from a run thread, paced by the
 *    synthesizer. BMidi::Stop() only asks that thread to stop; before the
 *    fix, ~BMidi then waited for "thread -1" -- for nothing -- and the
 *    thread went on in the freed store and called the song hook of an
 *    owner that was gone: a crash, a "double free", or a hook call after
 *    the delete.
 *  - the synthesizer's input (a glue consumer) plays each note from its own
 *    thread, and sleeps until the note is due. Before the fix the last
 *    BMidiSynth deleted be_synth under it: a crash in fluid_synth_noteon().
 *
 * Part 1: each round loads a song, starts it, sets a song hook, lets it
 * play for a while (different each round), in half the rounds calls Stop()
 * first (as the old MidiPlayer did), and deletes it. In most rounds another
 * BMidiSynth keeps be_synth (and its soundfont); in the last ones the
 * player is its last client, so deleting the player deletes be_synth too.
 * A hook call for a round whose player has been deleted is a failure; so is
 * a Stop() or delete that takes seconds (the song went on meanwhile), and
 * anything worse.
 *
 * Part 2: a short song whose song hook deletes its own player -- on the
 * player's run thread, which must then leave the freed player alone. Run it
 * with MALLOC_DEBUG=g (libroot_debug.so) to see such writes: they fault.
 *
 * usage: midikit_check [song.mid] [rounds] [last-client rounds]
 * The last line is PASS or FAIL; the exit status is the failures.
 */


#include <Application.h>
#include <Entry.h>
#include <File.h>
#include <MidiSynth.h>
#include <MidiSynthFile.h>
#include <OS.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static const int32 kMaxRounds = 200;
static const int32 kSelfDeleteRounds = 2;
static const bigtime_t kTooLong = 5000000;
static const char* kShortSongPath = "/tmp/midikit_check.mid";

// two notes, half a second each (BMidiStore's tempo is 60 bpm unless the
// song sets one)
static const uint8 kShortSong[] = {
	'M', 'T', 'h', 'd', 0, 0, 0, 6,
		0, 0,		// format 0
		0, 1,		// one track
		0, 96,		// ticks per quarter note
	'M', 'T', 'r', 'k', 0, 0, 0, 20,
		0x00, 0x90, 60, 100,
		0x30, 0x80, 60, 0,
		0x00, 0x90, 64, 100,
		0x30, 0x80, 64, 0,
		0x00, 0xff, 0x2f, 0x00
};

struct Owner {
	int32	deleted;
	int32	calls;
};

static Owner sOwners[kMaxRounds];
static int32 sLateCalls = 0;

static BMidiSynthFile* sSelfDeleting[kSelfDeleteRounds];
static sem_id sSelfDeleted;

static BMidiSynth* sKeeper = NULL;


static void
KeepSynth(bool keep)
{
	// A BMidiSynth that keeps be_synth, loaded, between the players; without
	// it each player is be_synth's last client, and takes it along
	if (keep && sKeeper == NULL)
		sKeeper = new BMidiSynth;
	else if (!keep && sKeeper != NULL) {
		delete sKeeper;
		sKeeper = NULL;
	}
}


static void
SongHook(int32 round)
{
	if (round < 0 || round >= kMaxRounds)
		return;
	if (atomic_get(&sOwners[round].deleted) != 0)
		atomic_add(&sLateCalls, 1);
	atomic_add(&sOwners[round].calls, 1);
}


static void
DeleteInHook(int32 round)
{
	// the song has ended: the player goes, from its own thread
	delete sSelfDeleting[round];
	sSelfDeleting[round] = NULL;
	release_sem(sSelfDeleted);
}


static int32
DeleteWhilePlaying(const entry_ref& ref, int32 rounds, int32 lastClientRounds)
{
	// the same "random" play times every run
	srand(1234);
	bigtime_t longest = 0;
	for (int32 round = 0; round < rounds; round++) {
		bool lastClient = round >= rounds - lastClientRounds;
		KeepSynth(!lastClient);

		BMidiSynthFile* synth = new BMidiSynthFile;
		bigtime_t start = system_time();
		if (synth->LoadFile(&ref) != B_OK) {
			printf("round %" B_PRId32 ": LoadFile() failed\n", round);
			return 1;
		}
		bigtime_t load = system_time() - start;
		synth->Start();
		synth->SetFileHook(SongHook, round);

		bigtime_t play = 50000 + (rand() % 12) * 40000;
		snooze(play);
		bool stopFirst = (round % 2) == 0;
		bigtime_t stop = 0;
		if (stopFirst) {
			start = system_time();
			synth->Stop();
			stop = system_time() - start;
		}

		start = system_time();
		delete synth;
		bigtime_t took = system_time() - start;
		atomic_set(&sOwners[round].deleted, 1);
		if (took > longest)
			longest = took;
		if (stop > longest)
			longest = stop;

		char stopText[32] = "";
		if (stopFirst) {
			snprintf(stopText, sizeof(stopText), "Stop() %" B_PRIdBIGTIME
				" ms, ", stop / 1000);
		}
		printf("round %2" B_PRId32 ": load %4" B_PRIdBIGTIME " ms, played "
			"%3" B_PRIdBIGTIME " ms, %sdelete %3" B_PRIdBIGTIME " ms%s\n",
			round, load / 1000, play / 1000, stopText, took / 1000,
			lastClient ? " (the synthesizer's last client)" : "");
		fflush(stdout);
	}

	// a thread that outlived its player would wake up now
	snooze(3000000);

	printf("hook calls after the delete: %" B_PRId32 "\n", sLateCalls);
	printf("longest Stop() or delete: %" B_PRIdBIGTIME " ms\n",
		longest / 1000);
	int32 failures = sLateCalls;
	if (longest > kTooLong) {
		printf("too long: the song went on while it was stopped\n");
		failures++;
	}
	return failures;
}


static int32
DeleteInSongHook()
{
	BFile file(kShortSongPath, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.Write(kShortSong, sizeof(kShortSong)) != sizeof(kShortSong)) {
		printf("cannot write %s\n", kShortSongPath);
		return 1;
	}
	entry_ref ref;
	if (get_ref_for_path(kShortSongPath, &ref) != B_OK)
		return 1;

	sSelfDeleted = create_sem(0, "self deleted");
	int32 failures = 0;
	for (int32 round = 0; round < kSelfDeleteRounds; round++) {
		bool lastClient = round == kSelfDeleteRounds - 1;
		KeepSynth(!lastClient);

		BMidiSynthFile* synth = new BMidiSynthFile;
		if (synth->LoadFile(&ref) != B_OK) {
			printf("short song: LoadFile() failed\n");
			return failures + 1;
		}
		sSelfDeleting[round] = synth;
		synth->SetFileHook(DeleteInHook, round);
		bigtime_t start = system_time();
		synth->Start();

		status_t status = acquire_sem_etc(sSelfDeleted, 1, B_RELATIVE_TIMEOUT,
			10000000);
		if (status != B_OK) {
			printf("self-delete %" B_PRId32 ": the song hook did not come "
				"(%s)\n", round, strerror(status));
			failures++;
			break;
		}
		bigtime_t took = system_time() - start;
		printf("self-delete %" B_PRId32 ": the 1 s song ended and its hook "
			"deleted the player after %" B_PRIdBIGTIME " ms%s\n", round,
			took / 1000, lastClient ? " (the synthesizer's last client)" : "");
		fflush(stdout);
		if (took > kTooLong) {
			printf("too long: the song ran late\n");
			failures++;
		}
		// what the run thread does after the hook: nothing to the player
		snooze(200000);
	}
	delete_sem(sSelfDeleted);
	KeepSynth(false);
	return failures;
}


int
main(int argc, char** argv)
{
	BApplication app("application/x-vnd.Prose-midikit-check");

	const char* path = argc > 1 ? argv[1]
		: "/boot/system/data/music/Beethoven - Ode to Joy.mid";
	int32 rounds = argc > 2 ? atoi(argv[2]) : 12;
	if (rounds < 1 || rounds > kMaxRounds)
		rounds = 12;
	int32 lastClientRounds = argc > 3 ? atoi(argv[3]) : 4;
	if (lastClientRounds < 0 || lastClientRounds > rounds)
		lastClientRounds = rounds;

	entry_ref ref;
	if (get_ref_for_path(path, &ref) != B_OK) {
		printf("no song at %s\nFAIL\n", path);
		return 1;
	}

	int32 failures = DeleteWhilePlaying(ref, rounds, lastClientRounds);
	failures += DeleteInSongHook();

	if (failures != 0) {
		printf("FAIL\n");
		return failures;
	}
	printf("PASS\n");
	return 0;
}
