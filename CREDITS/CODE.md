# Code credits

Third-party source code that was copied into this tree or that this tree was
written after, with its origin, licence and the changes made to it. Code that
is merely *linked* (Qt, Silica, Sailfish Secrets) or vendored unchanged with
its own licence file (`third_party/`) is not repeated here.

## Sailfish Secrets token store — salichess

| | |
|---|---|
| Our files | `src/SecretsTokenStore.h`, `src/SecretsTokenStore.cpp`, and the `TokenStore` interface in `src/TokenStore.h` |
| Origin | `src/core/secretstokenstore.{h,cpp}` and `src/core/tokenstore.h` of **salichess** |
| URL | https://github.com/van-ess0/salichess |
| Author | van-ess0 |
| Retrieved | 2026-09-17 |
| Licence | **GPL-3.0-or-later** — the same licence as this project, so the code may be taken over as it stands |

What was taken: the shape of the thing. An abstract `TokenStore` that the rest
of the app talks to, one backend that keeps the Lichess access token in an
owner-only, device-lock collection of the default encrypted storage plugin, and
the synchronous request pattern (`startRequest()` / `waitForFinished()`), which
is acceptable because it only ever runs at start, at login and at logout.

What was changed:

* The collection name is ours (`orgsmatkovischachlehrer`; the plugin allows at
  most 31 alphanumeric characters).
* `available()` was added, and `makeTokenStore()` uses it: where the Secrets
  daemon does not answer, the app falls back to a file with `0600` —
  chess-spec/platform.md §3.3 names both places and the fallback is the
  documented minimum, not an invention.
* A one-time migration carries a token that an older installation of this app
  left in `lichess.token` into Secrets and then deletes the file.
* `describe()` and `encrypted()` were added so that `SettingsPage.qml` can tell
  the user which of the two places their key actually ended up in. An app that
  silently uses the weaker one is lying by omission.
* Comment style, namespace (`schach`), naming and the GPL header follow this
  tree.

## Offline puzzle pool — salichess

| | |
|---|---|
| Our files | `src/PuzzleFeed.h`, `src/PuzzleFeed.cpp` |
| Origin | `src/lichess/puzzlestore.{h,cpp}` of **salichess** |
| URL | https://github.com/van-ess0/salichess |
| Licence | **GPL-3.0-or-later** |

What was taken: the shape. A pool of puzzles kept on the phone so they can be
played without a connection, topped up from `GET /api/puzzle/batch/{angle}`
while the app is online — fifty per request, deduplicated by puzzle id, written
to a file in the app's own data directory, and refilled whenever it is drawn
down.

What was changed:

* The five difficulty levels are asked **in turn**. salichess fills its pool at
  one difficulty, because that is what the player chose; this app needs a
  spread from about 700 to 2 200 (teacher.md §4.2), and the round-robin is what
  produces it.
* Puzzles are **converted** on arrival into this app's item format — FEN,
  line, dimension, difficulty, one German sentence — instead of being stored as
  Lichess puzzles. The placement test picks by dimension and difficulty and
  knows nothing about themes.
* It is **off until the learner turns it on**, and the app is complete without
  it. teacher.md §0.2 makes "everything works offline" a non-goal to break;
  salichess is a Lichess client and has no such constraint.
* Solved puzzles are not reported back to Lichess. salichess queues results and
  flushes them so the player's puzzle rating moves; this app measures with its
  own estimator (§4.4) and has nothing to send.

## What was deliberately *not* taken from salichess

`src/lichess/puzzlelogic.{h,cpp}` solves a Lichess puzzle by playing the
opponent's reply onto the board automatically. That is the right model for a
Lichess client and the wrong one here: chess-spec/teacher.md §6.5 quotes the
Stappenmethode on exactly this ("There is no need to look at alternatives.
**Superficiality is trumps**") and requires the whole line to be entered before
anything is executed. Our `core::SolutionLine` keeps salichess' data model —
alternating player and opponent moves, a mate by any move counts as solved —
and rejects its interaction. See `src/core/SolutionLine.h`.
