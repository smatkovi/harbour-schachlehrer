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
// The 50 detection rules of teacher.md §2.6.
//
// Two kinds of case, because the rules have two kinds of input:
//
//   1. Real positions run through buildFeatures(), with the engine verdict
//      supplied by hand. These check the board half — SEE, the hanging list
//      and the motif predicates of §2.5 — against positions one can set up on
//      a board and look at.
//   2. One constructed feature vector per class, on a real FEN, for all 50.
//      That is where the engine-only and game-level facts live (mate distance,
//      the position of the played move in the main line, clock data, the
//      opening counters); a detector for those cannot be written any other way
//      than by handing it the numbers the Analyser will hand it.
//
// And the other half of the job: a clean move must fire nothing at all. A
// taxonomy that labels sound moves is worse than none.
#include "core/Taxonomy.h"
#include "core/Skill.h"

#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

using namespace schach::core;

namespace {

int failures = 0;

void check(bool ok, const char* what, int line)
{
    if (!ok) {
        std::fprintf(stderr, "FAILED line %d: %s\n", line, what);
        if (++failures > 40)
            std::exit(1);
    }
}
#define CHECK(x) check((x), #x, __LINE__)

bool contains(const std::vector<Finding>& findings, ErrorClass cls)
{
    for (const Finding& finding : findings) {
        if (finding.cls == cls)
            return true;
    }
    return false;
}

std::string listOf(const std::vector<Finding>& findings)
{
    std::string out;
    for (const Finding& finding : findings) {
        if (!out.empty())
            out += ",";
        out += errorKey(finding.cls);
    }
    return out.empty() ? std::string("-") : out;
}

// --- part 1: real positions -------------------------------------------------

struct BoardCase {
    const char* name;
    const char* fen;
    const char* played;
    const char* best;
    const char* reply;
    int cpBefore;
    int cpAfter;
    bool mateAgainstAfter;
    int mateInAfter;
    ErrorClass expect;
};

void runBoardCase(const BoardCase& item)
{
    Position before(item.fen);
    CHECK(before.isLegal(item.played));

    GameContext game;
    game.learnerElo = 1500;
    game.learnerIsWhite = before.whiteToMove();
    game.ply = 30;

    EngineView engine;
    engine.before = Score::centipawns(item.cpBefore);
    engine.after = item.mateAgainstAfter ? Score::mate(item.mateInAfter)
                                         : Score::centipawns(item.cpAfter);
    engine.nullBefore = engine.before;   // nothing was threatened beforehand
    engine.nullAfter = engine.after;
    engine.best = item.best;
    engine.opponentReply = item.reply;
    engine.refutationConfirmed = true;

    const MoveFeatures features = buildFeatures(before, item.played, engine, game);
    const std::vector<Finding> findings = classify(features, game.learnerElo);
    const bool ok = contains(findings, item.expect);
    if (!ok) {
        std::fprintf(stderr, "FAILED board case %s: expected %s, got %s (motif %s)\n", item.name,
                     errorKey(item.expect), listOf(findings).c_str(), motifKey(features.replyMotif));
        ++failures;
    } else {
        std::printf("  %-26s -> %-4s %s\n", item.name, errorKey(item.expect),
                    findings.front().sentence.c_str());
    }
}

const BoardCase kBoardCases[] = {
    // The knight steps onto a square the c5 pawn covers; after cxd4 it is gone.
    { "Springer eingestellt", "4k3/8/8/2p5/8/8/4N3/4K3 w - - 0 1",
      "e2d4", "e2c3", "c5d4", 0, -300, false, 0, ErrorClass::A1 },
    // The knight forks both rooks. Nothing was hanging before the move.
    { "Gabel auf zwei Tuerme", "7k/8/8/1n6/4R3/8/R6P/4K3 w - - 0 1",
      "h2h3", "e4e5", "b5c3", 0, -300, false, 0, ErrorClass::A3 },
    // The same fork with the king in it: a different class, because it always
    // costs — the king has to move first (§2.6 A3k).
    { "Familiengabel", "7k/8/8/1n6/4R3/8/7P/3K4 w - - 0 1",
      "h2h3", "e4e5", "b5c3", 0, -300, false, 0, ErrorClass::A3k },
    // Bg4 pins the knight in front of the queen.
    { "In die Fesselung gezogen", "4k3/8/8/7b/8/5N2/8/3Q2K1 w - - 0 1",
      "g1h1", "f3e5", "h5g4", 0, -300, false, 0, ErrorClass::A4 },
    // Rd8 skewers king and queen on the d-file.
    { "Spiess auf der d-Linie", "r6k/8/8/3K4/8/8/7P/3Q4 w - - 0 1",
      "h2h3", "d5e5", "a8d8", 0, -300, false, 0, ErrorClass::A5 },
    // The knight steps aside and uncovers the bishop on the long diagonal.
    { "Abzug auf den Turm", "4k3/1b6/8/3n4/8/8/P5R1/6K1 w - - 0 1",
      "a2a3", "g2d2", "d5f4", 0, -300, false, 0, ErrorClass::A6 },
    // The bishop on d5 is simply free and stays there.
    { "Freie Figur nicht genommen", "4k3/8/8/3b4/8/8/8/3QK3 w - - 0 1",
      "e1f1", "d1d5", "d5g2", 300, 0, false, 0, ErrorClass::C2 },
    // Back rank: the king sits behind three unmoved pawns, Ra1 is mate.
    { "Grundreihenmatt zugelassen", "6k1/5ppp/8/8/8/7Q/r4PPP/6K1 w - - 0 1",
      "h3h4", "h3d7", "a2a1", 0, 0, true, -1, ErrorClass::B3 },
};

// --- part 2: one feature vector per class ----------------------------------

// A neutral middlegame error: 25 pp lost, nothing specific about it. On its
// own it has to come out as Z0 — that is the baseline every case starts from.
void baseline(MoveFeatures& m, GameContext& game)
{
    game = GameContext();
    game.learnerElo = 1500;
    game.learnerIsWhite = true;
    game.ply = 30;
    game.moveTimeMs = 20000;
    game.medianMoveTimeMs = 20000.0;

    m = MoveFeatures();
    m.game = &game;
    m.fen = "r2q1rk1/pp2ppbp/2n3p1/8/3PP3/2N2N2/PP3PPP/R2Q1RK1 w - - 0 12";
    m.played = "a2a3";
    m.best = "d4d5";
    m.opponentReply = "c6b4";
    m.before = Score::centipawns(30);
    m.after = Score::centipawns(-120);
    m.wcBefore = 0.11;
    m.wcAfter = -0.36;
    m.dW = 25.0;
    m.phase = Phase::Middlegame;
    m.ply = 30;
    m.moveTimeMs = 20000;
    m.pieceCount = 24;
    m.movedPiece = 'P';
    m.refutationConfirmed = true;
}

typedef void (*Mutator)(MoveFeatures&, GameContext&);

struct ClassCase {
    ErrorClass cls;
    Mutator mutate;
};

void mA1(MoveFeatures& m, GameContext&)
{
    Hanging knight;
    knight.square = squareFromName("f3");
    knight.value = 3;
    knight.piece = 'N';
    knight.see = 3;
    m.newlyHanging.push_back(knight);
    m.hangingAfter.push_back(knight);
    m.replyMotif = Motif::Hanging;
}
void mA2(MoveFeatures& m, GameContext&)
{
    m.playedIsCapture = true;
    m.seePlayed = -2;
    m.played = "f3e5";
    m.capturedPiece = 'p';
}
void mA3(MoveFeatures& m, GameContext&) { m.replyMotif = Motif::Fork; }
void mA3k(MoveFeatures& m, GameContext&)
{
    m.replyMotif = Motif::Fork;
    m.replyIsCheck = true;
}
void mA4(MoveFeatures& m, GameContext&) { m.replyMotif = Motif::Pin; }
void mA5(MoveFeatures& m, GameContext&) { m.replyMotif = Motif::Skewer; }
void mA6(MoveFeatures& m, GameContext&) { m.replyMotif = Motif::DiscoveredCheck; }
void mA7(MoveFeatures& m, GameContext&) { m.replyMotif = Motif::Trapped; }
void mA8(MoveFeatures& m, GameContext&) { m.replyMotif = Motif::Overloading; }
void mA9(MoveFeatures& m, GameContext&) { m.replyMotif = Motif::InBetween; }
void mA10(MoveFeatures& m, GameContext&)
{
    m.playedIsCapture = true;
    m.movedPiece = 'Q';
    m.capturedPiece = 'p';
    m.replyMotif = Motif::Trapped;
}

void mB1(MoveFeatures& m, GameContext&)
{
    m.threatBefore = 20.0;
    m.threatAfter = 19.0;
    m.threatAddressed = false;
    m.bestAddressesThreat = true;
}
void mB2(MoveFeatures& m, GameContext&)
{
    m.mateAgainstAfter = true;
    m.mateInAfter = -4;
    m.after = Score::mate(-4);
    m.wcAfter = -1.0;
    m.dW = 55.0;
}
void mB2n(MoveFeatures& m, GameContext& game)
{
    mB2(m, game);
    m.mateInAfter = -1;
    m.after = Score::mate(-1);
}
void mB3(MoveFeatures& m, GameContext& game)
{
    mB2(m, game);
    m.mateSquareOnOwnBackRank = true;
    m.kingHasLuft = false;
}
void mB4(MoveFeatures& m, GameContext&)
{
    m.threatBefore = 20.0;
    m.attackersNearOwnKing = 3;
    m.playedDefendsKingZone = false;
}
void mB5(MoveFeatures& m, GameContext&)
{
    m.phase = Phase::Endgame;
    m.enemyPassedPawnUncatchable = true;
}

void mC1(MoveFeatures& m, GameContext&)
{
    m.mateBefore = true;
    m.mateInBefore = 2;
    m.before = Score::mate(2);
    m.wcBefore = 1.0;
    m.dW = 50.0;
}
void mC2(MoveFeatures& m, GameContext&)
{
    m.freeCaptureMissed = true;
    m.freeCaptureMove = "d1d8";
    m.freeCaptureSee = 3;
}
void mC3(MoveFeatures& m, GameContext&)
{
    m.bestIsCapture = true;
    m.bestMotif = Motif::Fork;
}
void mC4(MoveFeatures& m, GameContext&) { m.bestIsQuiet = true; }
void mC5(MoveFeatures& m, GameContext&) { m.bestIsOnlyMove = true; }
void mC6(MoveFeatures& m, GameContext&)
{
    m.phase = Phase::Endgame;
    m.tbBefore = Tb::Win;
    m.tbAfter = Tb::Draw;
    m.pieceCount = 6;
}

void mD1(MoveFeatures& m, GameContext& game)
{
    game.castlingRightUntilPly20 = true;
    game.kingStillOnStartSquare = true;
    m.attackersNearOwnKing = 2;
}
void mD2(MoveFeatures& m, GameContext&)
{
    m.playedIsKingShieldPawn = true;
    m.enemyPiecesOnThatWing = 3;
}
void mD3(MoveFeatures& m, GameContext& game)
{
    m.phase = Phase::Opening;
    m.ply = 18;
    m.movedPiece = 'Q';
    m.queenLeftThirdRank = true;
    game.queenAttackTempiNext4 = 2;
}

void mE1(MoveFeatures& m, GameContext&)
{
    m.playedIsCapture = true;
    m.seePlayed = -2;
    m.attackersOnTarget = 3;
    m.defendersOnTarget = 3;
}
void mE2(MoveFeatures& m, GameContext&) { m.bestIsBackward = true; }
void mE3(MoveFeatures& m, GameContext&) { m.bestIsLongSlide = true; }
void mE4(MoveFeatures& m, GameContext&) { m.bestIsEdge = true; }
void mE5(MoveFeatures& m, GameContext&) { m.playedIndexInBestPv = 3; }
void mE6(MoveFeatures& m, GameContext&) { m.equalUntilPly = 4; }

void mEndgame(MoveFeatures& m)
{
    m.phase = Phase::Endgame;
    m.fen = "8/8/4k3/8/8/3K4/4P3/8 w - - 0 1";
    m.played = "d3d4";
    m.best = "d3e4";
    m.opponentReply = "e6d6";
    m.pieceCount = 5;
}
void mF1(MoveFeatures& m, GameContext&)
{
    mEndgame(m);
    m.pawnEndgameOnly = true;
    m.lostOpposition = true;
}
void mF2(MoveFeatures& m, GameContext&)
{
    mEndgame(m);
    m.leftKeySquares = true;
}
void mF3(MoveFeatures& m, GameContext&)
{
    mEndgame(m);
    m.ownPassedPawnLost = true;
}
void mF4(MoveFeatures& m, GameContext&)
{
    mEndgame(m);
    m.kingPassive = true;
    m.bestActivatesKing = true;
}
void mF5(MoveFeatures& m, GameContext&)
{
    mEndgame(m);
    m.bestPutsRookBehindPasser = true;
    m.rookMispostedVsPassedPawn = true;
}
void mF6(MoveFeatures& m, GameContext&)
{
    mEndgame(m);
    m.lastPieceTraded = true;
    m.wcBefore = -0.10;
    m.wcAfter = -0.60;
}
void mF7(MoveFeatures& m, GameContext&)
{
    mEndgame(m);
    m.tbBefore = Tb::Draw;
    m.tbAfter = Tb::Loss;
}
void mF8(MoveFeatures& m, GameContext&)
{
    mEndgame(m);
    m.wrongBishopRookPawn = true;
    m.tbAfter = Tb::Draw;
}

void mG1(MoveFeatures& m, GameContext&)
{
    m.playedCreatesWeakPawn = true;
    m.bestCreatesWeakPawn = false;
    m.playedGainsCompensation = false;
}
void mG2(MoveFeatures& m, GameContext&) { m.playedIsPurposeless = true; }
void mG3(MoveFeatures& m, GameContext&) { m.tradedGoodForBad = true; }
void mG4(MoveFeatures& m, GameContext& game)
{
    game.weakSquareOccupiedByKnight = true;
    m.movedPiece = 'P';
}
void mG5(MoveFeatures& m, GameContext&)
{
    m.bestTakesOpenFile = true;
    m.ownRookOnOpenFile = false;
}

void mOpening(MoveFeatures& m)
{
    m.phase = Phase::Opening;
    m.ply = 16;
    m.fen = "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3";
    m.played = "f1b5";
    m.best = "f1c4";
    m.opponentReply = "g8f6";
}
void mH1(MoveFeatures& m, GameContext& game)
{
    mOpening(m);
    game.samePieceMovesFirst20 = 3;
    game.undevelopedMinors = 2;
}
void mH2(MoveFeatures& m, GameContext& game)
{
    mOpening(m);
    game.wingPawnMovesFirst16 = 2;
    game.undevelopedMinors = 2;
}
void mH3(MoveFeatures& m, GameContext& game)
{
    mOpening(m);
    m.ply = 20;
    game.developmentDeficitAtPly20 = 3;
}
void mH4(MoveFeatures& m, GameContext& game)
{
    mOpening(m);
    m.ply = 12;
    m.dW = 26.0;
    game.knownTrapPosition = true;
}

void mI1(MoveFeatures& m, GameContext& game)
{
    m.moveTimeMs = 4000;
    game.moveTimeMs = 4000;
    m.spreadTopThree = 20.0;
}
void mI2(MoveFeatures& m, GameContext& game)
{
    m.moveTimeMs = 90000;
    game.moveTimeMs = 90000;
    m.spreadTopThree = 2.0;
}
void mI3(MoveFeatures& m, GameContext& game)
{
    m.moveTimeMs = -1;
    game.moveTimeMs = -1;
    game.medianMoveTimeMs = -1.0;
    game.errorsClusterAtEnd = true;
}
const ClassCase kClassCases[] = {
    { ErrorClass::A1, mA1 },   { ErrorClass::A2, mA2 },   { ErrorClass::A3, mA3 },
    { ErrorClass::A3k, mA3k }, { ErrorClass::A4, mA4 },   { ErrorClass::A5, mA5 },
    { ErrorClass::A6, mA6 },   { ErrorClass::A7, mA7 },   { ErrorClass::A8, mA8 },
    { ErrorClass::A9, mA9 },   { ErrorClass::A10, mA10 },
    { ErrorClass::B1, mB1 },   { ErrorClass::B2, mB2 },   { ErrorClass::B2n, mB2n },
    { ErrorClass::B3, mB3 },   { ErrorClass::B4, mB4 },   { ErrorClass::B5, mB5 },
    { ErrorClass::C1, mC1 },   { ErrorClass::C2, mC2 },   { ErrorClass::C3, mC3 },
    { ErrorClass::C4, mC4 },   { ErrorClass::C5, mC5 },   { ErrorClass::C6, mC6 },
    { ErrorClass::D1, mD1 },   { ErrorClass::D2, mD2 },   { ErrorClass::D3, mD3 },
    { ErrorClass::E1, mE1 },   { ErrorClass::E2, mE2 },   { ErrorClass::E3, mE3 },
    { ErrorClass::E4, mE4 },   { ErrorClass::E5, mE5 },   { ErrorClass::E6, mE6 },
    { ErrorClass::F1, mF1 },   { ErrorClass::F2, mF2 },   { ErrorClass::F3, mF3 },
    { ErrorClass::F4, mF4 },   { ErrorClass::F5, mF5 },   { ErrorClass::F6, mF6 },
    { ErrorClass::F7, mF7 },   { ErrorClass::F8, mF8 },
    { ErrorClass::G1, mG1 },   { ErrorClass::G2, mG2 },   { ErrorClass::G3, mG3 },
    { ErrorClass::G4, mG4 },   { ErrorClass::G5, mG5 },
    { ErrorClass::H1, mH1 },   { ErrorClass::H2, mH2 },   { ErrorClass::H3, mH3 },
    { ErrorClass::H4, mH4 },
    { ErrorClass::I1, mI1 },   { ErrorClass::I2, mI2 },   { ErrorClass::I3, mI3 },
};

} // namespace

int main()
{
    // --- the tables ---------------------------------------------------------
    {
        std::set<std::string> keys;
        int cardMaking = 0;
        int clusterClasses = 0;
        for (ErrorClass cls : kAllClasses) {
            const std::string key = errorKey(cls);
            CHECK(!key.empty());
            CHECK(keys.insert(key).second);          // stable and unique
            CHECK(errorFromKey(key) == cls);         // round trip
            CHECK(std::string(errorName(cls)).size() > 3);
            CHECK(std::string(errorTemplate(cls)).size() > 30);
            if (makesCard(cls))
                ++cardMaking;
            if (isClusterClass(cls))
                ++clusterClasses;
        }
        CHECK(keys.size() == 50);
        // teacher.md §2.7: 50 rules, 44 of them card-making. The six that are
        // not: I2 and I3 are hints, E2–E4 and G2 are cluster classes that make
        // one blind-spot card instead of one per occurrence.
        CHECK(cardMaking == 44);
        CHECK(clusterClasses == 4);
        CHECK(!makesCard(ErrorClass::I2));
        CHECK(!makesCard(ErrorClass::I3));
        CHECK(!makesCard(ErrorClass::Z0));
        // Group G waits for 1300 (§2.6, group G); G2 is a cluster class and
        // reports as a pattern at any strength.
        CHECK(needsTheta1300(ErrorClass::G1));
        CHECK(needsTheta1300(ErrorClass::G5));
        CHECK(!needsTheta1300(ErrorClass::A1));
        // Every class that produces work has an exercise type: a class without
        // one would be useless to the app (§2.1 point 4). The two pure hints,
        // I2 and I3, are the only ones without.
        for (ErrorClass cls : kAllClasses) {
            const bool hasDrill = std::string(drillKey(cls)).size() > 0;
            CHECK(hasDrill == (makesCard(cls) || isClusterClass(cls)));
        }
    }

    // --- part 1: real positions --------------------------------------------
    std::printf("Stellungen:\n");
    for (const BoardCase& item : kBoardCases)
        runBoardCase(item);

    // --- part 2: every one of the 50 ---------------------------------------
    {
        // The baseline on its own has to be Z0: an error that no rule explains.
        // Its share is the quality measure of the taxonomy (§2.4, target < 15 %).
        MoveFeatures m;
        GameContext game;
        baseline(m, game);
        const std::vector<Finding> findings = classify(m, game.learnerElo);
        CHECK(findings.size() == 1);
        CHECK(contains(findings, ErrorClass::Z0));
        CHECK(!findings.front().makesCard);
    }

    CHECK(sizeof(kClassCases) / sizeof(kClassCases[0]) == 52);   // the 50 rules plus A3k and B2n
    std::set<std::string> triggered;
    for (const ClassCase& item : kClassCases) {
        MoveFeatures m;
        GameContext game;
        baseline(m, game);
        item.mutate(m, game);
        const std::vector<Finding> findings = classify(m, game.learnerElo);
        if (!contains(findings, item.cls)) {
            std::fprintf(stderr, "FAILED class %s not triggered, got %s\n", errorKey(item.cls),
                         listOf(findings).c_str());
            ++failures;
            continue;
        }
        triggered.insert(errorKey(item.cls));
        CHECK(findings.size() <= 2);   // §2.4: at most two classes per event
        for (const Finding& finding : findings) {
            CHECK(finding.dimension == dimensionOf(finding.cls));
            CHECK(finding.severity > 0);
            CHECK(!finding.sentence.empty());
            // Every placeholder has to be filled: a sentence with a brace in it
            // would reach the learner (§6.6 — never a number, never a fragment).
            CHECK(finding.sentence.find('{') == std::string::npos);
            CHECK(finding.makesCard == (makesCard(finding.cls) && !finding.belowThreshold));
        }
    }
    // All 50 rules of §2.6 fired at least once, the two sub-variants included.
    CHECK(triggered.size() == 52);
    for (ErrorClass cls : kAllClasses)
        CHECK(triggered.count(errorKey(cls)) == 1);

    // --- part 3: a clean move fires nothing --------------------------------
    {
        struct CleanCase {
            const char* fen;
            const char* played;
            const char* best;
            const char* reply;
            int cp;
        };
        const CleanCase kClean[] = {
            // A sound developing move in the Spanish.
            { "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3",
              "f1b5", "f1b5", "g8f6", 30 },
            // Castling.
            { "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/2N2N2/PPPP1PPP/R1BQK2R w KQkq - 6 5",
              "e1g1", "e1g1", "e8g8", 20 },
            // A perfectly ordinary recapture.
            { "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 4 4",
              "f6e4", "f8c5", "d1e2", -10 },
            // King and pawn, the only move that keeps the opposition.
            { "8/8/4k3/8/8/4K3/4P3/8 w - - 0 1", "e3d3", "e3d3", "e6d6", 120 },
        };
        for (const CleanCase& item : kClean) {
            Position before(item.fen);
            CHECK(before.isLegal(item.played));
            GameContext game;
            game.learnerElo = 1500;
            game.learnerIsWhite = before.whiteToMove();
            game.ply = 8;
            EngineView engine;
            engine.before = Score::centipawns(item.cp);
            engine.after = Score::centipawns(item.cp - 3);   // a fraction of a pp
            engine.nullBefore = engine.before;
            engine.nullAfter = engine.after;
            engine.best = item.best;
            engine.opponentReply = item.reply;
            const MoveFeatures features = buildFeatures(before, item.played, engine, game);
            const std::vector<Finding> findings = classify(features, game.learnerElo);
            if (!findings.empty()) {
                std::fprintf(stderr, "FAILED clean move %s in %s fired %s\n", item.played, item.fen,
                             listOf(findings).c_str());
                ++failures;
            }
        }
        std::printf("  saubere Zuege: %d, keiner ausgeloest\n",
                    static_cast<int>(sizeof(kClean) / sizeof(kClean[0])));
    }

    // --- suppression, §2.3.3 ------------------------------------------------
    {
        MoveFeatures m;
        GameContext game;

        baseline(m, game);
        m.wcBefore = 0.95;
        m.wcAfter = 0.92;
        mA1(m, game);
        CHECK(suppressionOf(m) == Suppression::U1_StillWinning);
        CHECK(classify(m, 1500).empty());

        baseline(m, game);
        m.wcBefore = -0.85;
        mA1(m, game);
        CHECK(suppressionOf(m) == Suppression::U2_AlreadyLost);
        CHECK(classify(m, 1500).empty());

        baseline(m, game);
        m.wcBefore = -0.70;
        m.wcAfter = -0.95;
        mA1(m, game);
        CHECK(suppressionOf(m) == Suppression::U3_CollapseUnderway);

        baseline(m, game);
        m.ply = 8;
        m.inOpeningBook = true;
        mA1(m, game);
        CHECK(suppressionOf(m) == Suppression::U4_OpeningBook);

        baseline(m, game);
        m.moveTimeMs = 900;
        game.remainingFraction = 0.05;
        mA1(m, game);
        CHECK(suppressionOf(m) == Suppression::U5_TimeTrouble);

        baseline(m, game);
        game.onlyLegalMove = true;
        mA1(m, game);
        CHECK(suppressionOf(m) == Suppression::U6_Forced);

        // Step 0: the counter-check comes before everything else.
        baseline(m, game);
        m.refutationConfirmed = false;
        mA1(m, game);
        CHECK(suppressionOf(m) == Suppression::Step0_NotConfirmed);
        CHECK(classify(m, 1500).empty());
    }

    // --- the learning threshold, §2.3.2 ------------------------------------
    {
        MoveFeatures m;
        GameContext game;
        baseline(m, game);
        mA1(m, game);
        m.dW = 22.0;
        // A 22 pp event is homework for a 1500 player (L = 20) …
        CHECK(contains(classify(m, 1500), ErrorClass::A1));
        // … and deliberately not for a 900 player (L = 28), whose 22 pp events
        // are so frequent that a card per event would be noise.
        CHECK(classify(m, 900).empty());

        // A2 and E1 are the exception: they are logged below L to feed the REC
        // dimension, but they make no card (§2.6 A2).
        baseline(m, game);
        mA2(m, game);
        m.dW = 4.0;
        const std::vector<Finding> below = classify(m, 1500);
        CHECK(below.size() == 1);
        CHECK(below.front().cls == ErrorClass::A2);
        CHECK(below.front().belowThreshold);
        CHECK(!below.front().makesCard);
    }

    // --- U7: at most five events, the biggest win, ties to the earlier one --
    {
        std::vector<double> deltas;
        deltas.push_back(30.0);   // 0
        deltas.push_back(12.0);   // 1
        deltas.push_back(45.0);   // 2
        deltas.push_back(30.0);   // 3, ties with 0 and must lose to it
        deltas.push_back(8.0);    // 4
        deltas.push_back(22.0);   // 5
        const std::vector<std::size_t> kept = selectEvents(deltas, 3);
        CHECK(kept.size() == 3);
        CHECK(kept[0] == 0);
        CHECK(kept[1] == 2);
        CHECK(kept[2] == 3);
        CHECK(selectEvents(deltas, 99).size() == deltas.size());
    }

    // --- the dimensions, §3 -------------------------------------------------
    {
        // The operationalisation table of §3.3 has to be what the code says.
        CHECK(dimensionOf(ErrorClass::A1) == Dimension::SRG);
        CHECK(dimensionOf(ErrorClass::A2) == Dimension::REC);
        CHECK(dimensionOf(ErrorClass::A3) == Dimension::TAK);
        CHECK(dimensionOf(ErrorClass::B4) == Dimension::STL);
        CHECK(dimensionOf(ErrorClass::C2) == Dimension::SRG);
        CHECK(dimensionOf(ErrorClass::C4) == Dimension::REC);
        CHECK(dimensionOf(ErrorClass::F6) == Dimension::END);
        CHECK(dimensionOf(ErrorClass::H4) == Dimension::ERD);
        CHECK(dimensionOf(ErrorClass::I1) == Dimension::SRG);

        SkillState state;
        state.ownMoves = 200;
        Finding blunder;
        blunder.cls = ErrorClass::A1;
        blunder.dimension = Dimension::SRG;
        blunder.dW = 30.0;
        addEvent(state, blunder);
        addEvent(state, blunder);
        Finding small;
        small.cls = ErrorClass::C4;
        small.dimension = Dimension::REC;
        small.dW = 12.0;
        addEvent(state, small);
        const std::array<double, kDimensionCount> a = shares(state);
        CHECK(a[static_cast<std::size_t>(Dimension::SRG)] > a[static_cast<std::size_t>(Dimension::REC)]);
        CHECK(a[static_cast<std::size_t>(Dimension::TAK)] == 0.0);
        CHECK(byShare(state).front() == Dimension::SRG);
        CHECK(blunderRate(state) == 1.0);   // two blunders in 200 own moves

        // §3.4: a single occurrence is nothing; below 30 events nothing is
        // reported at all, and the 1 % level is the guard against the multiple
        // testing that six cluster classes would otherwise commit.
        CHECK(!clusterTest(5, 10, 0.15).significant);
        CHECK(!clusterTest(6, 30, 0.15).significant);
        CHECK(clusterTest(18, 30, 0.15).significant);
        std::vector<ClusterCandidate> candidates;
        ClusterCandidate weak;
        weak.cls = ErrorClass::E2;
        weak.result = clusterTest(14, 30, 0.15);
        ClusterCandidate strong;
        strong.cls = ErrorClass::E3;
        strong.result = clusterTest(22, 30, 0.15);
        candidates.push_back(weak);
        candidates.push_back(strong);
        CHECK(strongestCluster(candidates).cls == ErrorClass::E3);

        // §2.5 [RISIKO]: a motif that marks almost nothing or almost everything
        // is broken, like Lichess' own `overloading` predicate.
        CHECK(motifSuspect(0, 1000));
        CHECK(motifSuspect(400, 1000));
        CHECK(!motifSuspect(120, 1000));
    }

    std::printf("OK: %d Fehlerklassen ausgeloest, Tabellen und Unterdrueckung geprueft\n",
                static_cast<int>(triggered.size()));
    if (failures) {
        std::printf("%d failures\n", failures);
        return 1;
    }
    return 0;
}
