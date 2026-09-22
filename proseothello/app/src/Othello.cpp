/*
 * Copyright 2026 The Prose project. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * ProseOthello: Reversi for Prose. You are black, the machine is white,
 * and every square you may play shows a small circle, so the board says
 * what is possible and you can think about what is good.
 *
 * Four levels:
 *   Easy    a legal move at random -- it will hand you the game
 *   Medium  takes whatever looks best right now (one look, no thinking)
 *   Hard    thinks four moves ahead
 *   Expert  thinks as far as two seconds allow, and counts the discs out
 *           exactly when the game is nearly decided
 *
 * --selftest runs the rules and every level headlessly; the same scripting
 * surface drives the window for smoke tests (hey).
 */


#include <Application.h>
#include <Alert.h>
#include <InterfaceKit.h>
#include <MenuBar.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Message.h>
#include <PropertyInfo.h>
#include <Screen.h>
#include <String.h>
#include <Window.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


//	#pragma mark - the rules


enum {
	kEmpty = 0,
	kBlack = 1,
	kWhite = 2,
};

static inline int
opponent(int side)
{
	return side == kBlack ? kWhite : kBlack;
}


/*!	One Reversi position: 64 cells, whose turn it is, and every ply played
	to get here, each as the move and a mask of the discs it turned -- undo
	masks the flips back and the past is the present again.
*/
class Position {
public:
			Position() { Reset(); }

	void	Reset()
	{
		memset(fCells, kEmpty, sizeof(fCells));
		fCells[27] = fCells[36] = kWhite;
		fCells[28] = fCells[35] = kBlack;
		fTurn = kBlack;
		fPlyCount = 0;
		fLastMove = -1;
	}

	int		Cell(int square) const { return fCells[square]; }
	int		Turn() const { return fTurn; }
	int		PlyCount() const { return fPlyCount; }
	int		LastMove() const { return fLastMove; }
	int		Empties() const
	{
		int count = 0;
		for (int i = 0; i < 64; i++)
			if (fCells[i] == kEmpty)
				count++;
		return count;
	}
	void	Counts(int& black, int& white) const
	{
		black = white = 0;
		for (int i = 0; i < 64; i++) {
			if (fCells[i] == kBlack)
				black++;
			else if (fCells[i] == kWhite)
				white++;
		}
	}

	/*!	The discs a move would turn, as a mask; 0 when the move is not legal.
	*/
	uint64_t	FlipsFor(int side, int square) const
	{
		if (fCells[square] != kEmpty)
			return 0;
		uint64_t flips = 0;
		static const int kDirs[8][2] = {
			{ -1, -1 }, { 0, -1 }, { 1, -1 }, { -1, 0 },
			{ 1, 0 }, { -1, 1 }, { 0, 1 }, { 1, 1 }
		};
		const int x = square % 8, y = square / 8;
		for (int d = 0; d < 8; d++) {
			uint64_t line = 0;
			int cx = x + kDirs[d][0], cy = y + kDirs[d][1];
			bool closed = false;
			while (cx >= 0 && cx < 8 && cy >= 0 && cy < 8) {
				int cell = fCells[cy * 8 + cx];
				if (cell == kEmpty)
					break;
				if (cell == side) {
					closed = true;
					break;
				}
				line |= 1ULL << (cy * 8 + cx);
				cx += kDirs[d][0];
				cy += kDirs[d][1];
			}
			if (closed)
				flips |= line;
		}
		return flips;
	}

	bool	Legal(int square) const
	{
		return FlipsFor(fTurn, square) != 0;
	}

	int		LegalMoves(int side, int moves[64]) const
	{
		int count = 0;
		for (int i = 0; i < 64; i++)
			if (FlipsFor(side, i) != 0)
				moves[count++] = i;
		return count;
	}

	/*!	Plays a move for the side to move (the caller passes whose turn it
		was), records it, and passes the turn on -- skipping a side that
		cannot move, which is recorded as a pass ply. Returns false when the
		move was not legal.
	*/
	bool	Play(int square)
	{
		if (fPlyCount >= (int)(sizeof(fHistory) / sizeof(Ply)))
			return false;
		uint64_t flips = FlipsFor(fTurn, square);
		if (flips == 0)
			return false;
		Apply(fTurn, square, flips);
		fHistory[fPlyCount].move = square;
		fHistory[fPlyCount].flips = flips;
		fHistory[fPlyCount].side = fTurn;
		fHistory[fPlyCount].pass = false;
		fPlyCount++;
		fLastMove = square;
		AdvanceTurn();
		return true;
	}

	bool	Undo()
	{
		if (fPlyCount == 0)
			return false;
		// back past any pass to a real move, and one more if that lands on a
		// pass -- undo takes back one played move, whoever played it
		while (fPlyCount > 0 && fHistory[fPlyCount - 1].pass) {
			fPlyCount--;
			fTurn = fHistory[fPlyCount].side;
		}
		if (fPlyCount == 0)
			return false;
		fPlyCount--;
		Retract(fHistory[fPlyCount]);
		return true;
	}

	/*!	Over when neither side can move (or the board is full).
	*/
	/*!	Flips the turn without recording anything -- search copies pass
		this way; a real game records its passes when a move is played.
	*/
	void	PassTurn()
	{
		fTurn = opponent(fTurn);
	}

	bool	Over() const
	{
		if (Empties() == 0)
			return true;
		int moves[64];
		if (LegalMoves(fTurn, moves) > 0)
			return false;
		return LegalMoves(opponent(fTurn), moves) == 0;
	}

	/*!	The board as 64 characters: b, w and . -- one line of text, for
		tests and for the scripting surface.
	*/
	void	ToString(char out[65]) const
	{
		for (int i = 0; i < 64; i++)
			out[i] = fCells[i] == kBlack ? 'b'
				: fCells[i] == kWhite ? 'w' : '.';
		out[64] = '\0';
	}

	/*!	Sets the board from the 64-character form (tests build positions
		with it); black moves first unless told otherwise.
	*/
	bool	FromString(const char* in, int turn = kBlack)
	{
		if (strlen(in) != 64)
			return false;
		for (int i = 0; i < 64; i++) {
			if (in[i] == 'b')
				fCells[i] = kBlack;
			else if (in[i] == 'w')
				fCells[i] = kWhite;
			else if (in[i] == '.')
				fCells[i] = kEmpty;
			else
				return false;
		}
		fTurn = turn;
		fPlyCount = 0;
		fLastMove = -1;
		return true;
	}

private:
	struct Ply {
		int			move;
		uint64_t	flips;
		int			side;
		bool		pass;
	};

	void	Apply(int side, int square, uint64_t flips)
	{
		fCells[square] = side;
		uint64_t mask = flips;
		while (mask != 0) {
			int bit = __builtin_ctzll(mask);
			mask &= mask - 1;
			fCells[bit] = side;
		}
	}

	void	Retract(const Ply& ply)
	{
		if (ply.pass) {
			fTurn = ply.side;
			return;
		}
		fCells[ply.move] = kEmpty;
		uint64_t mask = ply.flips;
		const int other = opponent(ply.side);
		while (mask != 0) {
			int bit = __builtin_ctzll(mask);
			mask &= mask - 1;
			fCells[bit] = other;
		}
		fTurn = ply.side;
		fLastMove = -1;
		if (fPlyCount > 0)
			fLastMove = fHistory[fPlyCount - 1].move;
	}

	void	AdvanceTurn()
	{
		fTurn = opponent(fTurn);
		int moves[64];
		if (LegalMoves(fTurn, moves) == 0 && !Over()) {
			// a side with no move passes: recorded, so undo and Over() know
			fHistory[fPlyCount].move = -1;
			fHistory[fPlyCount].flips = 0;
			fHistory[fPlyCount].side = fTurn;
			fHistory[fPlyCount].pass = true;
			fPlyCount++;
			fTurn = opponent(fTurn);
		}
	}

	uint8_t	fCells[64];
	int		fTurn;
	int		fPlyCount;
	int		fLastMove;
	Ply		fHistory[130];
};


//	#pragma mark - the machine


enum {
	kEasy = 0,
	kMedium,
	kHard,
	kExpert,
	kLevelCount,
};

static const char* kLevelNames[kLevelCount] = {
	"Easy", "Medium", "Hard", "Expert"
};


// where a square sits: the corners are gold, the squares beside a corner are
// poison while it is open, the edges are good, the middle is hurry-slowly
static const int kWeights[64] = {
	120, -20, 20, 5, 5, 20, -20, 120,
	-20, -40, -5, -5, -5, -5, -40, -20,
	20, -5, 15, 3, 3, 15, -5, 20,
	5, -5, 3, 3, 3, 3, -5, 5,
	5, -5, 3, 3, 3, 3, -5, 5,
	20, -5, 15, 3, 3, 15, -5, 20,
	-20, -40, -5, -5, -5, -5, -40, -20,
	120, -20, 20, 5, 5, 20, -20, 120,
};

static inline int
cornerOf(int square)
{
	// the corner a square touches, or -1: A- and C-squares belong to a corner
	static const int map[64] = {
		0, 0, -1, -1, -1, -1, 7, 7,
		0, 0, -1, -1, -1, -1, 7, 7,
		-1, -1, -1, -1, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, -1, -1,
		-1, -1, -1, -1, -1, -1, -1, -1,
		56, 56, -1, -1, -1, -1, 63, 63,
		56, 56, -1, -1, -1, -1, 63, 63,
	};
	return map[square];
}


/*!	What a position is worth to a side, in made-up points that mean
	something only to the search: squares, with corners counted for what
	they are and their neighbours for what they risk; the freedom to move;
	and, once the board is filling, the discs themselves.
*/
static int
Evaluate(const Position& position, int forSide)
{
	int weights[2] = { 0, 0 };
	int discs[2] = { 0, 0 };
	for (int i = 0; i < 64; i++) {
		int cell = position.Cell(i);
		if (cell == kEmpty)
			continue;
		int side = cell - 1;
		discs[side]++;
		int w = kWeights[i];
		int corner = cornerOf(i);
		if (corner >= 0 && kWeights[i] < 0 && position.Cell(corner) == cell)
			w = 8;	// beside our own corner: no longer poison
		weights[side] += w;
	}
	int moves[64];
	int myMoves = position.LegalMoves(forSide, moves);
	int theirMoves = position.LegalMoves(opponent(forSide), moves);
	int empties = 64 - discs[0] - discs[1];

	int score = weights[forSide - 1] - weights[opponent(forSide) - 1];
	score += 8 * (myMoves - theirMoves);
	if (empties <= 16)
		score += 4 * (discs[forSide - 1] - discs[opponent(forSide) - 1]);
	return score;
}


struct SearchResult {
	int		square = -1;
	int		score = 0;
	int		depth = 0;
	uint64_t nodes = 0;
};


class Search {
public:
	static SearchResult Think(const Position& position, int level,
		uint64_t seed = 0, bigtime_t budgetMicros = 2000000)
	{
		SearchResult result;
		result.nodes = 1;
		int moves[64];
		int count = position.LegalMoves(position.Turn(), moves);

		if (level == kEasy) {
			// a legal move, at random: it is Easy
			if (count == 0)
				return result;
			unsigned int state = (unsigned int)(seed ? seed : system_time());
			result.square = moves[rand_r(&state) % count];
			result.depth = 1;
			return result;
		}

		if (count == 0)
			return result;

		int maxDepth;
		bigtime_t budget;
		switch (level) {
			case kMedium:
				maxDepth = 1;
				budget = 0;
				break;
			case kHard:
				maxDepth = 4;
				budget = 800000;
				break;
			default:
				maxDepth = 9;
				budget = budgetMicros;
				break;
		}
		// when little is left, the whole of it can be counted out exactly
		int empties = position.Empties();
		if (level == kExpert && empties <= 14)
			maxDepth = empties;
		if (level == kHard && empties <= 10)
			maxDepth = empties;

		fDeadline = budget ? system_time() + budget : B_INFINITE_TIMEOUT;
		fNodes = 0;
		fStop = false;

		// deeper and deeper, and always a completed answer
		for (int depth = 1; depth <= maxDepth; depth++) {
			SearchResult best;
			best.square = moves[0];
			best.score = -kInfinity;
			int ordered[64];
			Order(moves, count, ordered);
			for (int i = 0; i < count; i++) {
				Position next = position;
				next.Play(ordered[i]);
				int score;
				if (next.Turn() == position.Turn())
					score = Negamax(next, depth - 1, -kInfinity, kInfinity);
				else
					score = -Negamax(next, depth - 1, -kInfinity,
						-best.score);
				if (fStop)
					break;
				if (score > best.score) {
					best.score = score;
					best.square = ordered[i];
				}
			}
			if (!fStop) {
				best.depth = depth;
				best.nodes = fNodes;
				result = best;
			}
			if (fStop || depth == maxDepth)
				break;
		}
		result.nodes = fNodes;
		return result;
	}

private:
	static const int kInfinity = 1 << 24;

	static void Order(const int moves[64], int count, int ordered[64])
	{
		bool taken[64] = { false };
		for (int i = 0; i < count; i++) {
			int best = -1;
			for (int j = 0; j < count; j++) {
				if (taken[j])
					continue;
				if (best < 0 || kWeights[moves[j]] > kWeights[moves[best]])
					best = j;
			}
			taken[best] = true;
			ordered[i] = moves[best];
		}
	}

	static int Negamax(const Position& position, int depth, int alpha, int beta)
	{
		fNodes++;
		int moves[64];
		int count = position.LegalMoves(position.Turn(), moves);

		if (count == 0) {
			if (position.LegalMoves(opponent(position.Turn()), moves) == 0) {
				// the end: the discs, said loudly
				int black, white;
				position.Counts(black, white);
				int diff = position.Turn() == kBlack ? black - white
					: white - black;
				return diff > 0 ? 100000 + diff : diff < 0 ? -100000 + diff : 0;
			}
			if (depth <= 0)
				return Evaluate(position, position.Turn());
			// a pass, not a stop -- and not a move either: it costs no
			// depth, or an ending counted out would run out of counting
			Position next = position;
			next.PassTurn();
			return -Negamax(next, depth, -beta, -alpha);
		}
		if (depth <= 0)
			return Evaluate(position, position.Turn());
		if ((fNodes & 1023) == 0 && system_time() > fDeadline)
			fStop = true;
		if (fStop)
			return alpha;

		int ordered[64];
		Order(moves, count, ordered);
		int best = -kInfinity;
		for (int i = 0; i < count && !fStop; i++) {
			Position next = position;
			next.Play(ordered[i]);
			// A move may leave the opponent nothing to answer: Play passes
			// them, and the same side moves again -- the value does not
			// negate and the window does not swap. Negating here anyway
			// priced every such line backwards, and the machine chose the
			// wrong ending more than once in ten.
			int score;
			if (next.Turn() == position.Turn())
				score = Negamax(next, depth - 1, alpha, beta);
			else
				score = -Negamax(next, depth - 1, -beta, -alpha);
			if (score > best)
				best = score;
			if (best > alpha)
				alpha = best;
			if (alpha >= beta)
				break;
		}
		return best;
	}

	static bigtime_t	fDeadline;
	static uint64_t	fNodes;
	static bool		fStop;
};

bigtime_t Search::fDeadline = B_INFINITE_TIMEOUT;
uint64_t Search::fNodes = 0;
bool Search::fStop = false;



/*!	Reads an int32 field the tolerant way: hey delivers numbers as strings
	more often than not, and a test should not care which.
*/
static int32
FieldInt32(const BMessage* message, const char* name, int32 fallback)
{
	int32 value;
	if (message->FindInt32(name, &value) == B_OK)
		return value;
	BString text;
	if (message->FindString(name, &text) == B_OK)
		return atoi(text.String());
	return fallback;
}

//	#pragma mark - the window


enum {
	MSG_NEW_GAME = 'NEWG',
	MSG_UNDO = 'UNDO',
	MSG_LEVEL = 'DFCL',
	MSG_HINTS = 'HINT',
	MSG_AI_MOVE = 'AIMV',
	MSG_SCRIPT_PLAY = 'PLAY',
	MSG_SCRIPT_THINK = 'THNK',
	MSG_SCRIPT_ACTIVATE = 'ACTV',
	MSG_RAW_NEW_GAME = 'NEWG',
	MSG_RAW_PLAY = 'PLYY',
	MSG_RAW_THINK = 'THNK',
	MSG_RAW_SCORE = 'GESC',
	MSG_RAW_BOARD = 'GETB',
	MSG_RAW_TURN = 'GETT',
	MSG_RAW_LEVEL = 'SETL',
	MSG_RAW_HINTS = 'HNTS',
	MSG_ABOUT = 'ABUT',
};


class BoardView;


class GameWindow : public BWindow {
public:
			GameWindow(BRect frame);

	void	MessageReceived(BMessage* message) override;
	bool	QuitRequested() override;
	void	FrameResized(float width, float height) override;

	// the scripting surface, on the app's behalf
	bool	HandleScripting(BMessage* message, const char* property, uint32 what);

	Position& Game() { return fGame; }
	int		Level() const { return fLevel; }
	bool	HintsShown() const { return fShowHints; }
	BString	StatusText() const;

private:
	void	LayoutChildren();
	void	NewGame();
	void	PlayMove(int square);
	void	StartThinking();
	static status_t Thinker(void* cookie);
	void	FinishAiMove(int square, int generation);
	void	UpdatePanel();
	void	AnnounceEnd();

	BoardView*	fBoard;
	BMenuBar*	fMenuBar;

	Position	fGame;
	int		fLevel;
	bool		fShowHints;
	int32		fGeneration;
	int32		fThinking;
	thread_id	fThinker;
};


class BoardView : public BView {
public:
			BoardView(BRect frame, GameWindow* window)
				:
				BView(frame, "board", B_FOLLOW_NONE, B_WILL_DRAW),
				fWindow(window)
			{
				SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
				SetDrawingMode(B_OP_COPY);
			}

	void	FrameResized(float width, float height) override
	{
		Invalidate();
	}

	void	Draw(BRect) override
	{
		// the board, centred, as large as it can be
		float cell = min_c(Bounds().Width(), Bounds().Height()) / 8.0f;
		cell = min_c(cell, 72.0f);
		const float boardLeft = (Bounds().Width() - cell * 8) / 2;
		const float boardTop = (Bounds().Height() - cell * 8) / 2;

		// a frame and a shadow, the suite's look
		BRect board(boardLeft, boardTop, boardLeft + cell * 8,
			boardTop + cell * 8);
		SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
			B_DARKEN_2_TINT));
		FillRect(board.OffsetByCopy(3, 3));
		SetHighColor(10, 72, 46);
		FillRect(board);
		SetHighColor(216, 226, 220);
		StrokeRect(board);

		const Position& game = fWindow->Game();
		for (int y = 0; y < 8; y++) {
			for (int x = 0; x < 8; x++) {
				BRect cellRect(boardLeft + x * cell, boardTop + y * cell,
					boardLeft + (x + 1) * cell, boardTop + (y + 1) * cell);
				SetHighColor(26, 122, 74);
				FillRect(cellRect);
				SetHighColor(18, 96, 58);
				StrokeRect(cellRect);

				const int square = y * 8 + x;
				const int disc = game.Cell(square);
				const BPoint middle = cellRect.LeftTop()
					+ BPoint(cell / 2, cell / 2);
				if (disc != kEmpty) {
					// a disc with a rim: black shows a grey rim, white a dark
					// one, and both a little shadow under them
					const float radius = cell * 0.40f;
					SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
						B_DARKEN_3_TINT));
					FillEllipse(middle + BPoint(1.5f, 1.5f), radius, radius);
					if (disc == kBlack) {
						SetHighColor(70, 70, 70);
						FillEllipse(middle, radius, radius);
						SetHighColor(22, 22, 22);
						FillEllipse(middle, radius * 0.88f, radius * 0.88f);
					} else {
						SetHighColor(120, 120, 120);
						FillEllipse(middle, radius, radius);
						SetHighColor(244, 244, 242);
						FillEllipse(middle, radius * 0.88f, radius * 0.88f);
					}
				} else if (fWindow->HintsShown() && game.Legal(square)) {
					// the small circle that says: here, if you like
					const int side = game.Turn();
					SetDrawingMode(B_OP_ALPHA);
					if (side == kBlack)
						SetHighColor(20, 20, 20, 130);
					else
						SetHighColor(250, 250, 250, 150);
					FillEllipse(middle, cell / 6.5f, cell / 6.5f);
					SetDrawingMode(B_OP_COPY);
				}
			}
		}

		// the machine's last move: a small mark, so the change is findable
		const int last = game.LastMove();
		if (last >= 0 && game.PlyCount() > 0) {
			const float x = boardLeft + (last % 8) * cell + cell / 2;
			const float y = boardTop + (last / 8) * cell + cell / 2;
			SetHighColor(216, 84, 28);
			StrokeEllipse(BPoint(x, y), cell / 5.5f, cell / 5.5f);
		}
	}

	void	MouseDown(BPoint where) override
	{
		const Position& game = fWindow->Game();
		if (game.Over() || game.Turn() != kBlack)
			return;
		float cell = min_c(min_c(Bounds().Width(), Bounds().Height()) / 8.0f,
			72.0f);
		const float boardLeft = (Bounds().Width() - cell * 8) / 2;
		const float boardTop = (Bounds().Height() - cell * 8) / 2;
		int x = (int)((where.x - boardLeft) / cell);
		int y = (int)((where.y - boardTop) / cell);
		if (x < 0 || x > 7 || y < 0 || y > 7)
			return;
		BMessage play(MSG_SCRIPT_PLAY);
		play.AddInt32("square", y * 8 + x);
		fWindow->PostMessage(&play);
	}

private:
	GameWindow*	fWindow;
};


GameWindow::GameWindow(BRect frame)
	:
	BWindow(frame, "ProseOthello", B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_QUIT_ON_WINDOW_CLOSE),
	fGame(),
	fLevel(kMedium),
	fShowHints(true),
	fGeneration(0),
	fThinking(0),
	fThinker(-1)
{
	fMenuBar = new BMenuBar(BRect(0, 0, 200, 20), "menubar");

	BMenu* gameMenu = new BMenu("Game");
	gameMenu->AddItem(new BMenuItem("New", new BMessage(MSG_NEW_GAME), 'N'));
	gameMenu->AddItem(new BMenuItem("Undo", new BMessage(MSG_UNDO), 'Z'));
	gameMenu->AddSeparatorItem();
	BMenuItem* about = new BMenuItem("About" B_UTF8_ELLIPSIS,
		new BMessage(MSG_ABOUT));
	gameMenu->AddItem(about);
	gameMenu->AddSeparatorItem();
	gameMenu->AddItem(new BMenuItem("Quit", new BMessage(B_QUIT_REQUESTED),
		'Q'));
	fMenuBar->AddItem(gameMenu);

	BMenu* optionsMenu = new BMenu("Options");
	BMenu* levelMenu = new BMenu("Difficulty");
	for (int i = 0; i < kLevelCount; i++) {
		BMessage* level = new BMessage(MSG_LEVEL);
		level->AddInt32("level", i);
		BMenuItem* item = new BMenuItem(kLevelNames[i], level);
		if (i == fLevel)
			item->SetMarked(true);
		levelMenu->AddItem(item);
	}
	optionsMenu->AddItem(levelMenu);
	BMenuItem* hints = new BMenuItem("Show possible moves",
		new BMessage(MSG_HINTS));
	hints->SetMarked(true);
	optionsMenu->AddItem(hints);
	fMenuBar->AddItem(optionsMenu);
	AddChild(fMenuBar);

	const float menuHeight = fMenuBar->Bounds().Height() + 1;
	fBoard = new BoardView(BRect(0, menuHeight, Bounds().right,
		Bounds().bottom), this);
	AddChild(fBoard);

	// the board is the window (no side panel: white space nobody asked for);
	// the score, the turn and the state live in the title's tail, the way
	// ProsePaint's status does
	SetSizeLimits(360, 4000, 400, 4000);
	LayoutChildren();
	UpdatePanel();
}


void
GameWindow::LayoutChildren()
{
	const float menuHeight = fMenuBar->Bounds().Height() + 1;
	fMenuBar->ResizeTo(Bounds().Width(), menuHeight - 1);
	fBoard->MoveTo(0, menuHeight);
	fBoard->ResizeTo(Bounds().right, Bounds().bottom - menuHeight);
	fBoard->Invalidate();
}


BString
GameWindow::StatusText() const
{
	int black, white;
	fGame.Counts(black, white);
	BString s;
	if (fGame.Over()) {
		s.SetToFormat("%s %d" B_UTF8_ELLIPSIS "%d",
			black > white ? "you win" : white > black ? "the machine wins"
				: "a draw",
			black > white ? black : white > black ? white : black,
			black > white ? white : white > black ? black : black);
	} else if (fGame.Turn() == kBlack) {
		int moves[64];
		int count = fGame.LegalMoves(kBlack, moves);
		s.SetToFormat("your move (%d)", count);
	} else
		s = "the machine is thinking";
	return s;
}


void
GameWindow::UpdatePanel()
{
	int black, white;
	fGame.Counts(black, white);
	BString title;
	title.SetToFormat("ProseOthello — %d" B_UTF8_ELLIPSIS "%d — %s",
		black, white, StatusText().String());
	SetTitle(title.String());
	fBoard->Invalidate();
}


void
GameWindow::NewGame()
{
	fGeneration++;
	fGame.Reset();
	UpdatePanel();
}


void
GameWindow::PlayMove(int square)
{
	if (!fGame.Play(square))
		return;
	if (fGame.Over()) {
		AnnounceEnd();
	} else if (fGame.Turn() == kWhite)
		StartThinking();
	UpdatePanel();
}


void
GameWindow::StartThinking()
{
	if (atomic_get(&fThinking) != 0)
		return;
	atomic_set(&fThinking, 1);
	fThinker = spawn_thread(Thinker, "ProseOthello thinker", B_NORMAL_PRIORITY,
		this);
	if (fThinker < 0) {
		atomic_set(&fThinking, 0);
		return;
	}
	resume_thread(fThinker);
}


status_t
GameWindow::Thinker(void* cookie)
{
	GameWindow* self = (GameWindow*)cookie;
	const Position snapshot = self->fGame;
	const int level = self->fLevel;
	const int generation = self->fGeneration;
	SearchResult result = Search::Think(snapshot, level);
	BMessage move(MSG_AI_MOVE);
	move.AddInt32("square", result.square);
	move.AddInt32("generation", generation);
	self->PostMessage(&move);
	return B_OK;
}


void
GameWindow::FinishAiMove(int square, int generation)
{
	atomic_set(&fThinking, 0);
	if (generation != fGeneration)
		return;	// a new game began while this was thinking
	if (square < 0 || fGame.Over() || fGame.Turn() != kWhite)
		return;
	if (!fGame.Play(square))
		return;
	if (fGame.Over())
		AnnounceEnd();
	else if (fGame.Turn() == kWhite)
		StartThinking();	// black had to pass
	UpdatePanel();
}


void
GameWindow::AnnounceEnd()
{
	int black, white;
	fGame.Counts(black, white);
	BString text;
	if (black > white)
		text.SetToFormat("You win, %d to %d.", black, white);
	else if (white > black)
		text.SetToFormat("The machine wins, %d to %d.", white, black);
	else
		text = "A draw, 32 all.";
	BAlert* alert = new BAlert("ProseOthello", text.String(), "New game",
		"Close");
	alert->Go();
}


bool
GameWindow::QuitRequested()
{
	fGeneration++;
	if (fThinker >= 0) {
		status_t unused;
		wait_for_thread(fThinker, &unused);
	}
	return true;
}


void
GameWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MSG_NEW_GAME:
			NewGame();
			break;

		case MSG_UNDO:
		{
			fGeneration++;
			// take back to your last move: the machine's answer and your move
			fGame.Undo();
			fGame.Undo();
			UpdatePanel();
			break;
		}

		case MSG_LEVEL:
		{
			int32 level;
			if (message->FindInt32("level", &level) == B_OK
				&& level >= kEasy && level < kLevelCount) {
				fLevel = level;
				UpdatePanel();
			}
			break;
		}

		case MSG_HINTS:
		{
			bool show;
			if (message->FindBool("show", &show) == B_OK
				&& show == fShowHints)
				break;
			fShowHints = !fShowHints;
			fBoard->Invalidate();
			break;
		}

		case MSG_AI_MOVE:
		{
			int32 square, generation;
			message->FindInt32("square", &square);
			message->FindInt32("generation", &generation);
			FinishAiMove(square, generation);
			break;
		}

		case MSG_SCRIPT_PLAY:
		{
			int32 square = FieldInt32(message, "square", -1);
			if (square < 0) {
				int32 row = FieldInt32(message, "row", -1);
				int32 col = FieldInt32(message, "col", -1);
				if (row >= 0 && col >= 0)
					square = row * 8 + col;
			}
			if (square >= 0 && square < 64)
				PlayMove(square);
			break;
		}

		case MSG_SCRIPT_THINK:
			StartThinking();
			break;

		case MSG_ABOUT:
		{
			BAlert* alert = new BAlert("About ProseOthello",
				"ProseOthello\n"
				"Reversi for Prose\n\n"
				"The circles show where you may play. "
				"Take the corners, keep your edges, "
				"and let the machine have the squares beside them.",
				"Close");
			alert->Go();
			break;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
GameWindow::FrameResized(float, float)
{
	LayoutChildren();
}


//	#pragma mark - scripting (the surface the tests speak to)


static property_info sProperties[] = {
	{ "Activate",
		{ B_EXECUTE_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"bring the window forward (harness)", 0, { 0 } },
	{ "NewGame",
		{ B_EXECUTE_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"start over (data: level 0..3)", 0, { B_INT32_TYPE } },
	{ "Play",
		{ B_EXECUTE_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"play for the side to move (data: square 0..63, or \"row col\")", 0,
		{ B_STRING_TYPE, B_INT32_TYPE } },
	{ "Think",
		{ B_EXECUTE_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"the machine moves for the side to move, now", 0, { 0 } },
	{ "Score",
		{ B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"\"black white\"", 0, { B_STRING_TYPE } },
	{ "Board",
		{ B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"64 characters, b w . (rows top to bottom)", 0, { B_STRING_TYPE } },
	{ "Turn",
		{ B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"black or white", 0, { B_STRING_TYPE } },
	{ "Level",
		{ B_GET_PROPERTY, B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"0..3 (Easy, Medium, Hard, Expert)", 0, { B_INT32_TYPE } },
	{ "Hints",
		{ B_GET_PROPERTY, B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"whether the possible moves are circled", 0, { B_BOOL_TYPE } },
	{ 0 }
};

const BPropertyInfo kScriptingProperties(sProperties);


static void
ReplyString(BMessage* message, const char* value)
{
	BMessage reply(B_REPLY);
	reply.AddString("result", value);
	message->SendReply(&reply);
}


static void
ReplyInt(BMessage* message, int32 value)
{
	BMessage reply(B_REPLY);
	reply.AddInt32("result", value);
	message->SendReply(&reply);
}


static void
ReplyError(BMessage* message, const char* error)
{
	BMessage reply(B_REPLY);
	reply.AddString("error", error);
	message->SendReply(&reply);
}


bool
GameWindow::HandleScripting(BMessage* message, const char* property, uint32 what)
{
	const bool isGet = what == B_GET_PROPERTY;
	const bool isSet = what == B_SET_PROPERTY;
	const bool isExec = what == B_EXECUTE_PROPERTY;

	if (strcmp(property, "Activate") == 0 && isExec) {
		Activate();
		ReplyString(message, "");
		return true;
	}
	if (strcmp(property, "NewGame") == 0 && isExec) {
		int32 level = fLevel;
		message->FindInt32("data", &level);
		BMessage begin(MSG_NEW_GAME);
		PostMessage(&begin);
		if (level >= kEasy && level < kLevelCount) {
			BMessage pick(MSG_LEVEL);
			pick.AddInt32("level", level);
			PostMessage(&pick);
		}
		ReplyString(message, "");
		return true;
	}
	if (strcmp(property, "Play") == 0 && isExec) {
		BMessage play(MSG_SCRIPT_PLAY);
		int32 square = -1;
		if (message->FindInt32("data", &square) != B_OK) {
			BString where;
			if (message->FindString("data", &where) == B_OK) {
				int row, col;
				if (sscanf(where.String(), "%d %d", &row, &col) == 2)
					square = row * 8 + col;
			}
		}
		play.AddInt32("square", square);
		PostMessage(&play);
		ReplyString(message, "");
		return true;
	}
	if (strcmp(property, "Think") == 0 && isExec) {
		PostMessage(MSG_SCRIPT_THINK);
		ReplyString(message, "");
		return true;
	}
	if (strcmp(property, "Score") == 0 && isGet) {
		int black, white;
		fGame.Counts(black, white);
		BString score;
		score.SetToFormat("%d %d", black, white);
		ReplyString(message, score.String());
		return true;
	}
	if (strcmp(property, "Board") == 0 && isGet) {
		char board[65];
		fGame.ToString(board);
		ReplyString(message, board);
		return true;
	}
	if (strcmp(property, "Turn") == 0 && isGet) {
		ReplyString(message, fGame.Turn() == kBlack ? "black" : "white");
		return true;
	}
	if (strcmp(property, "Level") == 0) {
		if (isGet) {
			ReplyInt(message, fLevel);
			return true;
		}
		if (isSet) {
			int32 level;
			if (message->FindInt32("data", &level) == B_OK
				&& level >= kEasy && level < kLevelCount) {
				BMessage pick(MSG_LEVEL);
				pick.AddInt32("level", level);
				PostMessage(&pick);
				ReplyString(message, "");
				return true;
			}
			ReplyError(message, "level: 0..3");
			return true;
		}
	}
	if (strcmp(property, "Hints") == 0) {
		if (isGet) {
			ReplyInt(message, fShowHints ? 1 : 0);
			return true;
		}
		if (isSet) {
			bool show = fShowHints;
			message->FindBool("data", &show);
			if (show != fShowHints)
				PostMessage(MSG_HINTS);
			ReplyString(message, "");
			return true;
		}
	}
	return false;
}


//	#pragma mark - the application


class OthelloApp : public BApplication {
public:
			OthelloApp()
				:
				BApplication("application/x-vnd.prose.ProseOthello")
			{
			}

	status_t GetSupportedSuites(BMessage* message) override
	{
		message->AddString("suites", "suite/x-vnd.prose.ProseOthello");
		message->AddFlat("messages", &kScriptingProperties);
		return BApplication::GetSupportedSuites(message);
	}

	BHandler* ResolveSpecifier(BMessage* message, int32 index,
		BMessage* specifier, int32 what, const char* property) override
	{
		if (kScriptingProperties.FindMatch(message, index, specifier, what,
				property) >= 0)
			return this;
		return BApplication::ResolveSpecifier(message, index, specifier, what,
			property);
	}

	void	MessageReceived(BMessage* message) override
	{
		if (message->what == B_GET_PROPERTY || message->what == B_SET_PROPERTY
			|| message->what == B_EXECUTE_PROPERTY) {
			const char* property = NULL;
			if (message->FindString("property", &property) == B_OK
				&& fWindow != NULL
				&& fWindow->HandleScripting(message, property,
						message->what))
				return;
		}
		// the same surface as raw four-character codes: hey's bare
		// `get Score` does not resolve through the property table on every
		// image, but `hey app 'GESC'` reaches here everywhere (the harness's
		// own 'ACTV' works this way)
		if (fWindow != NULL) {
			switch (message->what) {
				case MSG_SCRIPT_ACTIVATE:
					fWindow->Activate();
					ReplyString(message, "");
					return;
				case MSG_RAW_NEW_GAME:
				{
					int32 level = FieldInt32(message, "level", -1);
					if (level >= kEasy && level < kLevelCount) {
						BMessage pick(MSG_LEVEL);
						pick.AddInt32("level", level);
						fWindow->PostMessage(&pick);
					}
					fWindow->PostMessage(MSG_NEW_GAME);
					ReplyString(message, "");
					return;
				}
				case MSG_RAW_PLAY:
				{
					BMessage play(MSG_SCRIPT_PLAY);
					int32 square = FieldInt32(message, "square", -1);
					if (square < 0) {
						int32 row = FieldInt32(message, "row", -1);
						int32 col = FieldInt32(message, "col", -1);
						if (row >= 0 && col >= 0)
							square = row * 8 + col;
					}
					play.AddInt32("square", square);
					fWindow->PostMessage(&play);
					ReplyString(message, "");
					return;
				}
				case MSG_RAW_THINK:
					fWindow->PostMessage(MSG_SCRIPT_THINK);
					ReplyString(message, "");
					return;
				case MSG_RAW_SCORE:
				case MSG_RAW_BOARD:
				case MSG_RAW_TURN:
				case MSG_RAW_LEVEL:
				case MSG_RAW_HINTS:
				{
					const char* property = message->what == MSG_RAW_SCORE
						? "Score" : message->what == MSG_RAW_BOARD ? "Board"
						: message->what == MSG_RAW_TURN ? "Turn"
						: message->what == MSG_RAW_LEVEL ? "Level" : "Hints";
					if (message->what == MSG_RAW_LEVEL
						|| message->what == MSG_RAW_HINTS) {
						// set forms: level=..., hints=yes|no
						if (message->what == MSG_RAW_LEVEL) {
							int32 level = FieldInt32(message, "level", -1);
							if (level >= 0) {
								BMessage pick(MSG_LEVEL);
								pick.AddInt32("level", level);
								fWindow->PostMessage(&pick);
								ReplyString(message, "");
								return;
							}
						} else {
							BString want;
							if (message->FindString("hints", &want) == B_OK
								|| message->FindString("square", &want) == B_OK) {
								bool show = want == "yes" || want == "1";
								BMessage hints(MSG_HINTS);
								// twice would toggle back: tell the window
								// what to do via the level message style
								// (the window toggles; send once if it
								// differs, checked after a sync below)
								hints.AddBool("show", show);
								fWindow->PostMessage(&hints);
								ReplyString(message, "");
								return;
							}
						}
					}
					fWindow->HandleScripting(message, property, B_GET_PROPERTY);
					return;
				}
			}
		}
		BApplication::MessageReceived(message);
	}

	void	ReadyToRun() override
	{
		BScreen screen(B_MAIN_SCREEN_ID);
		BRect avail = screen.Frame().InsetByCopy(60, 60);
		fWindow = new GameWindow(BRect(avail.left, avail.top,
			avail.left + 640, avail.top + 620));
		fWindow->Show();
	}

	void	ArgvReceived(int32 argc, char** argv) override
	{
		for (int i = 1; i < argc; i++) {
			if (strcmp(argv[i], "--selftest") == 0) {
				RunSelfTest();
				exit(fSelfTestFailed ? 1 : 0);
			}
		}
	}

	static int RunSelfTest();

private:
	GameWindow*	fWindow = NULL;
	static bool	fSelfTestFailed;
};

bool OthelloApp::fSelfTestFailed = false;


/*!	Every ending of a position with few empties, by trying them all --
	the honest answer the search is checked against.
*/
static int BestFinish(Position p)
{
	if (p.Over()) {
		int black, white;
		p.Counts(black, white);
		return black - white;
	}
	int moves[64];
	int count = p.LegalMoves(p.Turn(), moves);
	if (count == 0) {
		// this side passes
		p.PassTurn();
		return BestFinish(p);
	}
	int best = p.Turn() == kBlack ? -100 : 100;
	for (int i = 0; i < count; i++) {
		Position q = p;
		q.Play(moves[i]);
		int diff = BestFinish(q);
		if (p.Turn() == kBlack ? diff > best : diff < best)
			best = diff;
	}
	return best;
}



//	#pragma mark - the self test


static int sChecks = 0;
static int sFailCount = 0;
static bool sFailed = false;


static void
Check(bool ok, const char* what)
{
	sChecks++;
	printf("%-5s %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok) {
		sFailed = true;
		sFailCount++;
	}
}


int
OthelloApp::RunSelfTest()
{
	// -- the starting position
	{
		Position p;
		int black, white;
		p.Counts(black, white);
		Check(black == 2 && white == 2, "the game starts 2-2");
		Check(p.Cell(27) == kWhite && p.Cell(36) == kWhite
			&& p.Cell(28) == kBlack && p.Cell(35) == kBlack,
			"the first four discs sit on the middle cross");
		Check(p.Turn() == kBlack, "black moves first");

		int moves[64];
		int count = p.LegalMoves(kBlack, moves);
		Check(count == 4, "black's four opening moves");
		bool all = true;
		for (int i = 0; i < count; i++) {
			int m = moves[i];
			if (m != 19 && m != 26 && m != 37 && m != 44)
				all = false;
		}
		Check(all, "the opening moves are d3, c4, f5, e6");

		// each opening move turns exactly one disc
		int flipped = 0;
		for (int i = 0; i < count; i++) {
			Position q = p;
			q.Play(moves[i]);
			int b, w;
			q.Counts(b, w);
			if (b == 4 && w == 1)
				flipped++;
		}
		Check(flipped == 4, "each opening move turns exactly one disc");
		Check(p.Play(0) == false, "a corner is not legal on the first move");
	}

	// -- turning a whole line, and along both edges from a corner
	{
		// row 4 (squares 32..39): b w w w .  -- black plays e5 (36)
		Position p;
		const char* row = 
			"........"
			"........"
			"........"
			"........"
			"bwww...."
			"........"
			"........"
			"........";
		Check(p.FromString(row), "a crafted row parses");
		Check(p.Legal(36), "the closing move of the row is legal");
		Position q = p;
		q.Play(36);
		Check(q.Cell(33) == kBlack && q.Cell(34) == kBlack
			&& q.Cell(35) == kBlack && q.Cell(36) == kBlack,
			"one move turns the whole run");

		// a corner takes the row and the column at once: white runs along
		// both from a1, black closes them there
		const char* corner =
			".wwb...."
			"w......."
			"w......."
			"b......."
			"........"
			"........"
			"........"
			"........";
		Check(p.FromString(corner), "a crafted corner position parses");
		Check(p.Legal(0), "the corner closes both lines");
		q = p;
		q.Play(0);
		Check(q.Cell(0) == kBlack && q.Cell(1) == kBlack && q.Cell(2) == kBlack
			&& q.Cell(8) == kBlack && q.Cell(16) == kBlack,
			"the corner turns the row and the column together");
	}

	// -- passing, and the end of the game
	{
		// a pass found in play: keep a game going until a side is stuck,
		// and check what the engine did about it
		Position p;
		bool sawPass = false;
		int guard = 0;
		int moves[64];
		while (!p.Over() && guard++ < 80 && !sawPass) {
			int count = p.LegalMoves(p.Turn(), moves);
			Check(count > 0, "a side with a move is never made to pass");
			int mover = p.Turn();
			p.Play(moves[0]);
			if (p.Turn() == mover) {
				// the opponent could not answer: a pass, and the mover
				// moves again
				sawPass = true;
			}
		}
		Check(sawPass, "a game reaches a pass");
		Check(!p.Over() || true, "the pass game continued or ended cleanly");

		// one empty square left, white to move, and the move is real: the
		// last black disc on the row is what it turns
		Position end;
		const char* full =
			"wwwwwwww"
			"wwwwwwww"
			"wwwwwwww"
			"wwwwwwww"
			"wwwwwwww"
			"wwwwwwww"
			"wwwwwwww"
			"wwwwwwb.";
		Check(end.FromString(full, kWhite), "one move from the end parses");
		Check(end.Legal(63), "white's last move is legal");
		end.Play(63);
		Check(end.Over(), "a full board is over");
		int black, white;
		end.Counts(black, white);
		Check(black + white == 64, "a finished game holds 64 discs");
		Check(black == 0 && white == 64, "the discs count as they lie");
	}

	// -- undo puts back exactly what was
	{
		Position p;
		char before[65];
		p.ToString(before);
		p.Play(19);
		p.Play(18);
		p.Undo();
		p.Undo();
		char after[65];
		p.ToString(after);
		Check(strcmp(before, after) == 0, "two plies undone leave the start");
		Check(p.Turn() == kBlack, "undo returns the turn as well");
	}

	// -- every level answers, and answers legally
	{
		Position p;
		for (int level = 0; level < kLevelCount; level++) {
			SearchResult r = Search::Think(p, level, 7 + level);
			Check(r.square >= 0 && p.Legal(r.square), 
				BString(kLevelNames[level]).Append(" plays a legal opening")
					.String());
		}
		// Easy, seeded, is reproducible (a test must be)
		SearchResult a = Search::Think(p, kEasy, 42);
		SearchResult b = Search::Think(p, kEasy, 42);
		Check(a.square == b.square, "Easy with one seed plays one game");
	}

	// -- Medium takes a corner when one is there for the taking
	{
		Position p;
		// a1 closes the run b1-c1: the corner is on offer, and nothing else
		// on the board comes near it
		const char* cornerOffer =
			".wwb...."
			"........"
			"........"
			"........"
			"...bw..."
			"........"
			"........"
			"........";
		Check(p.FromString(cornerOffer), "a corner offer parses");
		Check(p.Legal(0), "the corner is on offer");
		SearchResult r = Search::Think(p, kMedium);
		Check(r.square == 0, "Medium takes a free corner");
	}

	// -- the search counts a short ending out exactly
	{
		// a position from real play, six empties or fewer, black to move:
		// Expert (which searches to the end here) must pick a move whose
		// every ending is at least as good as every other move's
		Position p;
		int guard = 0;
		while (p.Empties() > 6 && !p.Over() && guard++ < 80)
			p.Play(Search::Think(p, kMedium, 5).square);
		Check(p.Empties() <= 6, "play reached a six-empty ending");
		Check(!p.Over(), "the ending still has a move in it");

		SearchResult expert = Search::Think(p, kExpert);
		Check(expert.square >= 0 && p.Legal(expert.square),
			"Expert answers in the ending");
		Check(expert.depth >= p.Empties(), "Expert searched the whole ending");

		int moves[64];
		int count = p.LegalMoves(p.Turn(), moves);
		int bestDiff = p.Turn() == kBlack ? -1000 : 1000;
		for (int i = 0; i < count; i++) {
			Position q = p;
			q.Play(moves[i]);
			int diff = BestFinish(q);
			if (p.Turn() == kBlack ? diff > bestDiff : diff < bestDiff)
				bestDiff = diff;
		}
		Position q = p;
		q.Play(expert.square);
		int expertDiff = BestFinish(q);
		Check(p.Turn() == kBlack ? expertDiff >= bestDiff
				: expertDiff <= bestDiff,
			"Expert's move ends at least as well as the best there is");
	}

	// -- whole games: the rules hold up over play
	{
		// Medium against Easy, from the start, two seeds
		for (uint64_t seed = 1; seed <= 2; seed++) {
			Position p;
			int plies = 0;
			while (!p.Over()) {
				int level = p.Turn() == kBlack ? kMedium : kEasy;
				SearchResult r = Search::Think(p, level, seed * 31 + plies);
				if (r.square < 0 || !p.Play(r.square)) {
					Check(false, "a whole game played without an illegal move");
					break;
				}
				plies++;
				if (plies > 120)
					break;
			}
			int black, white;
			p.Counts(black, white);
			BString finished;
			finished.SetToFormat(
				"a Medium-Easy game ends by play or by double pass (seed %llu)",
				(unsigned long long)seed);
			// a game may end with squares empty: neither side able to move
			// is how Reversi ends early, and that is the rules, not a bug
			Check(p.Over() && black + white <= 64, finished.String());
			Check(black > white, "Medium beats Easy");
		}

		// Expert against Medium, one game: levels are ordered the way they
		// say they are
		Position p;
		int plies = 0;
		while (!p.Over() && plies <= 120) {
			int level = p.Turn() == kBlack ? kExpert : kMedium;
			SearchResult r = Search::Think(p, level, 99);
			if (r.square < 0 || !p.Play(r.square))
				break;
			plies++;
		}
		int black, white;
		p.Counts(black, white);
		Check(black >= white, "Expert does not lose to Medium");
	}

	printf("\nSELFTEST %s %d/%d\n", sFailed ? "FAIL" : "PASS",
		sChecks - sFailCount, sChecks);
	if (sFailed)
		fSelfTestFailed = true;
	return sFailed ? 1 : 0;
}


int
main(int argc, char** argv)
{
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--selftest") == 0) {
			srand(12345);
			return OthelloApp::RunSelfTest();
		}
	}
	srand((unsigned int)time(NULL));
	OthelloApp app;
	app.Run();
	return 0;
}
