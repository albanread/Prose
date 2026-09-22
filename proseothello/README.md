# ProseOthello — Reversi for Prose

You are black, the machine is white, and every square you may play shows a
small circle: the board says what is possible, you decide what is good.

Four levels (Options ▸ Difficulty):

| | |
|---|---|
| Easy | a legal move at random — it will hand you the game |
| Medium | takes whatever looks best right now: corners, edges, freedom to move |
| Hard | alpha-beta, four moves ahead |
| Expert | alpha-beta with iterative deepening to a two-second budget, and the ending counted out exactly when fourteen squares or fewer remain |

Game ▸ New (⌘N) starts over, Game ▸ Undo (⌘Z) takes back your move and the
machine's answer, and the machine's last move carries a small orange ring so
the change is findable at a glance. The score sits beside the board, and the
window's title tail says whose move it is.

## The engine

The rules are one class (`Position` in `app/src/Othello.cpp`): 64 cells, the
turn, and a history that records each ply's move and a mask of the discs it
turned, so undo is exact. A side with no move passes, recorded the same way;
neither side able to move is the end, full board or not — both are Reversi's
rules, not special cases.

The machine's search is negamax with alpha-beta and move ordering
(corner-first), iterative deepening under a time budget, an evaluation of
squares (corners gold, their neighbours poison while the corner is open,
edges good) plus the freedom to move, and the discs themselves once the board
fills. A forced pass costs no search depth and does not negate the value —
the line is still the same player's to play. That last rule was found the
hard way: without it the machine priced every pass-line backwards and chose
wrong endings, which the self test now pins down against looking at every
ending there is.

## Tests

`ProseOthello --selftest` checks the rules (opening moves, whole-line and
corner captures, passing, the end, undo), every level (legal answers, seeded
reproducibility, Medium takes a free corner), the search (Expert's ending
matches exhaustive enumeration), and whole games (Medium beats Easy, Expert
does not lose to Medium). 58 checks.

The same surface speaks to the window for smoke tests (`hey`): `NEWG`
(level=), `PLYY` (square=, or row= col=), `THNK`, `GESC`/`GETB`/`GETT` for
score/board/turn, `SETL`, `HNTS`, `ACTV`. Numbers are read tolerantly — hey
delivers them as strings as often as not.

## Building

```
cd proseothello/app
make ProseOthello          # the ProseWriter sysroot and the cross compiler
```
