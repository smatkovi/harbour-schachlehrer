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
#ifndef SCHACH_CORE_TAXONOMY_H
#define SCHACH_CORE_TAXONOMY_H

// teacher.md §2 — the heart of the app: 50 detection rules over one feature
// vector, a fixed check order, and one German sentence per class.
//
// Two design rules from §2.1 are visible in the code: every rule is decidable
// from engine output plus board features without human judgement, and the
// thresholds live in tables (§2.3) instead of in scattered if-chains, so they
// can be read off against the specification.

#include "Position.h"
#include "WinProb.h"

#include <cstdint>
#include <string>
#include <vector>

namespace schach {
namespace core {

// teacher.md §3.1 — six skills, and TAK/SRG are deliberately separate:
// TAK is recognising a pattern, SRG is looking whether there is one at all.
enum class Dimension : std::uint8_t { TAK = 0, SRG, REC, END, STL, ERD, Count };

// teacher.md §2.5. The refutation's motif; the German names are the ones the
// app shows, the enum keeps the code English.
enum class Motif : std::uint8_t {
    None = 0,
    Fork,             // GABEL
    Pin,              // FESSELUNG
    Skewer,           // SPIESS
    Discovery,        // ABZUG
    DiscoveredCheck,  // ABZUGSSCHACH
    DoubleCheck,      // DOPPELSCHACH
    Hanging,          // HÄNGER
    Deflection,       // ABLENKUNG
    Attraction,       // HINLENKUNG
    Overloading,      // ÜBERLASTUNG
    InBetween,        // ZWISCHENZUG
    XRay,             // RÖNTGEN
    Trapped,          // EINSPERRUNG
    MateNet           // MATTNETZ
};

const char* motifKey(Motif motif);
Motif motifFromKey(const std::string& key);
const char* motifName(Motif motif);   // German

// Tablebase verdict from the side to move's point of view (teacher.md §8.2).
enum class Tb : std::uint8_t { Unknown = 0, Win, Draw, Loss };

// teacher.md §2.4 groups, in check order.
enum class Group : std::uint8_t { A = 0, B, C, D, E, F, G, H, I, Z };

// The 50 detection rules of teacher.md §2.6, plus the two sub-variants that
// are counted separately (A3k, B2n) and the Z0 bucket whose share is the
// quality measure of the whole taxonomy (target < 15 %).
enum class ErrorClass : std::uint8_t {
    None = 0,
    A1, A2, A3, A3k, A4, A5, A6, A7, A8, A9, A10,
    B1, B2, B2n, B3, B4, B5,
    C1, C2, C3, C4, C5, C6,
    D1, D2, D3,
    E1, E2, E3, E4, E5, E6,
    F1, F2, F3, F4, F5, F6, F7, F8,
    G1, G2, G3, G4, G5,
    H1, H2, H3, H4,
    I1, I2, I3,
    Z0,
    Count
};

// The 50 rules in check order; A3k and B2n are attributes of A3/B2 and Z0 is
// the fallback, so none of the three appears here.
extern const ErrorClass kAllClasses[50];
constexpr int kClassCount = 50;

Group groupOf(ErrorClass cls);
Dimension dimensionOf(ErrorClass cls);
// Stable machine key, e.g. "A1" — this is what goes into the database, never
// the enum value, so the numbering may change (platform.md §5.1 `finding.code`).
const char* errorKey(ErrorClass cls);
ErrorClass errorFromKey(const std::string& key);
const char* errorName(ErrorClass cls);          // German short name
// German sentence template, teacher.md §6.6: refutation, mechanism, transfer.
// Placeholders: {gespielt} {bester} {widerlegung} {figur} {figurAkk} {feld} {motiv}
const char* errorTemplate(ErrorClass cls);
// 44 of the 50 make a card: I2 and I3 are hints only, and the four cluster
// classes E2–E4 and G2 make one blind-spot card instead of one per event.
bool makesCard(ErrorClass cls);
bool isClusterClass(ErrorClass cls);
// Group G never makes a card below theta 1300 (teacher.md §2.6, group G).
bool needsTheta1300(ErrorClass cls);
// The exercise type of teacher.md §6.1, as a stable key ("D-SEHEN", …).
const char* drillKey(ErrorClass cls);

// --- the feature vector, teacher.md §2.2 ------------------------------------

// What the engine supplied for one half move. All scores are from the point
// of view of the *mover* (the learner), which means the Analyser has to negate
// what the engine says about the position after the move.
struct EngineView {
    Score before;        // eval(P_vor)
    Score after;         // eval(P_nach)
    Score nullBefore;    // nullEval(P_vor): what does he threaten already?
    Score nullAfter;     // nullEval(P_nach): is the threat still there?
    Score second;        // multipv 2 in P_vor
    Score third;         // multipv 3 in P_vor, for the criticality span (I1/I2)

    std::string best;                 // engine best move in P_vor
    std::vector<std::string> bestPv;  // its principal variation, UCI
    std::string opponentReply;        // engine best move after `played` = the refutation

    // teacher.md §2.4 step 0: is the refutation really the engine's best move,
    // and does the advantage survive four more half moves? False discards the
    // event and catches SEE artefacts and horizon effects.
    bool refutationConfirmed = true;

    // C5: `best` is the only move that keeps the loss below 8 pp.
    bool bestIsOnlyMove = false;
    // E5: where the played move appears in the main line of `best` (-1 = not).
    int playedIndexInBestPv = -1;
    // E6: up to which half move the played move is equivalent (k >= 3).
    int equalUntilPly = -1;

    Tb tbBefore = Tb::Unknown;
    Tb tbAfter = Tb::Unknown;
    bool inOpeningBook = false;
};

// Game-level facts a single half move cannot know. The Analyser fills these
// once per game; the detectors only read them.
struct GameContext {
    bool learnerIsWhite = true;
    int learnerElo = 1000;
    int ply = 0;                    // half move index, 0-based
    int moveTimeMs = -1;            // from %clk, -1 when unknown
    double medianMoveTimeMs = -1.0;
    double remainingFraction = -1.0;  // remaining clock / base time, -1 unknown
    bool onlyLegalMove = false;       // U6
    bool repetition = false;          // U6

    // group D
    bool castlingRightUntilPly20 = false;
    bool kingStillOnStartSquare = false;
    bool castlingAlreadyReported = false;
    int queenAttackTempiNext4 = 0;    // D3
    // group G
    bool weakSquareOccupiedByKnight = false;  // G4, needs six half moves of hindsight
    // group H
    int samePieceMovesFirst20 = 0;    // H1
    int wingPawnMovesFirst16 = 0;     // H2
    int undevelopedMinors = 0;        // H1, H2
    int developmentDeficitAtPly20 = 0;  // H3
    bool knownTrapPosition = false;     // H4
    bool openingAlreadyReported = false;
    // group I
    bool errorsClusterAtEnd = false;    // I3, computed over the whole game
};

// teacher.md §2.2. Everything in here is derivable from two engine calls plus
// a SEE routine; nothing needs a human.
struct MoveFeatures {
    // --- engine ------------------------------------------------------------
    double dW = 0.0;          // W(cp_vor) - W(cp_nach), percentage points
    double wcBefore = 0.0, wcAfter = 0.0;
    Score before, after;
    bool mateBefore = false, mateAfter = false;   // a mate exists for the mover
    bool mateAgainstBefore = false, mateAgainstAfter = false;
    int mateInBefore = 0, mateInAfter = 0;

    std::string fen;             // P_vor, the position the sentence talks about
    std::string played, best, opponentReply;
    std::vector<std::string> bestPv;

    bool bestIsCapture = false, bestIsCheck = false, bestIsQuiet = false;
    bool playedIsCapture = false, playedIsCheck = false, playedIsPromotion = false;
    int seePlayed = 0;           // pawn units
    int capturedValue = 0;       // value of the piece `played` took, 0 if none
    char movedPiece = ' ';       // FEN letter of the piece that moved
    char capturedPiece = ' ';

    std::vector<Hanging> hangingBefore, hangingAfter, newlyHanging;

    double threatBefore = 0.0;   // drohungVor in pp
    double threatAfter = 0.0;
    bool threatAddressed = false;     // threatAfter < 0.4 * threatBefore
    bool bestAddressesThreat = false;

    Motif replyMotif = Motif::None;
    Motif bestMotif = Motif::None;    // C3: the motif `best` would have created
    bool replyIsCheck = false;        // A3k: the fork comes with check

    Phase phase = Phase::Middlegame;
    int material = 0;            // pawn difference from the mover's view
    int officers = 0;
    int pieceCount = 0;
    int ply = 0;
    int moveTimeMs = -1;

    Tb tbBefore = Tb::Unknown, tbAfter = Tb::Unknown;

    // --- derived board features the individual rules need -------------------
    int attackersOnTarget = 0, defendersOnTarget = 0;   // E1
    bool bestIsBackward = false;      // E2
    bool bestIsLongSlide = false;     // E3, >= 4 squares
    bool bestIsEdge = false;          // E4, a/h file or rank 1/8
    int playedIndexInBestPv = -1;     // E5
    int equalUntilPly = -1;           // E6
    bool bestIsOnlyMove = false;      // C5
    bool freeCaptureMissed = false;   // C2
    std::string freeCaptureMove;
    int freeCaptureSee = 0;
    bool mateSquareOnOwnBackRank = false;   // B3
    bool kingHasLuft = true;                // B3
    int attackersNearOwnKing = 0;           // B4
    bool playedDefendsKingZone = false;     // B4
    bool playedIsKingShieldPawn = false;    // D2
    int enemyPiecesOnThatWing = 0;          // D2
    bool playedCreatesWeakPawn = false;     // G1, doubled or isolated
    bool bestCreatesWeakPawn = false;
    bool playedGainsCompensation = false;   // G1
    bool playedIsPurposeless = false;       // G2
    bool tradedGoodForBad = false;          // G3
    bool bestTakesOpenFile = false;         // G5
    bool ownRookOnOpenFile = false;         // G5
    bool enemyPassedPawnUncatchable = false;  // B5
    bool ownPassedPawnLost = false;           // F3
    bool lostOpposition = false;              // F1
    bool leftKeySquares = false;              // F2
    bool pawnEndgameOnly = false;             // F1, F2
    bool kingPassive = false;                 // F4
    bool bestActivatesKing = false;           // F4
    bool rookMispostedVsPassedPawn = false;   // F5
    bool bestPutsRookBehindPasser = false;    // F5
    bool lastPieceTraded = false;             // F6
    bool wrongBishopRookPawn = false;         // F8
    bool queenLeftThirdRank = false;          // D3
    double spreadTopThree = 0.0;              // I1, I2 in pp
    bool refutationConfirmed = true;          // §2.4 step 0
    bool inOpeningBook = false;               // U4

    const GameContext* game = nullptr;   // never owned
};

// teacher.md §2.3.3 — suppression rules, applied before any class rule.
enum class Suppression : std::uint8_t {
    None = 0,
    U1_StillWinning,
    U2_AlreadyLost,
    U3_CollapseUnderway,
    U4_OpeningBook,
    U5_TimeTrouble,
    U6_Forced,
    Step0_NotConfirmed
};

Suppression suppressionOf(const MoveFeatures& features);
const char* suppressionKey(Suppression suppression);

// One diagnosed error (docs/design.md §3).
struct Finding {
    ErrorClass cls = ErrorClass::None;
    Dimension dimension = Dimension::SRG;
    int severity = 0;             // rounded dW in percentage points
    Severity level = Severity::Clean;
    double dW = 0.0;
    Motif motif = Motif::None;
    std::string fen, playedMove, bestMove, refutation;
    std::string sentence;         // German, teacher.md §6.6
    bool makesCard = false;       // 44 of 50 classes do
    bool belowThreshold = false;  // logged for a_d, but no card (A2, E1)
    int ply = 0;
};

// Build the feature vector for one half move. `before` must be the position
// with the learner to move; `playedUci` must be legal in it.
MoveFeatures buildFeatures(const Position& before, const std::string& playedUci,
                           const EngineView& engine, const GameContext& game);

// teacher.md §2.5: the motif of the opponent's refutation.
Motif detectMotif(const Position& afterPlayed, const std::string& replyUci,
                  const std::vector<Hanging>& newlyHanging, bool mateForReplier);

// teacher.md §2.4: the fixed check order, at most two classes, Z0 when nothing
// fires. An empty result means the event was suppressed.
std::vector<Finding> classify(const MoveFeatures& features, int learnerElo);

// Fill {gespielt} … in the template of `cls`.
std::string sentenceFor(ErrorClass cls, const MoveFeatures& features);

// teacher.md §2.3.3 U7: of the events that survived, the biggest dW win, ties
// going to the earlier one, because the earlier one is the cause. Returns the
// indices to keep, in board order.
std::vector<std::size_t> selectEvents(const std::vector<double>& deltaW, std::size_t maxEvents);

} // namespace core
} // namespace schach

#endif // SCHACH_CORE_TAXONOMY_H
