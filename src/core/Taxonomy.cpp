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
#include "Taxonomy.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

namespace schach {
namespace core {

// ---------------------------------------------------------------------------
// The tables. teacher.md §2.3 asks for tables rather than scattered ifs, and
// the same is true of the texts: one row per class, readable against §2.6.
// ---------------------------------------------------------------------------

namespace {

struct ClassRow {
    ErrorClass cls;
    const char* key;
    Group group;
    Dimension dimension;
    bool card;
    bool cluster;      // reported as a pattern over >= 30 events (§3.4)
    bool theta1300;    // group G only becomes active at 1300 (§2.6)
    const char* drill;
    const char* name;
    const char* text;
};

const ClassRow kRows[] = {
    { ErrorClass::None, "", Group::Z, Dimension::SRG, false, false, false, "", "", "" },

    { ErrorClass::A1, "A1", Group::A, Dimension::SRG, true, false, false, "D-SEHEN",
      "Figur eingestellt",
      "{gespielt} lässt {figurAkk} auf {feld} stehen: Nach {widerlegung} ist die Figur weg und du "
      "bekommst nichts dafür. Nach jedem eigenen Zug: Was steht jetzt ungedeckt?" },
    { ErrorClass::A2, "A2", Group::A, Dimension::REC, true, false, false, "D-ABTAUSCH",
      "Schlechter Abtausch",
      "{gespielt} geht nicht auf: Auf {feld} hat er einen Verteidiger mehr, als du Angreifer hast. Zähl vor "
      "dem Abtausch beide Seiten zu Ende." },
    { ErrorClass::A3, "A3", Group::A, Dimension::TAK, true, false, false, "D-TAKTIK",
      "Gabel zugelassen",
      "{gespielt} übersieht {widerlegung}: Der Zug gabelt zwei deiner Figuren, eine muss weg und die andere "
      "fällt. Achte auf Felder, von denen aus eine Figur zwei von deinen erreicht." },
    { ErrorClass::A3k, "A3k", Group::A, Dimension::TAK, true, false, false, "D-TAKTIK",
      "Königsgabel zugelassen",
      "{gespielt} übersieht {widerlegung}: Die Gabel trifft deinen König mit. Du musst aus dem Schach, "
      "danach fällt die zweite Figur. Königsgabeln kosten immer." },
    { ErrorClass::A4, "A4", Group::A, Dimension::TAK, true, false, false, "D-TAKTIK",
      "In die Fesselung gezogen",
      "Nach {widerlegung} steht {figur} gefesselt: Sie kann nicht ziehen, ohne dahinter etwas "
      "Wertvolleres preiszugeben." },
    { ErrorClass::A5, "A5", Group::A, Dimension::TAK, true, false, false, "D-TAKTIK",
      "Spieß zugelassen",
      "{widerlegung} spießt zwei deiner Figuren auf einer Linie auf. Die vordere muss weichen, dahinter "
      "fällt die nächste." },
    { ErrorClass::A6, "A6", Group::A, Dimension::TAK, true, false, false, "D-TAKTIK",
      "Abzugsangriff zugelassen",
      "{widerlegung} ist ein Abzug: Der ziehende Stein öffnet die Linie dahinter. Während du dem Schach "
      "begegnest, schlägt er." },
    { ErrorClass::A7, "A7", Group::A, Dimension::TAK, true, false, false, "D-TAKTIK",
      "Figur eingesperrt",
      "Nach {gespielt} hat {figur} kein Feld mehr: Nach {widerlegung} ist sie gefangen und geht in "
      "wenigen Zügen verloren." },
    { ErrorClass::A8, "A8", Group::A, Dimension::TAK, true, false, false, "D-TAKTIK",
      "Verteidiger überlastet",
      "{widerlegung} lenkt deinen Verteidiger ab: Eine Figur muss zwei Dinge zugleich decken und schafft "
      "das nicht." },
    { ErrorClass::A9, "A9", Group::A, Dimension::REC, true, false, false, "D-ABTAUSCH",
      "Zwischenzug übersehen",
      "{gespielt} rechnet mit dem Rückschlag. Er schlägt aber nicht zurück, sondern zieht erst "
      "{widerlegung} — und nimmt danach. Frag bei jeder Abtauschfolge: Hat er ein Schach dazwischen?" },
    { ErrorClass::A10, "A10", Group::A, Dimension::TAK, true, false, false, "D-TAKTIK",
      "Vergifteten Bauern genommen",
      "Der Bauer auf {feld} war eine Falle. Nach {widerlegung} hat deine schlagende Figur kein gutes Feld "
      "mehr. Ein Bauer tief in seiner Stellung ist selten geschenkt." },

    { ErrorClass::B1, "B1", Group::B, Dimension::SRG, true, false, false, "D-DROHUNG",
      "Drohung ignoriert",
      "{gespielt} kümmert sich nicht um {widerlegung}, das schon vorher drohte. Erst hinschauen, was er "
      "will, dann den eigenen Plan." },
    { ErrorClass::B2, "B2", Group::B, Dimension::SRG, true, false, false, "D-DROHUNG",
      "Matt zugelassen",
      "Nach {gespielt} hat er ein erzwungenes Matt, vorher war es noch zu verhindern. Es beginnt mit "
      "{widerlegung}. Vor jedem Zug: Hat er ein Matt?" },
    { ErrorClass::B2n, "B2n", Group::B, Dimension::SRG, true, false, false, "D-DROHUNG",
      "Matt in einem zugelassen",
      "Nach {gespielt} setzt {widerlegung} sofort matt. Prüf nach jedem eigenen Zug alle Schachgebote des "
      "Gegners — das ist der Fehler, der am leichtesten zu vermeiden ist." },
    { ErrorClass::B3, "B3", Group::B, Dimension::SRG, true, false, false, "D-DROHUNG",
      "Grundreihenschwäche",
      "{gespielt} erlaubt {widerlegung}: Dein König sitzt hinter unbewegten Bauern auf der Grundreihe. Ein "
      "Luftloch kostet einen Zug und verhindert das für den Rest der Partie." },
    { ErrorClass::B4, "B4", Group::B, Dimension::STL, true, false, false, "D-DROHUNG",
      "Königsangriff nicht ernst genommen",
      "Er hat mehrere Figuren auf deinen König gerichtet, {gespielt} spielt woanders weiter. Nach "
      "{widerlegung} bricht die Stellung auf. Wo er Figuren sammelt, musst du antworten." },
    { ErrorClass::B5, "B5", Group::B, Dimension::END, true, false, false, "D-ENDSPIEL",
      "Freibauer nicht gestoppt",
      "Sein Freibauer ist nach {gespielt} im Quadrat nicht mehr einzuholen. {bester} hält ihn auf. Bei "
      "jedem Freibauern zuerst das Quadrat prüfen." },

    { ErrorClass::C1, "C1", Group::C, Dimension::TAK, true, false, false, "D-TAKTIK",
      "Matt verpasst",
      "{bester} setzt matt — du hattest es, {gespielt} lässt es aus. Wenn sein König wenige Felder hat, "
      "such zuerst nach Schach." },
    { ErrorClass::C2, "C2", Group::C, Dimension::SRG, true, false, false, "D-SEHEN",
      "Freie Figur nicht genommen",
      "Auf {feld} stand {figur} ungedeckt — {bester} gewinnt die Figur glatt. Vor dem eigenen Plan: "
      "Steht bei ihm etwas frei?" },
    { ErrorClass::C3, "C3", Group::C, Dimension::TAK, true, false, false, "D-TAKTIK",
      "Taktik verpasst",
      "{bester} erzeugt {motiv} und gewinnt Material; {gespielt} lässt die Gelegenheit vorbei. Prüf in "
      "jeder Stellung zuerst Schach und Schlag." },
    { ErrorClass::C4, "C4", Group::C, Dimension::REC, true, false, false, "D-STILL",
      "Stiller Gewinnzug verpasst",
      "{bester} gibt kein Schach und schlägt nichts, und genau deshalb ist es stark: Danach ist die Drohung "
      "nicht mehr zu halten. Nicht jeder Gewinnzug ist laut." },
    { ErrorClass::C5, "C5", Group::C, Dimension::REC, true, false, false, "D-STILL",
      "Einzige Verteidigung verpasst",
      "Es gab nur einen Zug, der hält: {bester}. Alles andere verliert. Wenn es eng wird, such nach dem "
      "einen Zug, nicht nach dem schönsten." },
    { ErrorClass::C6, "C6", Group::C, Dimension::END, true, false, false, "D-ENDSPIEL",
      "Gewinnstellung nicht verwertet",
      "Du standest auf Gewinn, nach {gespielt} ist es nur noch remis. {bester} hält den Gewinn fest. Im "
      "Endspiel entscheidet die Königsstellung, nicht das Tempo." },

    { ErrorClass::D1, "D1", Group::D, Dimension::ERD, true, false, false, "D-EROEFF",
      "Rochade versäumt",
      "Dein König steht immer noch in der Mitte, und die Linien öffnen sich. Rochiere früh — es ist der "
      "billigste gute Zug, den du hast." },
    { ErrorClass::D2, "D2", Group::D, Dimension::STL, true, false, false, "D-STRUKTUR",
      "Königsstellung geöffnet",
      "{gespielt} reißt die Deckung deines Königs auf. Die Bauern vor dem rochierten König bleiben stehen, "
      "solange der Gegner dort Figuren hat." },
    { ErrorClass::D3, "D3", Group::D, Dimension::ERD, true, false, false, "D-EROEFF",
      "Dame zu früh herausgeholt",
      "{gespielt} holt die Dame zu früh heraus: Sie wird jetzt mit Tempogewinn vertrieben, und er "
      "entwickelt sich dabei. Die Dame kommt nach den Leichtfiguren." },

    { ErrorClass::E1, "E1", Group::E, Dimension::REC, true, false, false, "D-ABTAUSCH",
      "Abtauschfolge falsch gerechnet",
      "Auf {feld} stehen genauso viele Angreifer wie Verteidiger — aber die Reihenfolge geht nicht auf, "
      "{gespielt} verliert Material. Zähl die Reihenfolge, nicht nur die Zahl." },
    { ErrorClass::E2, "E2", Group::E, Dimension::REC, false, true, false, "D-KANDIDAT",
      "Rückwärtszüge übersehen",
      "{bester} zieht rückwärts. Rückwärtszüge werden am häufigsten übersehen — nimm sie ausdrücklich in "
      "deine Kandidatenliste auf." },
    { ErrorClass::E3, "E3", Group::E, Dimension::REC, false, true, false, "D-KANDIDAT",
      "Weite Linienzüge übersehen",
      "{bester} zieht weit über die Linie. Lange Züge von Läufer, Turm und Dame fallen am ehesten aus dem "
      "Blick." },
    { ErrorClass::E4, "E4", Group::E, Dimension::REC, false, true, false, "D-KANDIDAT",
      "Randzüge übersehen",
      "{bester} führt an den Rand. Randfelder werden systematisch übersehen — schau sie bewusst an." },
    { ErrorClass::E5, "E5", Group::E, Dimension::REC, true, false, false, "D-KANDIDAT",
      "Richtige Idee, falsche Zugfolge",
      "Die Idee stimmt, nur die Reihenfolge nicht: erst {bester}, dann dein Zug. Andersherum hat er etwas "
      "dazwischen." },
    { ErrorClass::E6, "E6", Group::E, Dimension::REC, true, false, false, "D-VISU",
      "Zu kurz gerechnet",
      "Deine Rechnung stimmt bis zum vorletzten Halbzug; der Zug danach kippt sie. Rechne eine Station "
      "weiter, als es sich ruhig anfühlt." },

    { ErrorClass::F1, "F1", Group::F, Dimension::END, true, false, false, "D-ENDSPIEL",
      "Opposition verloren",
      "{gespielt} gibt die Opposition auf. Mit {bester} hältst du ihn vom Vorrücken ab." },
    { ErrorClass::F2, "F2", Group::F, Dimension::END, true, false, false, "D-ENDSPIEL",
      "Schlüsselfeld verfehlt",
      "Der König muss vor den Bauern, auf eines der Schlüsselfelder. Nach {gespielt} kommt er nicht mehr "
      "durch; {bester} ist der Weg." },
    { ErrorClass::F3, "F3", Group::F, Dimension::END, true, false, false, "D-ENDSPIEL",
      "Quadratregel falsch angewandt",
      "Dein Freibauer läuft nach {gespielt} nicht mehr durch — sein König erreicht das Quadrat. {bester} "
      "hält ihn am Laufen." },
    { ErrorClass::F4, "F4", Group::F, Dimension::END, true, false, false, "D-ENDSPIEL",
      "König bleibt passiv",
      "Im Endspiel ist der König eine Angriffsfigur. {bester} statt {gespielt} — er hat mehrere Züge "
      "Vorsprung." },
    { ErrorClass::F5, "F5", Group::F, Dimension::END, true, false, false, "D-ENDSPIEL",
      "Turm falsch postiert",
      "Türme gehören hinter den Freibauern — den eigenen wie den gegnerischen. {bester} statt {gespielt}." },
    { ErrorClass::F6, "F6", Group::F, Dimension::END, true, false, false, "D-ENDSPIEL",
      "Ins verlorene Bauernendspiel getauscht",
      "Nach {gespielt} ist das Bauernendspiel verloren: Sein König steht aktiver. Vor dem letzten Abtausch "
      "immer das Bauernendspiel durchrechnen." },
    { ErrorClass::F7, "F7", Group::F, Dimension::END, true, false, false, "D-ENDSPIEL",
      "Remis verschenkt",
      "Die Stellung war remis zu halten; {gespielt} verliert sie. {bester} hält." },
    { ErrorClass::F8, "F8", Group::F, Dimension::END, true, false, false, "D-ENDSPIEL",
      "Randbauer-Remis nicht erkannt",
      "Turmbauer plus Läufer der falschen Farbe ist remis, auch mit mehr Material: Das Umwandlungsfeld hat "
      "nicht die Farbe deines Läufers." },

    { ErrorClass::G1, "G1", Group::G, Dimension::STL, true, false, true, "D-STRUKTUR",
      "Bauernstruktur verschlechtert",
      "{gespielt} verdirbt deine Bauernstruktur ohne Gegenwert. Struktur gibt man nur gegen etwas "
      "Greifbares auf." },
    { ErrorClass::G2, "G2", Group::G, Dimension::STL, false, true, false, "D-KANDIDAT",
      "Zug ohne Zweck",
      "{gespielt} tut nichts: kein Schach, kein Schlag, keine Drohung, keine Entwicklung. Jeder Zug soll "
      "eine Aufgabe haben." },
    { ErrorClass::G3, "G3", Group::G, Dimension::STL, true, false, true, "D-STRUKTUR",
      "Gute Figur weggetauscht",
      "{gespielt} tauscht deine aktive Figur gegen seine schlechte. Tausche, was bei ihm gut steht, nicht "
      "was bei dir gut steht." },
    { ErrorClass::G4, "G4", Group::G, Dimension::STL, true, false, true, "D-STRUKTUR",
      "Feld dauerhaft geschwächt",
      "{gespielt} schwächt ein Feld dauerhaft: Kein eigener Bauer kann es mehr kontrollieren, und sein "
      "Springer setzt sich dort fest." },
    { ErrorClass::G5, "G5", Group::G, Dimension::STL, true, false, true, "D-STRUKTUR",
      "Turm bleibt auf geschlossener Linie",
      "Die Linie ist offen und kein Turm von dir steht darauf. {bester} nimmt sie." },

    { ErrorClass::H1, "H1", Group::H, Dimension::ERD, true, false, false, "D-EROEFF",
      "Dieselbe Figur mehrfach gezogen",
      "Du ziehst dieselbe Figur immer wieder, während Leichtfiguren zu Hause stehen. In der Eröffnung jede "
      "Figur einmal, dann rochieren." },
    { ErrorClass::H2, "H2", Group::H, Dimension::ERD, true, false, false, "D-EROEFF",
      "Flügelbauern statt Entwicklung",
      "{gespielt} bringt keine Figur ins Spiel. Erst Zentrum und Leichtfiguren, dann die Flügel." },
    { ErrorClass::H3, "H3", Group::H, Dimension::ERD, true, false, false, "D-EROEFF",
      "Entwicklungsrückstand",
      "Nach zwanzig Halbzügen hat er deutlich mehr Figuren im Spiel als du. Ein Entwicklungsvorsprung wird "
      "irgendwann zu Material." },
    { ErrorClass::H4, "H4", Group::H, Dimension::ERD, true, false, false, "D-EROEFF",
      "In eine bekannte Falle getreten",
      "Das ist eine bekannte Falle: Nach {gespielt} folgt {widerlegung} und du verlierst Material. Merk dir "
      "die Stellung — sie kommt wieder." },

    { ErrorClass::I1, "I1", Group::I, Dimension::SRG, true, false, false, "D-DROHUNG",
      "Im kritischen Moment zu schnell",
      "Du hast für den wichtigsten Zug der Partie kaum Zeit gebraucht. Wenn die Stellung kippt, hilft nur "
      "Hinschauen." },
    { ErrorClass::I2, "I2", Group::I, Dimension::SRG, false, false, false, "",
      "Zeit in ruhiger Stellung verbraucht",
      "Hier hast du lange nachgedacht, obwohl mehrere Züge gleich gut waren. Zeit gehört in die kritischen "
      "Momente." },
    { ErrorClass::I3, "I3", Group::I, Dimension::SRG, false, false, false, "",
      "Fehlerhäufung am Partieende",
      "Deine Fehler häufen sich am Partieende. Eine kürzere Partie oder eine Pause davor bringt mehr als "
      "jede Übung." },

    { ErrorClass::Z0, "Z0", Group::Z, Dimension::SRG, false, false, false, "",
      "Unklassifiziert",
      "Hier ist etwas schiefgegangen, das ich nicht benennen kann. Ich merke es mir und schaue es mir "
      "noch einmal an." },
};

const ClassRow& rowOf(ErrorClass cls)
{
    for (const ClassRow& row : kRows) {
        if (row.cls == cls)
            return row;
    }
    return kRows[0];
}

} // namespace

// The check order of §2.4 within each group; the groups themselves are
// ordered by classify().
const ErrorClass kAllClasses[50] = {
    ErrorClass::A1, ErrorClass::A2, ErrorClass::A3, ErrorClass::A4, ErrorClass::A5,
    ErrorClass::A6, ErrorClass::A7, ErrorClass::A8, ErrorClass::A9, ErrorClass::A10,
    ErrorClass::B1, ErrorClass::B2, ErrorClass::B3, ErrorClass::B4, ErrorClass::B5,
    ErrorClass::C1, ErrorClass::C2, ErrorClass::C3, ErrorClass::C4, ErrorClass::C5, ErrorClass::C6,
    ErrorClass::D1, ErrorClass::D2, ErrorClass::D3,
    ErrorClass::E1, ErrorClass::E2, ErrorClass::E3, ErrorClass::E4, ErrorClass::E5, ErrorClass::E6,
    ErrorClass::F1, ErrorClass::F2, ErrorClass::F3, ErrorClass::F4,
    ErrorClass::F5, ErrorClass::F6, ErrorClass::F7, ErrorClass::F8,
    ErrorClass::G1, ErrorClass::G2, ErrorClass::G3, ErrorClass::G4, ErrorClass::G5,
    ErrorClass::H1, ErrorClass::H2, ErrorClass::H3, ErrorClass::H4,
    ErrorClass::I1, ErrorClass::I2, ErrorClass::I3
};

Group groupOf(ErrorClass cls) { return rowOf(cls).group; }
Dimension dimensionOf(ErrorClass cls) { return rowOf(cls).dimension; }
const char* errorKey(ErrorClass cls) { return rowOf(cls).key; }
const char* errorName(ErrorClass cls) { return rowOf(cls).name; }
const char* errorTemplate(ErrorClass cls) { return rowOf(cls).text; }
bool makesCard(ErrorClass cls) { return rowOf(cls).card; }
bool isClusterClass(ErrorClass cls) { return rowOf(cls).cluster; }
bool needsTheta1300(ErrorClass cls) { return rowOf(cls).theta1300; }
const char* drillKey(ErrorClass cls) { return rowOf(cls).drill; }

ErrorClass errorFromKey(const std::string& key)
{
    for (const ClassRow& row : kRows) {
        if (key == row.key && row.cls != ErrorClass::None)
            return row.cls;
    }
    return ErrorClass::None;
}

const char* motifKey(Motif motif)
{
    switch (motif) {
    case Motif::None: return "none";
    case Motif::Fork: return "gabel";
    case Motif::Pin: return "fesselung";
    case Motif::Skewer: return "spiess";
    case Motif::Discovery: return "abzug";
    case Motif::DiscoveredCheck: return "abzugsschach";
    case Motif::DoubleCheck: return "doppelschach";
    case Motif::Hanging: return "haenger";
    case Motif::Deflection: return "ablenkung";
    case Motif::Attraction: return "hinlenkung";
    case Motif::Overloading: return "ueberlastung";
    case Motif::InBetween: return "zwischenzug";
    case Motif::XRay: return "roentgen";
    case Motif::Trapped: return "einsperrung";
    case Motif::MateNet: return "mattnetz";
    }
    return "none";
}

Motif motifFromKey(const std::string& key)
{
    for (int i = 0; i <= static_cast<int>(Motif::MateNet); ++i) {
        const Motif motif = static_cast<Motif>(i);
        if (key == motifKey(motif))
            return motif;
    }
    return Motif::None;
}

const char* motifName(Motif motif)
{
    switch (motif) {
    case Motif::None: return "kein Motiv";
    case Motif::Fork: return "eine Gabel";
    case Motif::Pin: return "eine Fesselung";
    case Motif::Skewer: return "einen Spieß";
    case Motif::Discovery: return "einen Abzug";
    case Motif::DiscoveredCheck: return "ein Abzugsschach";
    case Motif::DoubleCheck: return "ein Doppelschach";
    case Motif::Hanging: return "einen Hänger";
    case Motif::Deflection: return "eine Ablenkung";
    case Motif::Attraction: return "eine Hinlenkung";
    case Motif::Overloading: return "eine Überlastung";
    case Motif::InBetween: return "einen Zwischenzug";
    case Motif::XRay: return "eine Röntgenwirkung";
    case Motif::Trapped: return "eine Einsperrung";
    case Motif::MateNet: return "ein Mattnetz";
    }
    return "kein Motiv";
}

const char* suppressionKey(Suppression suppression)
{
    switch (suppression) {
    case Suppression::None: return "";
    case Suppression::U1_StillWinning: return "U1";
    case Suppression::U2_AlreadyLost: return "U2";
    case Suppression::U3_CollapseUnderway: return "U3";
    case Suppression::U4_OpeningBook: return "U4";
    case Suppression::U5_TimeTrouble: return "U5";
    case Suppression::U6_Forced: return "U6";
    case Suppression::Step0_NotConfirmed: return "S0";
    }
    return "";
}

// ---------------------------------------------------------------------------
// Board helpers. Everything here is conservative on purpose: a rule that fires
// too often is worse than one that stays empty (teacher.md §2.1 point 5).
// ---------------------------------------------------------------------------

namespace {

int chebyshev(int a, int b)
{
    return std::max(std::abs(fileOf(a) - fileOf(b)), std::abs(rankOf(a) - rankOf(b)));
}

bool isPieceOf(char piece, bool white)
{
    if (piece == ' ')
        return false;
    return white ? (piece >= 'A' && piece <= 'Z') : (piece >= 'a' && piece <= 'z');
}

char upper(char c) { return static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c); }

int valueOfLetter(char piece)
{
    switch (upper(piece)) {
    case 'P': return 1;
    case 'N': case 'B': return 3;
    case 'R': return 5;
    case 'Q': return 9;
    default: return 0;
    }
}

bool isSlider(char piece)
{
    const char p = upper(piece);
    return p == 'B' || p == 'R' || p == 'Q';
}

// All squares a piece on `from` attacks in `pos`, ignoring whose turn it is.
std::vector<int> attackedSquares(const Position& pos, int from)
{
    std::vector<int> out;
    const char piece = pos.pieceAt(from);
    if (piece == ' ')
        return out;
    const bool white = isPieceOf(piece, true);
    for (int square = 0; square < 64; ++square) {
        if (square == from)
            continue;
        const std::vector<int> attackers = pos.attackersOf(square, white);
        if (std::find(attackers.begin(), attackers.end(), from) != attackers.end())
            out.push_back(square);
    }
    return out;
}

int totalHangingValue(const std::vector<Hanging>& list)
{
    int sum = 0;
    for (const Hanging& entry : list)
        sum += entry.value;
    return sum;
}

std::vector<Hanging> difference(const std::vector<Hanging>& after, const std::vector<Hanging>& before)
{
    std::vector<Hanging> out;
    for (const Hanging& entry : after) {
        if (std::find(before.begin(), before.end(), entry) == before.end())
            out.push_back(entry);
    }
    return out;
}

bool isPassedPawn(const Position& pos, int square, bool white)
{
    const int file = fileOf(square);
    const int rank = rankOf(square);
    for (int f = std::max(0, file - 1); f <= std::min(7, file + 1); ++f) {
        for (int r = 0; r < 8; ++r) {
            const bool ahead = white ? (r > rank) : (r < rank);
            if (!ahead)
                continue;
            const char piece = pos.pieceAt(r * 8 + f);
            if (upper(piece) == 'P' && isPieceOf(piece, !white))
                return false;
        }
    }
    return true;
}

// teacher.md B5/F3: can the defending king still catch the passer?
bool passerCatchable(const Position& pos, int pawnSquare, bool pawnIsWhite, bool defenderToMove)
{
    const int file = fileOf(pawnSquare);
    const int rank = rankOf(pawnSquare);
    const int promoRank = pawnIsWhite ? 7 : 0;
    int steps = std::abs(promoRank - rank);
    const int startRank = pawnIsWhite ? 1 : 6;
    if (rank == startRank)
        steps -= 1;   // the double step: a pawn on the 2nd rank counts as on the 3rd
    const int promoSquare = promoRank * 8 + file;
    const int king = pos.kingSquare(!pawnIsWhite);
    const int distance = chebyshev(king, promoSquare);
    return distance <= steps - (defenderToMove ? 0 : 1);
}

bool hasUncatchablePasser(const Position& pos, bool ownerWhite, bool defenderToMove, int* squareOut)
{
    for (int square = 0; square < 64; ++square) {
        const char piece = pos.pieceAt(square);
        if (upper(piece) != 'P' || !isPieceOf(piece, ownerWhite))
            continue;
        if (!isPassedPawn(pos, square, ownerWhite))
            continue;
        if (!passerCatchable(pos, square, ownerWhite, defenderToMove)) {
            if (squareOut)
                *squareOut = square;
            return true;
        }
    }
    return false;
}

bool isPawnEndgame(const Position& pos)
{
    for (int square = 0; square < 64; ++square) {
        const char piece = upper(pos.pieceAt(square));
        if (piece != ' ' && piece != 'P' && piece != 'K')
            return false;
    }
    return true;
}

// teacher.md E8: kings on the same file, rank or diagonal with an odd number
// of squares between them; the side *not* to move has the opposition.
bool hasOpposition(const Position& pos, bool white)
{
    const int a = pos.kingSquare(true);
    const int b = pos.kingSquare(false);
    const int df = std::abs(fileOf(a) - fileOf(b));
    const int dr = std::abs(rankOf(a) - rankOf(b));
    const bool aligned = (df == 0) || (dr == 0) || (df == dr);
    if (!aligned)
        return false;
    const int between = std::max(df, dr) - 1;
    if (between < 1 || between % 2 == 0)
        return false;
    return pos.whiteToMove() != white;
}

// teacher.md E9, for a single pawn.
std::vector<int> keySquares(int pawnSquare, bool white)
{
    std::vector<int> out;
    const int file = fileOf(pawnSquare);
    const int rank = rankOf(pawnSquare);
    const int forward = white ? 1 : -1;
    const int relativeRank = white ? rank : 7 - rank;
    if (file == 0 || file == 7) {
        // Rook pawn: the two squares next to the promotion square.
        const int promoRank = white ? 7 : 0;
        const int neighbour = file == 0 ? 1 : 6;
        out.push_back(promoRank * 8 + neighbour);
        out.push_back((promoRank - forward) * 8 + neighbour);
        return out;
    }
    const int firstRow = rank + 2 * forward;
    for (int f = file - 1; f <= file + 1; ++f) {
        if (f < 0 || f > 7)
            continue;
        if (firstRow >= 0 && firstRow < 8)
            out.push_back(firstRow * 8 + f);
        if (relativeRank >= 4) {
            const int secondRow = rank + forward;
            if (secondRow >= 0 && secondRow < 8)
                out.push_back(secondRow * 8 + f);
        }
    }
    return out;
}

int singlePawnSquare(const Position& pos, bool white)
{
    int found = -1;
    for (int square = 0; square < 64; ++square) {
        const char piece = pos.pieceAt(square);
        if (upper(piece) != 'P')
            continue;
        if (!isPieceOf(piece, white))
            return -1;   // pawns on both sides: not the E9 case
        if (found >= 0)
            return -1;
        found = square;
    }
    return found;
}

int doubledAndIsolated(const Position& pos, bool white)
{
    int perFile[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    for (int square = 0; square < 64; ++square) {
        const char piece = pos.pieceAt(square);
        if (upper(piece) == 'P' && isPieceOf(piece, white))
            ++perFile[fileOf(square)];
    }
    int penalty = 0;
    for (int file = 0; file < 8; ++file) {
        if (perFile[file] > 1)
            penalty += perFile[file] - 1;
        if (perFile[file] > 0) {
            const bool left = file > 0 && perFile[file - 1] > 0;
            const bool right = file < 7 && perFile[file + 1] > 0;
            if (!left && !right)
                penalty += 1;
        }
    }
    return penalty;
}

bool fileIsOpenFor(const Position& pos, int file, bool white)
{
    bool own = false;
    for (int rank = 0; rank < 8; ++rank) {
        const char piece = pos.pieceAt(rank * 8 + file);
        if (upper(piece) == 'P' && isPieceOf(piece, white))
            own = true;
    }
    return !own;   // open or half-open from our side
}

bool rookOnFile(const Position& pos, int file, bool white)
{
    for (int rank = 0; rank < 8; ++rank) {
        const char piece = pos.pieceAt(rank * 8 + file);
        if (upper(piece) == 'R' && isPieceOf(piece, white))
            return true;
    }
    return false;
}

int centreDistance(int square)
{
    const int file = fileOf(square);
    const int rank = rankOf(square);
    return std::max(std::abs(2 * file - 7), std::abs(2 * rank - 7));
}

int pawnCentroid(const Position& pos)
{
    int sumFile = 0, sumRank = 0, count = 0;
    for (int square = 0; square < 64; ++square) {
        if (upper(pos.pieceAt(square)) == 'P') {
            sumFile += fileOf(square);
            sumRank += rankOf(square);
            ++count;
        }
    }
    if (count == 0)
        return -1;
    return (sumRank / count) * 8 + (sumFile / count);
}

bool wrongBishopRookPawn(const Position& pos, bool white)
{
    int bishop = -1;
    int bishops = 0;
    int pawnFile = -1;
    int pawns = 0;
    for (int square = 0; square < 64; ++square) {
        const char piece = pos.pieceAt(square);
        if (!isPieceOf(piece, white))
            continue;
        const char type = upper(piece);
        if (type == 'B') {
            bishop = square;
            ++bishops;
        } else if (type == 'P') {
            pawnFile = fileOf(square);
            ++pawns;
        } else if (type != 'K') {
            return false;   // more than bishop and pawns: not the F8 case
        }
    }
    if (bishops != 1 || pawns == 0 || bishop < 0)
        return false;
    if (pawnFile != 0 && pawnFile != 7)
        return false;
    const int promoSquare = (white ? 7 : 0) * 8 + pawnFile;
    const bool promoLight = ((fileOf(promoSquare) + rankOf(promoSquare)) & 1) != 0;
    const bool bishopLight = ((fileOf(bishop) + rankOf(bishop)) & 1) != 0;
    return promoLight != bishopLight;
}

} // namespace

// ---------------------------------------------------------------------------
// Motif detection, teacher.md §2.5. These predicates are heuristic; the spec
// says so explicitly and asks for counters on each of them, which Skill.cpp
// provides.
// ---------------------------------------------------------------------------

Motif detectMotif(const Position& afterPlayed, const std::string& replyUci,
                  const std::vector<Hanging>& newlyHanging, bool mateForReplier)
{
    if (replyUci.size() < 4 || !afterPlayed.isLegal(replyUci))
        return mateForReplier ? Motif::MateNet : Motif::None;

    const int from = squareFromName(replyUci.substr(0, 2));
    const int to = squareFromName(replyUci.substr(2, 2));
    const bool replierWhite = afterPlayed.whiteToMove();
    const Position next = afterPlayed.after(replyUci);
    const char mover = afterPlayed.pieceAt(from);

    // HÄNGER: the reply is simply a capture with SEE > 0 on a piece that the
    // played move newly exposed.
    for (const Hanging& entry : newlyHanging) {
        if (entry.square == to && afterPlayed.see(replyUci) > 0)
            return Motif::Hanging;
    }

    const bool givesCheck = next.inCheck();
    const int victimKing = next.kingSquare(!replierWhite);

    // The squares the moved piece attacks after the reply.
    const std::vector<int> hits = attackedSquares(next, to);
    int valuableHits = 0;
    bool hitsKing = false;
    for (int square : hits) {
        const char piece = next.pieceAt(square);
        if (!isPieceOf(piece, !replierWhite))
            continue;
        if (upper(piece) == 'K') {
            hitsKing = true;
            ++valuableHits;
            continue;
        }
        if (next.seeOnSquare(square, replierWhite) > 0)
            ++valuableHits;
    }

    // ABZUG: the moved piece uncovered a line of one of its own sliders.
    bool discovered = false;
    bool discoveredCheck = false;
    for (int square = 0; square < 64; ++square) {
        const char piece = next.pieceAt(square);
        if (square == to || !isPieceOf(piece, replierWhite) || !isSlider(piece))
            continue;
        const std::vector<int> before = attackedSquares(afterPlayed, square);
        const std::vector<int> now = attackedSquares(next, square);
        for (int target : now) {
            if (std::find(before.begin(), before.end(), target) != before.end())
                continue;
            // The line only opened because `from` was vacated.
            if (std::max(std::abs(fileOf(square) - fileOf(from)), std::abs(rankOf(square) - rankOf(from))) == 0)
                continue;
            const char hit = next.pieceAt(target);
            if (target == victimKing) {
                discovered = true;
                discoveredCheck = true;
            } else if (isPieceOf(hit, !replierWhite) && next.seeOnSquare(target, replierWhite) > 0) {
                discovered = true;
            }
        }
    }
    if (discoveredCheck && givesCheck && upper(mover) != 'K') {
        // The moving piece checks as well -> double check.
        const std::vector<int> own = attackedSquares(next, to);
        if (std::find(own.begin(), own.end(), victimKing) != own.end())
            return Motif::DoubleCheck;
        return Motif::DiscoveredCheck;
    }
    if (discovered)
        return Motif::Discovery;

    if (valuableHits >= 2 && (hitsKing || valuableHits >= 2))
        return Motif::Fork;

    // FESSELUNG / SPIESS: a slider now looks through an enemy piece at the
    // king or at something more valuable.
    if (isSlider(mover)) {
        for (int square = 0; square < 64; ++square) {
            const char front = next.pieceAt(square);
            if (!isPieceOf(front, !replierWhite))
                continue;
            const int df = fileOf(square) - fileOf(to);
            const int dr = rankOf(square) - rankOf(to);
            if (df == 0 && dr == 0)
                continue;
            const bool straight = (df == 0 || dr == 0);
            const bool diagonal = std::abs(df) == std::abs(dr);
            if (!straight && !diagonal)
                continue;
            const char type = upper(mover);
            if (straight && type == 'B')
                continue;
            if (diagonal && type == 'R')
                continue;
            const int stepF = (df > 0) - (df < 0);
            const int stepR = (dr > 0) - (dr < 0);
            // walk from `to` to `square`; it must be clear in between
            bool clear = true;
            int walk = to;
            while (true) {
                walk = (rankOf(walk) + stepR) * 8 + (fileOf(walk) + stepF);
                if (walk == square)
                    break;
                if (next.pieceAt(walk) != ' ') {
                    clear = false;
                    break;
                }
            }
            if (!clear)
                continue;
            // and continue behind it
            int behind = square;
            while (true) {
                const int file = fileOf(behind) + stepF;
                const int rank = rankOf(behind) + stepR;
                if (file < 0 || file > 7 || rank < 0 || rank > 7)
                    break;
                behind = rank * 8 + file;
                const char back = next.pieceAt(behind);
                if (back == ' ')
                    continue;
                if (!isPieceOf(back, !replierWhite))
                    break;
                const int frontValue = upper(front) == 'K' ? 100 : valueOfLetter(front);
                const int backValue = upper(back) == 'K' ? 100 : valueOfLetter(back);
                if (backValue > frontValue)
                    return Motif::Pin;
                if (frontValue > backValue && upper(front) != 'K')
                    return Motif::Skewer;
                if (upper(front) == 'K')
                    return Motif::Skewer;
                break;
            }
        }
    }

    // EINSPERRUNG: the piece that was attacked has no square left with SEE <= 0.
    for (const Hanging& entry : newlyHanging) {
        const int square = entry.square;
        const std::vector<int> escapes = afterPlayed.legalTargets(square);
        if (!escapes.empty())
            continue;
        return Motif::Trapped;
    }
    for (int square = 0; square < 64; ++square) {
        const char piece = next.pieceAt(square);
        if (!isPieceOf(piece, !replierWhite) || valueOfLetter(piece) < 3)
            continue;
        if (next.seeOnSquare(square, replierWhite) <= 0)
            continue;
        bool escape = false;
        Position passed = next;
        if (!passed.inCheck()) {
            passed = next.afterNullMove();
            for (int target : passed.legalTargets(square)) {
                Position escaped = passed.after(squareName(square) + squareName(target));
                if (escaped.seeOnSquare(target, replierWhite) <= 0) {
                    escape = true;
                    break;
                }
            }
        }
        if (!escape)
            return Motif::Trapped;
    }

    // ABLENKUNG / ÜBERLASTUNG: the reply attacks a piece that is currently the
    // only defender of something else.
    for (int square = 0; square < 64; ++square) {
        const char piece = next.pieceAt(square);
        if (!isPieceOf(piece, !replierWhite))
            continue;
        const std::vector<int> attackers = next.attackersOf(square, replierWhite);
        if (attackers.empty())
            continue;
        int guarded = 0;
        for (int target : attackedSquares(next, square)) {
            const char friendly = next.pieceAt(target);
            if (isPieceOf(friendly, !replierWhite) && next.attackerCount(target, !replierWhite) == 1
                && next.attackerCount(target, replierWhite) > 0)
                ++guarded;
        }
        if (guarded >= 2)
            return Motif::Overloading;
        if (guarded == 1)
            return Motif::Deflection;
    }

    if (afterPlayed.isCapture(replyUci) && givesCheck)
        return Motif::Attraction;
    if (givesCheck && !afterPlayed.isCapture(replyUci))
        return Motif::InBetween;
    if (mateForReplier)
        return Motif::MateNet;
    if (afterPlayed.see(replyUci) > 0)
        return Motif::Hanging;
    return Motif::None;
}

// ---------------------------------------------------------------------------
// The feature vector, teacher.md §2.2
// ---------------------------------------------------------------------------

MoveFeatures buildFeatures(const Position& before, const std::string& playedUci,
                           const EngineView& engine, const GameContext& game)
{
    MoveFeatures m;
    m.game = &game;
    m.fen = before.fen();
    m.played = playedUci;
    m.best = engine.best;
    m.bestPv = engine.bestPv;
    m.opponentReply = engine.opponentReply;
    m.before = engine.before;
    m.after = engine.after;
    m.wcBefore = winningChances(engine.before);
    m.wcAfter = winningChances(engine.after);
    m.dW = deltaW(engine.before, engine.after);
    m.ply = game.ply;
    m.moveTimeMs = game.moveTimeMs;
    m.tbBefore = engine.tbBefore;
    m.tbAfter = engine.tbAfter;
    m.bestIsOnlyMove = engine.bestIsOnlyMove;
    m.refutationConfirmed = engine.refutationConfirmed;
    m.inOpeningBook = engine.inOpeningBook;
    m.playedIndexInBestPv = engine.playedIndexInBestPv;
    m.equalUntilPly = engine.equalUntilPly;

    if (engine.before.valid && engine.before.isMate) {
        m.mateBefore = engine.before.mateIn > 0;
        m.mateAgainstBefore = engine.before.mateIn < 0;
        m.mateInBefore = engine.before.mateIn;
    }
    if (engine.after.valid && engine.after.isMate) {
        // `after` is from the mover's point of view, so a mate *against* him
        // is a negative mate distance.
        m.mateAfter = engine.after.mateIn > 0;
        m.mateAgainstAfter = engine.after.mateIn < 0;
        m.mateInAfter = engine.after.mateIn;
    }

    const bool learnerWhite = before.whiteToMove();
    m.phase = before.phase();
    m.officers = before.officers();
    m.pieceCount = before.pieceCount();
    m.material = learnerWhite ? before.materialBalance() : -before.materialBalance();

    if (!before.isLegal(playedUci))
        return m;

    const Position afterPos = before.after(playedUci);

    m.playedIsCapture = before.isCapture(playedUci);
    m.playedIsCheck = before.givesCheck(playedUci);
    m.playedIsPromotion = before.isPromotion(playedUci);
    m.seePlayed = before.see(playedUci);
    const int playedFrom = squareFromName(playedUci.substr(0, 2));
    const int playedTo = squareFromName(playedUci.substr(2, 2));
    m.movedPiece = before.pieceAt(playedFrom);
    m.capturedPiece = before.pieceAt(playedTo);
    m.capturedValue = valueOfLetter(m.capturedPiece);

    if (!engine.best.empty() && before.isLegal(engine.best)) {
        m.bestIsCapture = before.isCapture(engine.best);
        m.bestIsCheck = before.givesCheck(engine.best);
        m.bestIsQuiet = before.isQuiet(engine.best);
        const int bestFrom = squareFromName(engine.best.substr(0, 2));
        const int bestTo = squareFromName(engine.best.substr(2, 2));
        const int forward = learnerWhite ? 1 : -1;
        m.bestIsBackward = (rankOf(bestTo) - rankOf(bestFrom)) * forward < 0;
        m.bestIsLongSlide = isSlider(before.pieceAt(bestFrom)) && chebyshev(bestFrom, bestTo) >= 4;
        m.bestIsEdge = fileOf(bestTo) == 0 || fileOf(bestTo) == 7 || rankOf(bestTo) == 0 || rankOf(bestTo) == 7;
    }

    m.hangingBefore = before.hanging(learnerWhite);
    m.hangingAfter = afterPos.hanging(learnerWhite);
    m.newlyHanging = difference(m.hangingAfter, m.hangingBefore);

    // teacher.md §0.4 defines the threat as the value of a free move. The
    // document writes it as W(nullEval) - W(eval), which comes out negative;
    // we use the sign that makes "a big threat" a big positive number, which
    // is what every rule in §2.6 assumes.
    if (engine.nullBefore.valid && engine.before.valid)
        m.threatBefore = winProbability(engine.before) - winProbability(engine.nullBefore);
    if (engine.nullAfter.valid && engine.after.valid)
        m.threatAfter = winProbability(engine.nullAfter) - winProbability(engine.after);
    m.threatAddressed = m.threatAfter < 0.4 * m.threatBefore;

    // Did `best` address it? Without a third engine call this is a board
    // judgement: the exposure drops, or the move is a check.
    if (!engine.best.empty() && before.isLegal(engine.best)) {
        const Position afterBest = before.after(engine.best);
        const int exposureBefore = totalHangingValue(m.hangingBefore);
        const int exposureBest = totalHangingValue(afterBest.hanging(learnerWhite));
        m.bestAddressesThreat = m.bestIsCheck || m.bestIsCapture
                || exposureBest * 2 <= exposureBefore;
    }

    m.replyMotif = detectMotif(afterPos, engine.opponentReply, m.newlyHanging,
                               m.mateAgainstAfter);
    m.replyIsCheck = !engine.opponentReply.empty() && afterPos.givesCheck(engine.opponentReply);

    // C3 needs the motif of the move the learner *should* have played, which
    // is the same predicate set applied to his own side.
    if (!engine.best.empty() && before.isLegal(engine.best)) {
        const Position afterBest = before.after(engine.best);
        const std::vector<Hanging> exposed =
                difference(afterBest.hanging(!learnerWhite), before.hanging(!learnerWhite));
        m.bestMotif = detectMotif(before, engine.best, exposed, m.mateBefore);
    }

    // E1: the naive count said the exchange works out.
    if (m.playedIsCapture && playedTo >= 0) {
        m.attackersOnTarget = before.attackerCount(playedTo, learnerWhite);
        m.defendersOnTarget = before.attackerCount(playedTo, !learnerWhite);
    }

    // C2: a piece was simply standing there.
    int freeSee = 0;
    const std::string free = before.bestFreeCapture(2, &freeSee);
    if (!free.empty() && free != playedUci) {
        m.freeCaptureMissed = true;
        m.freeCaptureMove = free;
        m.freeCaptureSee = freeSee;
    }

    // B3: back rank.
    const int king = afterPos.kingSquare(learnerWhite);
    const int homeRank = learnerWhite ? 0 : 7;
    if (rankOf(king) == homeRank) {
        bool luft = false;
        const int secondRank = learnerWhite ? 1 : 6;
        for (int file = std::max(0, fileOf(king) - 1); file <= std::min(7, fileOf(king) + 1); ++file) {
            const char piece = afterPos.pieceAt(secondRank * 8 + file);
            if (!(upper(piece) == 'P' && isPieceOf(piece, learnerWhite))) {
                luft = true;
                break;
            }
        }
        m.kingHasLuft = luft;
        m.mateSquareOnOwnBackRank = m.mateAgainstAfter && !luft;
    }

    // B4: how many of his pieces bear on the two-square ring around our king.
    {
        const int ownKing = before.kingSquare(learnerWhite);
        int count = 0;
        for (int square = 0; square < 64; ++square) {
            const char piece = before.pieceAt(square);
            if (!isPieceOf(piece, !learnerWhite) || upper(piece) == 'K' || upper(piece) == 'P')
                continue;
            bool bears = false;
            for (int target : attackedSquares(before, square)) {
                if (chebyshev(target, ownKing) <= 2) {
                    bears = true;
                    break;
                }
            }
            if (bears)
                ++count;
        }
        m.attackersNearOwnKing = count;
        m.playedDefendsKingZone = playedTo >= 0 && chebyshev(playedTo, ownKing) <= 2;
    }

    // D2 / D3
    if (upper(m.movedPiece) == 'P' && !m.playedIsCapture && !m.playedIsCheck) {
        const int ownKing = before.kingSquare(learnerWhite);
        const int kingFile = fileOf(ownKing);
        const bool sameWing = std::abs(fileOf(playedFrom) - kingFile) <= 2;
        const bool castled = kingFile >= 5 || kingFile <= 2;
        int enemyOnWing = 0;
        for (int square = 0; square < 64; ++square) {
            const char piece = before.pieceAt(square);
            if (!isPieceOf(piece, !learnerWhite) || upper(piece) == 'P' || upper(piece) == 'K')
                continue;
            if (std::abs(fileOf(square) - kingFile) <= 3)
                ++enemyOnWing;
        }
        m.enemyPiecesOnThatWing = enemyOnWing;
        m.playedIsKingShieldPawn = sameWing && castled && enemyOnWing >= 2;
    }
    if (upper(m.movedPiece) == 'Q') {
        const int relativeRank = learnerWhite ? rankOf(playedTo) : 7 - rankOf(playedTo);
        m.queenLeftThirdRank = relativeRank >= 3;
    }

    // G1 / G3 / G5
    m.playedCreatesWeakPawn = doubledAndIsolated(afterPos, learnerWhite)
            > doubledAndIsolated(before, learnerWhite);
    if (!engine.best.empty() && before.isLegal(engine.best)) {
        const Position afterBest = before.after(engine.best);
        m.bestCreatesWeakPawn = doubledAndIsolated(afterBest, learnerWhite)
                > doubledAndIsolated(before, learnerWhite);
        const int bestTo = squareFromName(engine.best.substr(2, 2));
        const int bestFrom = squareFromName(engine.best.substr(0, 2));
        if (upper(before.pieceAt(bestFrom)) == 'R' && bestTo >= 0) {
            const int file = fileOf(bestTo);
            m.bestTakesOpenFile = fileIsOpenFor(before, file, learnerWhite)
                    && !rookOnFile(before, file, learnerWhite);
            m.ownRookOnOpenFile = rookOnFile(before, file, learnerWhite);
        }
    }
    m.playedGainsCompensation = m.playedIsCapture || m.playedIsCheck
            || (learnerWhite ? afterPos.materialBalance() : -afterPos.materialBalance()) > m.material;

    if (m.playedIsCapture && valueOfLetter(m.movedPiece) == valueOfLetter(m.capturedPiece)
        && valueOfLetter(m.movedPiece) > 0) {
        const int ownMobility = before.mobilityOf(playedFrom);
        const int theirMobility = before.mobilityOf(playedTo);
        m.tradedGoodForBad = ownMobility >= 10 && theirMobility <= 4;
    }

    // G2: a move without a purpose.
    if (!m.playedIsCheck && !m.playedIsCapture) {
        const bool develops = upper(m.movedPiece) != 'P'
                && centreDistance(playedTo) < centreDistance(playedFrom);
        int threatAfterOwn = 0;
        if (!afterPos.inCheck()) {
            const Position passed = afterPos.afterNullMove();
            passed.bestFreeCapture(1, &threatAfterOwn);
        }
        const bool improvesKing = m.playedDefendsKingZone;
        m.playedIsPurposeless = !develops && threatAfterOwn <= 0 && !improvesKing;
    }

    // Endgame features.
    if (m.phase == Phase::Endgame) {
        m.pawnEndgameOnly = isPawnEndgame(before);
        int passer = -1;
        m.enemyPassedPawnUncatchable = !hasUncatchablePasser(before, !learnerWhite, true, &passer)
                && hasUncatchablePasser(afterPos, !learnerWhite, false, &passer);
        m.ownPassedPawnLost = hasUncatchablePasser(before, learnerWhite, false, &passer)
                && !hasUncatchablePasser(afterPos, learnerWhite, true, &passer);
        if (m.pawnEndgameOnly && before.pieceCount() <= 6)
            m.lostOpposition = hasOpposition(before, learnerWhite) && !hasOpposition(afterPos, learnerWhite);
        const int pawn = singlePawnSquare(before, learnerWhite);
        if (pawn >= 0) {
            const std::vector<int> keys = keySquares(pawn, learnerWhite);
            const bool wasOn = std::find(keys.begin(), keys.end(), before.kingSquare(learnerWhite)) != keys.end();
            const bool isOn = std::find(keys.begin(), keys.end(), afterPos.kingSquare(learnerWhite)) != keys.end();
            m.leftKeySquares = wasOn && !isOn;
        }
        const int centroid = pawnCentroid(before);
        if (centroid >= 0) {
            m.kingPassive = chebyshev(before.kingSquare(learnerWhite), centroid) >= 4;
            if (!engine.best.empty() && before.isLegal(engine.best)) {
                const int bestFrom = squareFromName(engine.best.substr(0, 2));
                const int bestTo = squareFromName(engine.best.substr(2, 2));
                m.bestActivatesKing = upper(before.pieceAt(bestFrom)) == 'K'
                        && chebyshev(bestTo, centroid) < chebyshev(bestFrom, centroid)
                        && upper(m.movedPiece) != 'K';
            }
        }
        // F5: rook behind the passed pawn.
        if (!engine.best.empty() && before.isLegal(engine.best)) {
            const int bestFrom = squareFromName(engine.best.substr(0, 2));
            const int bestTo = squareFromName(engine.best.substr(2, 2));
            for (int square = 0; square < 64; ++square) {
                const char piece = before.pieceAt(square);
                if (upper(piece) != 'P')
                    continue;
                const bool ownerWhite = isPieceOf(piece, true);
                if (!isPassedPawn(before, square, ownerWhite))
                    continue;
                const bool behind = fileOf(bestTo) == fileOf(square)
                        && ((ownerWhite && rankOf(bestTo) < rankOf(square))
                            || (!ownerWhite && rankOf(bestTo) > rankOf(square)));
                if (behind && upper(before.pieceAt(bestFrom)) == 'R') {
                    m.bestPutsRookBehindPasser = true;
                    m.rookMispostedVsPassedPawn = upper(m.movedPiece) != 'R'
                            || fileOf(playedTo) != fileOf(square);
                }
            }
        }
        // F6: the exchange that removes the last piece.
        if (m.playedIsCapture && before.officers() == 2 && afterPos.officers() == 0)
            m.lastPieceTraded = true;
        m.wrongBishopRookPawn = wrongBishopRookPawn(before, learnerWhite)
                || wrongBishopRookPawn(afterPos, learnerWhite);
    }

    // I1 / I2: the criticality span of teacher.md §2.6.
    if (engine.before.valid && engine.third.valid)
        m.spreadTopThree = winProbability(engine.before) - winProbability(engine.third);

    return m;
}

// ---------------------------------------------------------------------------
// Suppression, teacher.md §2.3.3
// ---------------------------------------------------------------------------


Suppression suppressionOf(const MoveFeatures& m)
{
    // Step 0 of the check order comes before the suppression rules: if the
    // refutation is not really the engine's best move, or the advantage does
    // not survive four more half moves, the event is an artefact.
    if (!m.refutationConfirmed)
        return Suppression::Step0_NotConfirmed;

    if (m.wcBefore >= 0.90 && m.wcAfter >= 0.90)
        return Suppression::U1_StillWinning;
    if (m.wcBefore <= -0.80)
        return Suppression::U2_AlreadyLost;
    if (m.wcAfter <= -0.90 && m.wcBefore <= -0.60)
        return Suppression::U3_CollapseUnderway;
    if (m.ply <= 10 && m.inOpeningBook)
        return Suppression::U4_OpeningBook;
    if (m.game && m.moveTimeMs >= 0 && m.moveTimeMs < 1500
        && m.game->remainingFraction >= 0.0 && m.game->remainingFraction < 0.10)
        return Suppression::U5_TimeTrouble;
    if (m.game && (m.game->onlyLegalMove || m.game->repetition))
        return Suppression::U6_Forced;
    return Suppression::None;
}

// ---------------------------------------------------------------------------
// The 50 detection rules, teacher.md §2.6, in the check order of §2.4.
// ---------------------------------------------------------------------------

namespace {

bool isPoisonedPawn(const MoveFeatures& m)
{
    // A10. Checked as a guard inside group A so that the more specific class
    // is not swallowed by A3/A7/A8, which would otherwise fire first.
    return m.playedIsCapture && upper(m.capturedPiece) == 'P'
            && (upper(m.movedPiece) == 'Q' || upper(m.movedPiece) == 'R')
            && (m.replyMotif == Motif::Trapped || m.replyMotif == Motif::Fork
                || m.replyMotif == Motif::Deflection);
}

bool tbSaysF7(const MoveFeatures& m)
{
    return m.tbBefore == Tb::Draw && m.tbAfter == Tb::Loss;
}

bool tbSaysF8(const MoveFeatures& m)
{
    return m.wrongBishopRookPawn
            && (m.tbBefore == Tb::Draw || m.tbAfter == Tb::Draw || m.pieceCount <= 5);
}

// Common precondition of group B (teacher.md §2.6, group B header).
bool threatWasThere(const MoveFeatures& m)
{
    return m.threatBefore >= 12.0 && !m.threatAddressed && m.bestAddressesThreat;
}

// Common precondition of group C.
bool chanceWasThere(const MoveFeatures& m)
{
    return m.dW >= 12.0;
}

ErrorClass checkGroupB(const MoveFeatures& m)
{
    if (m.mateAgainstAfter && !m.mateAgainstBefore) {
        if (m.mateSquareOnOwnBackRank)
            return ErrorClass::B3;
        if (m.mateInAfter == -1)
            return ErrorClass::B2n;
        return ErrorClass::B2;
    }
    if (m.phase == Phase::Endgame && m.enemyPassedPawnUncatchable
        && !tbSaysF7(m) && !tbSaysF8(m))
        return ErrorClass::B5;
    if (m.threatBefore >= 12.0 && m.attackersNearOwnKing >= 3 && !m.playedDefendsKingZone)
        return ErrorClass::B4;
    if (threatWasThere(m))
        return ErrorClass::B1;
    return ErrorClass::None;
}

ErrorClass checkGroupC(const MoveFeatures& m)
{
    if (m.mateBefore && !m.mateAfter)
        return ErrorClass::C1;
    if (!chanceWasThere(m))
        return ErrorClass::None;
    if (m.phase == Phase::Endgame
        && ((m.tbBefore == Tb::Win && m.tbAfter == Tb::Draw)
            || (m.wcBefore >= 0.85 && m.wcAfter <= 0.35 && m.pieceCount <= 8)))
        return ErrorClass::C6;
    if (m.freeCaptureMissed)
        return ErrorClass::C2;
    if ((m.bestIsCapture || m.bestIsCheck) && m.bestMotif != Motif::None)
        return ErrorClass::C3;
    // The C group's common precondition is only "best would have gained 12 pp
    // more", which every error satisfies. C4 therefore needs one more guard,
    // or every quiet best move in a game where a piece was dropped would be
    // read as a missed quiet win: nothing of the learner's may have been hung
    // by the move itself — that is group A's business.
    if (m.bestIsQuiet && m.newlyHanging.empty())
        return ErrorClass::C4;
    if (m.bestIsOnlyMove)
        return ErrorClass::C5;
    return ErrorClass::None;
}

ErrorClass checkGroupA(const MoveFeatures& m)
{
    const bool poisoned = isPoisonedPawn(m);
    if (!m.newlyHanging.empty() && m.replyMotif == Motif::Hanging
        && m.newlyHanging.front().value >= 3)
        return ErrorClass::A1;
    if (m.playedIsCapture && m.seePlayed <= -1)
        return ErrorClass::A2;
    if (m.replyMotif == Motif::Fork && !poisoned)
        return m.replyIsCheck ? ErrorClass::A3k : ErrorClass::A3;
    if (m.replyMotif == Motif::Pin)
        return ErrorClass::A4;
    if (m.replyMotif == Motif::Skewer)
        return ErrorClass::A5;
    if (m.replyMotif == Motif::Discovery || m.replyMotif == Motif::DiscoveredCheck
        || m.replyMotif == Motif::DoubleCheck)
        return ErrorClass::A6;
    if (m.replyMotif == Motif::Trapped && !poisoned)
        return ErrorClass::A7;
    if ((m.replyMotif == Motif::Deflection || m.replyMotif == Motif::Overloading
         || m.replyMotif == Motif::Attraction) && !poisoned)
        return ErrorClass::A8;
    if (m.replyMotif == Motif::InBetween)
        return ErrorClass::A9;
    if (poisoned)
        return ErrorClass::A10;
    return ErrorClass::None;
}

ErrorClass checkGroupF(const MoveFeatures& m)
{
    if (m.phase != Phase::Endgame)
        return ErrorClass::None;
    // F7 and F8 are exact (tablebase) and therefore take precedence.
    if (tbSaysF7(m))
        return ErrorClass::F7;
    if (tbSaysF8(m))
        return ErrorClass::F8;
    if (m.lastPieceTraded && m.wcBefore >= -0.20
        && (m.tbAfter == Tb::Loss || m.wcAfter <= -0.50))
        return ErrorClass::F6;
    if (m.lostOpposition && (m.tbBefore != m.tbAfter || m.tbBefore == Tb::Unknown))
        return ErrorClass::F1;
    if (m.leftKeySquares && (m.tbBefore != m.tbAfter || m.tbBefore == Tb::Unknown))
        return ErrorClass::F2;
    if (m.ownPassedPawnLost)
        return ErrorClass::F3;
    if (m.kingPassive && m.bestActivatesKing)
        return ErrorClass::F4;
    if (m.bestPutsRookBehindPasser && m.rookMispostedVsPassedPawn)
        return ErrorClass::F5;
    return ErrorClass::None;
}

ErrorClass checkGroupE(const MoveFeatures& m)
{
    if (m.playedIsCapture && m.seePlayed <= -1 && m.attackersOnTarget >= m.defendersOnTarget
        && m.attackersOnTarget > 0)
        return ErrorClass::E1;
    if (m.equalUntilPly >= 3)
        return ErrorClass::E6;
    if (m.playedIndexInBestPv >= 2)
        return ErrorClass::E5;
    if (m.bestIsBackward)
        return ErrorClass::E2;
    if (m.bestIsLongSlide)
        return ErrorClass::E3;
    if (m.bestIsEdge)
        return ErrorClass::E4;
    return ErrorClass::None;
}

ErrorClass checkGroupD(const MoveFeatures& m)
{
    if (!m.game)
        return ErrorClass::None;
    if (m.ply >= 24 && m.game->castlingRightUntilPly20 && m.game->kingStillOnStartSquare
        && m.attackersNearOwnKing >= 2 && !m.game->castlingAlreadyReported)
        return ErrorClass::D1;
    if (m.playedIsKingShieldPawn)
        return ErrorClass::D2;
    if (m.phase == Phase::Opening && m.queenLeftThirdRank && m.game->queenAttackTempiNext4 >= 2)
        return ErrorClass::D3;
    return ErrorClass::None;
}

ErrorClass checkGroupG(const MoveFeatures& m, int learnerElo)
{
    if (!m.game)
        return ErrorClass::None;
    const bool strongEnough = learnerElo >= 1300;
    if (strongEnough && m.playedCreatesWeakPawn && !m.bestCreatesWeakPawn
        && !m.playedGainsCompensation)
        return ErrorClass::G1;
    if (strongEnough && m.tradedGoodForBad)
        return ErrorClass::G3;
    if (strongEnough && m.game->weakSquareOccupiedByKnight && upper(m.movedPiece) == 'P')
        return ErrorClass::G4;
    if (strongEnough && m.bestTakesOpenFile && !m.ownRookOnOpenFile)
        return ErrorClass::G5;
    if (m.playedIsPurposeless)
        return ErrorClass::G2;
    return ErrorClass::None;
}

ErrorClass checkGroupH(const MoveFeatures& m)
{
    if (!m.game || m.phase != Phase::Opening || m.game->openingAlreadyReported)
        return ErrorClass::None;
    if (m.dW >= 20.0 && m.ply <= 20 && m.game->knownTrapPosition)
        return ErrorClass::H4;
    if (m.game->samePieceMovesFirst20 >= 3 && m.game->undevelopedMinors >= 2)
        return ErrorClass::H1;
    if (m.game->wingPawnMovesFirst16 >= 2 && m.game->undevelopedMinors >= 2)
        return ErrorClass::H2;
    if (m.ply >= 20 && m.game->developmentDeficitAtPly20 >= 3)
        return ErrorClass::H3;
    return ErrorClass::None;
}

ErrorClass checkGroupI(const MoveFeatures& m, double learningThresholdValue)
{
    if (!m.game)
        return ErrorClass::None;
    const double median = m.game->medianMoveTimeMs;
    if (m.moveTimeMs >= 0 && median > 0.0) {
        if (m.dW >= learningThresholdValue && m.moveTimeMs < 0.4 * median
            && m.spreadTopThree >= 15.0)
            return ErrorClass::I1;
        if (m.moveTimeMs > 3.0 * median && m.spreadTopThree < 5.0)
            return ErrorClass::I2;
    }
    if (m.game->errorsClusterAtEnd)
        return ErrorClass::I3;
    return ErrorClass::None;
}

Finding makeFinding(ErrorClass cls, const MoveFeatures& m, bool belowThreshold)
{
    Finding finding;
    finding.cls = cls;
    finding.dimension = dimensionOf(cls);
    finding.dW = m.dW;
    finding.severity = static_cast<int>(m.dW + (m.dW >= 0 ? 0.5 : -0.5));
    finding.level = severityOf(m.dW);
    finding.motif = (groupOf(cls) == Group::C) ? m.bestMotif : m.replyMotif;
    finding.fen = m.fen;
    finding.playedMove = m.played;
    finding.bestMove = m.best;
    finding.refutation = m.opponentReply;
    finding.ply = m.ply;
    finding.belowThreshold = belowThreshold;
    finding.makesCard = makesCard(cls) && !belowThreshold;
    finding.sentence = sentenceFor(cls, m);
    return finding;
}

} // namespace

std::vector<Finding> classify(const MoveFeatures& m, int learnerElo)
{
    std::vector<Finding> out;
    if (suppressionOf(m) != Suppression::None)
        return out;

    const double threshold = learningThreshold(learnerElo);

    // teacher.md §2.6 A2: a bad exchange in a won position falls out through
    // U1 and is still a calculation error, so A2 and its sharper variant E1 are
    // logged below L as well — without a card, only to feed the REC dimension.
    if (m.dW < threshold) {
        ErrorClass below = ErrorClass::None;
        if (m.playedIsCapture && m.seePlayed <= -1)
            below = (m.attackersOnTarget >= m.defendersOnTarget && m.attackersOnTarget > 0)
                    ? ErrorClass::E1 : ErrorClass::A2;
        if (below != ErrorClass::None)
            out.push_back(makeFinding(below, m, true));
        return out;
    }

    // The fixed order of §2.4. At most two classes are awarded: the first B or
    // C class, plus the first class from another group.
    ErrorClass first = checkGroupB(m);
    Group firstGroup = Group::B;
    if (first == ErrorClass::None) {
        first = checkGroupC(m);
        firstGroup = Group::C;
    }

    const ErrorClass ordered[] = {
        checkGroupA(m),
        checkGroupF(m),
        checkGroupE(m),
        checkGroupD(m),
        checkGroupG(m, learnerElo),
        checkGroupH(m),
        checkGroupI(m, threshold)
    };

    ErrorClass second = ErrorClass::None;
    for (ErrorClass candidate : ordered) {
        if (candidate == ErrorClass::None)
            continue;
        if (first == ErrorClass::None) {
            first = candidate;
            firstGroup = groupOf(candidate);
            continue;
        }
        if (groupOf(candidate) != firstGroup) {
            second = candidate;
            break;
        }
    }

    if (first == ErrorClass::None) {
        // teacher.md §2.4 step 9: Z0 is logged, makes no card, and its share is
        // the quality measure of the taxonomy.
        out.push_back(makeFinding(ErrorClass::Z0, m, false));
        return out;
    }

    out.push_back(makeFinding(first, m, false));
    if (second != ErrorClass::None)
        out.push_back(makeFinding(second, m, false));
    return out;
}

// ---------------------------------------------------------------------------
// The sentence, teacher.md §6.6
// ---------------------------------------------------------------------------

namespace {

const char* germanPiece(char piece)
{
    switch (upper(piece)) {
    case 'P': return "der Bauer";
    case 'N': return "der Springer";
    case 'B': return "der Läufer";
    case 'R': return "der Turm";
    case 'Q': return "die Dame";
    case 'K': return "der König";
    default: return "die Figur";
    }
}

// German needs the accusative with a possessive in half the sentences, and it
// is gendered: "deinen Springer" but "deine Dame".
const char* germanPieceAccusative(char piece)
{
    switch (upper(piece)) {
    case 'P': return "deinen Bauern";
    case 'N': return "deinen Springer";
    case 'B': return "deinen Läufer";
    case 'R': return "deinen Turm";
    case 'Q': return "deine Dame";
    case 'K': return "deinen König";
    default: return "deine Figur";
    }
}

void replaceAll(std::string& text, const std::string& what, const std::string& with)
{
    std::size_t at = 0;
    while ((at = text.find(what, at)) != std::string::npos) {
        text.replace(at, what.size(), with);
        at += with.size();
    }
}

} // namespace

std::string sentenceFor(ErrorClass cls, const MoveFeatures& m)
{
    std::string text = errorTemplate(cls);
    if (text.empty())
        return text;

    Position before(m.fen);
    std::string played = before.sanOf(m.played);
    if (played.empty())
        played = m.played;
    std::string best = before.sanOf(m.best);
    if (best.empty())
        best = m.best;

    std::string refutation = m.opponentReply;
    if (before.isLegal(m.played)) {
        const Position afterPos = before.after(m.played);
        const std::string san = afterPos.sanOf(m.opponentReply);
        if (!san.empty())
            refutation = san;
    }

    // Which piece and which square the sentence is about depends on the class.
    int square = -1;
    char piece = ' ';
    switch (cls) {
    case ErrorClass::A1:
    case ErrorClass::A4:
    case ErrorClass::A7:
        if (!m.newlyHanging.empty()) {
            square = m.newlyHanging.front().square;
            piece = m.newlyHanging.front().piece;
        }
        break;
    case ErrorClass::A2:
    case ErrorClass::E1:
    case ErrorClass::A10:
        square = squareFromName(m.played.size() >= 4 ? m.played.substr(2, 2) : std::string());
        piece = m.capturedPiece;
        break;
    case ErrorClass::C2:
        if (m.freeCaptureMove.size() >= 4) {
            square = squareFromName(m.freeCaptureMove.substr(2, 2));
            piece = before.pieceAt(square);
        }
        break;
    default:
        break;
    }

    replaceAll(text, "{gespielt}", played);
    replaceAll(text, "{bester}", best);
    replaceAll(text, "{widerlegung}", refutation.empty() ? std::string("seinem Zug") : refutation);
    replaceAll(text, "{figurAkk}",
               piece == ' ' ? std::string("deine Figur") : std::string(germanPieceAccusative(piece)));
    replaceAll(text, "{figur}", piece == ' ' ? std::string("die Figur") : std::string(germanPiece(piece)));
    replaceAll(text, "{feld}", square >= 0 ? squareName(square) : std::string("diesem Feld"));
    replaceAll(text, "{motiv}",
               motifName(groupOf(cls) == Group::C ? m.bestMotif : m.replyMotif));
    return text;
}

// teacher.md §2.3.3 U7.
std::vector<std::size_t> selectEvents(const std::vector<double>& deltaW, std::size_t maxEvents)
{
    std::vector<std::size_t> order(deltaW.size());
    std::iota(order.begin(), order.end(), static_cast<std::size_t>(0));
    std::stable_sort(order.begin(), order.end(), [&deltaW](std::size_t a, std::size_t b) {
        if (deltaW[a] != deltaW[b])
            return deltaW[a] > deltaW[b];
        return a < b;   // on a tie the earlier one wins: it is the cause
    });
    if (order.size() > maxEvents)
        order.resize(maxEvents);
    std::sort(order.begin(), order.end());
    return order;
}

} // namespace core
} // namespace schach
