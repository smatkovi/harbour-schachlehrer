/*
    Copyright (C) 2026 smatkovi

    This file is part of harbour-schachlehrer.

    harbour-schachlehrer is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    harbour-schachlehrer is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with harbour-schachlehrer. If not, see <https://www.gnu.org/licenses/>.

    SPDX-License-Identifier: GPL-3.0-or-later
*/
#ifndef SCHACH_CORE_UCI_H
#define SCHACH_CORE_UCI_H

// The UCI text protocol as pure functions: build a command, parse a line.
// No process, no file descriptor, no clock — EngineProcess owns all of that
// (docs/design.md §2, platform.md §1.9). Keeping the protocol here is what
// makes it testable without an engine binary, which matters because the phone
// may not have one (Sailjail, missing file).

#include "WinProb.h"

#include <string>
#include <vector>

namespace schach {
namespace core {
namespace uciproto {

// --- commands ---------------------------------------------------------------

std::string cmdUci();
std::string cmdIsReady();
std::string cmdNewGame();
std::string cmdSetOption(const std::string& name, const std::string& value);
std::string cmdSetOption(const std::string& name, int value);
// `moves` are UCI strings applied to `fen`; an empty FEN means the start position.
std::string cmdPosition(const std::string& fen, const std::vector<std::string>& moves);
// teacher.md §2.2 recommends a fixed node count rather than a fixed depth, so
// that the diagnosis gives the same answer on a fast and on a slow device.
std::string cmdGoNodes(long long nodes);
std::string cmdGoMovetime(int milliseconds);
std::string cmdGoDepth(int depth);
std::string cmdStop();
std::string cmdQuit();

// --- parsing ----------------------------------------------------------------

enum class LineKind {
    Unknown = 0,
    Id,
    Option,
    UciOk,
    ReadyOk,
    Info,
    BestMove
};

LineKind classify(const std::string& line);

// One `info` line. Only the fields the app uses are kept; anything else is
// ignored rather than rejected, because engines add fields freely.
struct Info {
    bool hasDepth = false;
    int depth = 0;
    int seldepth = 0;
    int multipv = 1;         // 1-based, as in the protocol
    Score score;             // cp or mate, from the side to move's point of view
    bool lowerbound = false;
    bool upperbound = false;
    long long nodes = 0;
    long long nps = 0;
    long long timeMs = 0;
    int hashfull = -1;
    long long tbhits = 0;
    std::string currmove;
    int currmovenumber = 0;
    std::vector<std::string> pv;

    std::string bestMove() const { return pv.empty() ? std::string() : pv.front(); }
};

// False for lines that are not `info` lines at all, and for `info string …`,
// which carries engine chatter and never a score.
bool parseInfo(const std::string& line, Info& out);

struct BestMove {
    std::string move;     // "e2e4", or "(none)" when the engine has no move
    std::string ponder;
};

bool parseBestMove(const std::string& line, BestMove& out);

// "id name Stockfish 17.1" -> ("name", "Stockfish 17.1")
bool parseId(const std::string& line, std::string& key, std::string& value);

// Splitting on whitespace, needed by the parsers and by the tests.
std::vector<std::string> tokenize(const std::string& line);

} // namespace uciproto
} // namespace core
} // namespace schach

#endif // SCHACH_CORE_UCI_H
