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
#include "Uci.h"

#include <cstdlib>
#include <sstream>

namespace schach {
namespace core {
namespace uciproto {

namespace {

bool toLong(const std::string& text, long long& out)
{
    if (text.empty())
        return false;
    char* end = nullptr;
    const long long value = std::strtoll(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0')
        return false;
    out = value;
    return true;
}

bool toInt(const std::string& text, int& out)
{
    long long value = 0;
    if (!toLong(text, value))
        return false;
    out = static_cast<int>(value);
    return true;
}

} // namespace

std::vector<std::string> tokenize(const std::string& line)
{
    std::vector<std::string> out;
    std::string token;
    std::istringstream stream(line);
    while (stream >> token)
        out.push_back(token);
    return out;
}

std::string cmdUci() { return "uci"; }
std::string cmdIsReady() { return "isready"; }
std::string cmdNewGame() { return "ucinewgame"; }
std::string cmdStop() { return "stop"; }
std::string cmdQuit() { return "quit"; }

std::string cmdSetOption(const std::string& name, const std::string& value)
{
    std::string out = "setoption name " + name;
    if (!value.empty())
        out += " value " + value;
    return out;
}

std::string cmdSetOption(const std::string& name, int value)
{
    return cmdSetOption(name, std::to_string(value));
}

std::string cmdPosition(const std::string& fen, const std::vector<std::string>& moves)
{
    std::string out;
    if (fen.empty() || fen == "startpos")
        out = "position startpos";
    else
        out = "position fen " + fen;
    if (!moves.empty()) {
        out += " moves";
        for (const std::string& move : moves)
            out += " " + move;
    }
    return out;
}

std::string cmdGoNodes(long long nodes)
{
    return "go nodes " + std::to_string(nodes < 1 ? 1 : nodes);
}

std::string cmdGoMovetime(int milliseconds)
{
    return "go movetime " + std::to_string(milliseconds < 1 ? 1 : milliseconds);
}

std::string cmdGoDepth(int depth)
{
    return "go depth " + std::to_string(depth < 1 ? 1 : depth);
}

LineKind classify(const std::string& line)
{
    const std::vector<std::string> tokens = tokenize(line);
    if (tokens.empty())
        return LineKind::Unknown;
    const std::string& head = tokens.front();
    if (head == "info")
        return LineKind::Info;
    if (head == "bestmove")
        return LineKind::BestMove;
    if (head == "uciok")
        return LineKind::UciOk;
    if (head == "readyok")
        return LineKind::ReadyOk;
    if (head == "id")
        return LineKind::Id;
    if (head == "option")
        return LineKind::Option;
    return LineKind::Unknown;
}

bool parseInfo(const std::string& line, Info& out)
{
    const std::vector<std::string> tokens = tokenize(line);
    if (tokens.size() < 2 || tokens[0] != "info")
        return false;
    // "info string …" is free text; it never carries a score and must not be
    // half-parsed into one.
    if (tokens[1] == "string")
        return false;

    Info info;
    bool sawAnything = false;

    for (std::size_t i = 1; i < tokens.size(); ++i) {
        const std::string& key = tokens[i];
        if (key == "depth" && i + 1 < tokens.size()) {
            info.hasDepth = toInt(tokens[++i], info.depth);
            sawAnything = true;
        } else if (key == "seldepth" && i + 1 < tokens.size()) {
            toInt(tokens[++i], info.seldepth);
            sawAnything = true;
        } else if (key == "multipv" && i + 1 < tokens.size()) {
            toInt(tokens[++i], info.multipv);
            sawAnything = true;
        } else if (key == "nodes" && i + 1 < tokens.size()) {
            toLong(tokens[++i], info.nodes);
            sawAnything = true;
        } else if (key == "nps" && i + 1 < tokens.size()) {
            toLong(tokens[++i], info.nps);
            sawAnything = true;
        } else if (key == "time" && i + 1 < tokens.size()) {
            toLong(tokens[++i], info.timeMs);
            sawAnything = true;
        } else if (key == "hashfull" && i + 1 < tokens.size()) {
            toInt(tokens[++i], info.hashfull);
            sawAnything = true;
        } else if (key == "tbhits" && i + 1 < tokens.size()) {
            toLong(tokens[++i], info.tbhits);
            sawAnything = true;
        } else if (key == "currmove" && i + 1 < tokens.size()) {
            info.currmove = tokens[++i];
            sawAnything = true;
        } else if (key == "currmovenumber" && i + 1 < tokens.size()) {
            toInt(tokens[++i], info.currmovenumber);
            sawAnything = true;
        } else if (key == "score" && i + 2 < tokens.size()) {
            const std::string& kind = tokens[i + 1];
            int value = 0;
            if (kind == "cp" && toInt(tokens[i + 2], value)) {
                info.score = Score::centipawns(value);
                i += 2;
                sawAnything = true;
            } else if (kind == "mate" && toInt(tokens[i + 2], value)) {
                info.score = Score::mate(value);
                i += 2;
                sawAnything = true;
            }
        } else if (key == "lowerbound") {
            info.lowerbound = true;
        } else if (key == "upperbound") {
            info.upperbound = true;
        } else if (key == "pv") {
            // The principal variation runs to the end of the line.
            for (std::size_t j = i + 1; j < tokens.size(); ++j)
                info.pv.push_back(tokens[j]);
            i = tokens.size();
            sawAnything = true;
        }
    }

    if (!sawAnything)
        return false;
    out = info;
    return true;
}

bool parseBestMove(const std::string& line, BestMove& out)
{
    const std::vector<std::string> tokens = tokenize(line);
    if (tokens.size() < 2 || tokens[0] != "bestmove")
        return false;
    BestMove best;
    best.move = tokens[1];
    for (std::size_t i = 2; i + 1 < tokens.size(); ++i) {
        if (tokens[i] == "ponder")
            best.ponder = tokens[i + 1];
    }
    out = best;
    return true;
}

bool parseId(const std::string& line, std::string& key, std::string& value)
{
    const std::vector<std::string> tokens = tokenize(line);
    if (tokens.size() < 3 || tokens[0] != "id")
        return false;
    key = tokens[1];
    value.clear();
    for (std::size_t i = 2; i < tokens.size(); ++i) {
        if (!value.empty())
            value += ' ';
        value += tokens[i];
    }
    return true;
}

} // namespace uciproto
} // namespace core
} // namespace schach
