# harbour-schachlehrer — Architektur

Eine Schach-Lern-App für Sailfish OS. Sie folgt nicht einem festen Lehrplan, sondern einer Schleife:
**messen → spielen → die eigenen Fehler diagnostizieren → genau die drillen → nachmessen.**

Zwei Dokumente sind die Vorgabe und gehen diesem hier vor:

- `../chess-spec/teacher.md` — die Lehrmethode: die Fehlertaxonomie (50 Erkennungsregeln), die sechs
  Fertigkeitsdimensionen, der adaptive Einstufungstest, die Wiederholungsplanung, das Aufgabendesign.
- `../chess-spec/platform.md` — die Technik: Stockfish 17.1 als eigener UCI-Prozess mit dem kleinen Netz,
  Syzygy 3+4 Steine im Paket, `Disservin/chess-library` als Schachmodell, kein Fork von shakkikello.

## 1. Reihenfolge

Der Lernmodus läuft zuerst, die Lichess-Anbindung kommt danach (ausdrückliche Vorgabe). In Ausbaustufe 1
gibt es deshalb **keinen** Netzzugriff außer der optionalen Tablebase-Abfrage; `platform.md` §3 bleibt
unimplementiert, aber die Datenhaltung ist so gebaut, dass die eigenen Partien später von Lichess
dazukommen können.

Seit M8 gibt es die Lichess-Anbindung (`src/Lichess.*`, `src/GameSync.*`, `sailfish/LichessPage.qml`,
`sailfish/OnlineGamePage.qml`). Sie ist **freiwillig**: ohne Konto funktionieren Einstufungstest,
Wiederholung, Blunder-Check-Drill und Sparring unverändert und vollständig. Die härteste Vorgabe des
ganzen Projekts steht in `platform.md` §3.7 und ist als Codeeigenschaft gebaut, nicht als
UI-Konvention: **solange eine Lichess-Partie läuft, gibt es keine Engine und keine Tablebase.** Eine
einzige Instanzvariable `TeacherEngine::m_liveGameId` entscheidet das; ist sie gesetzt, beendet
`EngineProcess::setFairPlayLock(true)` den Engine-Prozess (nicht pausieren — beenden), jede Anfrage in
Engine und Tablebase wird abgelehnt, und `teacher.analysisAvailable` ist falsch, woran die Seiten den
Analyse-Eintrag **ausblenden** statt ihn auszugrauen. `tests/test_fairplay.cpp` prüft genau das;
`tests/test_lichess.cpp` prüft das Protokoll gegen aufgezeichnetes ndjson, ohne Netz und ohne Konto.
Analysiert wird **nach** der Partie — das ist erlaubt und ist die Stelle, an der gelernt wird.

## 2. Repository

```
harbour-schachlehrer/
├─ src/core/            Qt-frei, deterministisch, testbar
│   ├─ chess.hpp        vendored Disservin/chess-library (MIT), unverändert
│   ├─ Position.h/.cpp  dünne Hülle: FEN, Zugliste, SAN, PGN, Materialbilanz
│   ├─ Uci.h/.cpp       UCI-Textprotokoll: Kommandos bauen, info/bestmove parsen (ohne Prozess)
│   ├─ Taxonomy.h/.cpp  die Fehlerklassen aus teacher.md §2 und ihre Erkennungsregeln
│   ├─ WinProb.h/.cpp   Centibauern → Gewinnwahrscheinlichkeit (teacher.md §0.5)
│   ├─ Skill.h/.cpp     die sechs Dimensionen, Schätzung und Häufungsklassen (§3)
│   ├─ Placement.h/.cpp der adaptive Einstufungstest (§4), Elo/Rasch
│   ├─ Srs.h/.cpp       FSRS-Planung, Kartenlebenslauf (§5)
│   ├─ Routine.h/.cpp   „Was frage ich mich?" — die Denkroutine (§6.6, §7.5)
│   └─ Card.h/.cpp      eine Übungskarte: Stellung, Muster, Herkunft, Termin
├─ src/                 Qt-Schicht
│   ├─ EngineProcess.*  QProcess + UCI, ein Prozess, serielle Anfragen, Zeitbudget
│   ├─ Database.*       SQLite: Partien, Fehler, Karten, Messwerte
│   ├─ Analyser.*       Partie → Fehlerliste → Karten (die Diagnosestufe)
│   ├─ Sparring.*       der Gegner mit Fehlerbudget statt Elo-Deckel
│   └─ TeacherEngine.*  die einzige QML-Fassade
├─ sailfish/            Silica-UI, Style-Singleton wie in harbour-tarock
├─ third_party/         Stockfish-Quellen + der Small-Net-Patch, cburnett-SVG (BSD-3)
├─ assets/syzygy/       3+4 Steine, WDL und DTZ, 70 Dateien, 4,2 MB
├─ tests/               perft, UCI-Parser, Taxonomie, FSRS, Einstufung, Routine
└─ rpm/harbour-schachlehrer.spec
```

Lizenz GPL-3.0-or-later (Stockfish ist GPL-3.0; die App liefert ihn als getrenntes Programm aus und
kommuniziert über UCI, siehe `platform.md` §1.7).

## 3. Der Kern

Alles in `src/core/` ist Qt-frei, ohne I/O und ohne Uhr — dieselbe Regel wie im Tarock-Kern, aus demselben
Grund: so lässt es sich mit festen Eingaben prüfen.

```cpp
// Taxonomy.h — teacher.md §2
struct MoveFeatures {            // §2.2, aus zwei Engine-Analysen gebildet
    int  cpBefore, cpAfter;      // aus Sicht des Ziehenden, in Centibauern
    double wpBefore, wpAfter;    // dieselben Werte als Gewinnwahrscheinlichkeit
    bool  wasCapture, wasCheck, wasPromotion;
    bool  hangsPiece;            // die gezogene Figur steht danach ungedeckt am Schlag
    int   hangingValue;          // in Bauerneinheiten
    bool  missedThreat;          // die Drohung bestand schon vor dem Zug
    Motif refutationMotif;       // Gabel, Fesselung, Abzug, Spieß, Matt, …
    Phase phase;                 // Eröffnung, Mittelspiel, Endspiel (Materialgrenze)
    int   materialTotal;
    // …
};

enum class ErrorClass : std::uint8_t { /* 50 Klassen, teacher.md §2.6 */ };

struct Finding {
    ErrorClass  cls;
    Dimension   dimension;       // welche der sechs Fertigkeiten
    int         severity;        // aus dem Verlust an Gewinnwahrscheinlichkeit
    std::string fen, playedMove, bestMove;
    bool        makesCard;       // 44 der 50 Klassen erzeugen eine Übungskarte
};

std::vector<Finding> classify(const MoveFeatures&, int learnerElo);
const char* errorKey(ErrorClass);      // stabiler Schlüssel für die Texte
```

Die Schwellen sind **nach Spielstärke verschieden** (§2.3) und die Prüfreihenfolge ist festgelegt (§2.4) —
beides gehört in Tabellen, nicht in verstreute `if`-Ketten.

## 4. Die QML-Fassade

`TeacherEngine` ist das einzige QObject, das QML sieht (Kontexteigenschaft `teacher`).

```cpp
class TeacherEngine : public QObject {
    // --- Brett ---
    Q_PROPERTY(QString fen READ fen NOTIFY positionChanged)
    Q_PROPERTY(QVariantList squares READ squares NOTIFY positionChanged)   // 64 Felder
    Q_PROPERTY(QVariantList legalTargets READ legalTargets NOTIFY selectionChanged)
    Q_PROPERTY(int selectedSquare READ selectedSquare WRITE setSelectedSquare NOTIFY selectionChanged)
    Q_PROPERTY(bool flipped READ flipped WRITE setFlipped NOTIFY boardChanged)
    Q_PROPERTY(QVariantList moveList READ moveList NOTIFY positionChanged)
    Q_PROPERTY(QString lastMove READ lastMove NOTIFY positionChanged)
    Q_PROPERTY(bool whiteToMove READ whiteToMove NOTIFY positionChanged)
    Q_PROPERTY(QString gameResult READ gameResult NOTIFY positionChanged)

    // --- Modus ---
    Q_PROPERTY(int mode READ mode NOTIFY modeChanged)   // Idle|Placement|Drill|Sparring|Review
    Q_PROPERTY(QString prompt READ prompt NOTIFY taskChanged)       // die Aufgabenstellung im Klartext
    Q_PROPERTY(QVariantMap task READ task NOTIFY taskChanged)
    Q_PROPERTY(QVariantMap feedback READ feedback NOTIFY feedbackChanged)  // immer ein Satz, nie eine Zahl
    Q_PROPERTY(bool engineReady READ engineReady NOTIFY engineChanged)
    Q_PROPERTY(bool thinking READ thinking NOTIFY engineChanged)

    // --- Fortschritt ---
    Q_PROPERTY(QVariantList skills READ skills NOTIFY progressChanged)     // sechs Dimensionen
    Q_PROPERTY(QVariantMap session READ session NOTIFY progressChanged)    // die drei Tagesblöcke
    Q_PROPERTY(int dueCards READ dueCards NOTIFY progressChanged)

    Q_INVOKABLE void startPlacement();
    Q_INVOKABLE void startSession();      // fällige Wiederholungen, ein neues Muster, Sparring
    Q_INVOKABLE void startSparring(int handicap);
    Q_INVOKABLE bool play(int fromSquare, int toSquare, const QString& promotion = QString());
    Q_INVOKABLE void takeBack();          // im Sparring frei, mit Erklärung
    Q_INVOKABLE void requestHint();
    Q_INVOKABLE void skipTask();
    Q_INVOKABLE void analyseCurrentGame();
    Q_INVOKABLE QVariantList lastFindings() const;
};
```

### 4.1 Die Denkroutine

```cpp
Q_PROPERTY(QVariantList routine READ routine NOTIFY routineChanged)
Q_PROPERTY(QVariantMap blunderCheck READ blunderCheck NOTIFY blunderCheckChanged)
Q_PROPERTY(int drillMode READ drillMode WRITE setDrillMode NOTIFY drillModeChanged)
Q_INVOKABLE void answerBlunderCheck(const QVariantList& dangerous);
Q_INVOKABLE void skipBlunderCheck();
```

`src/core/Routine.{h,cpp}` hält den Fragenkatalog: zehn Fragen, aus den
übertragbaren Hinweisen von `teacher.md` §6.6 gewonnen (wo §6.6 keinen Hinweis
hat — Gruppen H und I —, ist die Frage ergänzt und als solche markiert), jede
mit stabiler Kennung, Auslösezeitpunkt, Fertigkeitsdimension und der Liste der
Fehlerklassen, die sie gefangen hätte. Die Abbildung Klasse → Frage ist total
(`tests/test_routine.cpp`).

Reihenfolge und Sichtbarkeit kommen aus dem eigenen Fehlerbestand
(`Database::findingTallies()`, Fenster wie §3.2 b): was der Lernende wirklich
falsch macht, steht oben; was lange nicht mehr vorkam, verblasst und
verschwindet — dieselbe Ausblendung wie beim Blunder-Check-Drill (§7.5), auf
die ganze Liste angewandt. `feedback["question"]` nennt nach einem Fehler die
Frage, die ihn gefunden hätte („Frage 2 hätte das gefunden: …"); der Satz nach
§6.6 bleibt dabei unverändert stehen.

Der Drill selbst (§7.5) hängt in `TeacherEngine::play()`: im Sparring hält die
App den Zug vor der Freigabe an, zeigt die Schachs und Schlagzüge des Gegners
(SAN, keine Zahl), lässt antippen, welche gefährlich sind, und gibt den Zug
danach frei. Der Takt (jeder 3. → 5. → 8. → aus, zurück bei A1, B1 oder C2)
liegt als `core::BlunderCheckSchedule` im Qt-freien Kern, damit er prüfbar ist;
`Sparring` benutzt ihn nur.

**Grundsatz:** keine Bewertungszahl ohne Satz. `feedback` enthält immer `text`, optional `cp`/`wp`, und
einen Schlüssel für die Regel, aus der der Satz stammt.

## 5. Engine

Ein `QProcess` auf `/usr/share/harbour-schachlehrer/bin/stockfish`, `setoption name SyzygyPath value
/usr/share/harbour-schachlehrer/syzygy`, ein Thread, `Hash 16`. Anfragen strikt seriell mit Zeitbudget
(`go movetime`), **nie** `go infinite`. Zwei Betriebsarten:

- **Sparring** — 50–250 ms je Zug, plus das Fehlerbudget aus `teacher.md` §7: die Engine wählt bewusst den
  n-besten Zug, wenn der Lernende gerade diese Fehlerart üben soll.
- **Analyse** — nach der Partie, je Zug ein festes Budget, im Hintergrund, mit Fortschrittsanzeige.

Fällt das Binary aus (Sailjail, fehlende Datei), meldet die App das im Klartext und bleibt bedienbar:
Brett, Wiederholungen und Regeln funktionieren ohne Engine, nur Sparring und Analyse nicht.

## 6. Datenhaltung

SQLite unter `~/.local/share/harbour-schachlehrer/`: `games` (PGN, Datum, Gegner, Ergebnis), `findings`
(Fehlerklasse, FEN, Zug, Dimension, Schwere), `cards` (Muster, FEN, Lösung, Herkunft, FSRS-Zustand),
`reviews` (jede Antwort mit Zeit und Bewertung), `skills` (die sechs Schätzungen über die Zeit).

## 7. Oberfläche

Startseite mit der heutigen Sitzung, dem Fortschritt der sechs Dimensionen und dem Weg zum Einstufungstest.
Dann: Brettseite (Sparring und Drill teilen sich dasselbe Brett), Analyse einer Partie mit der Fehlerliste,
die Kartenübersicht, Regeln und Glossar, Einstellungen.

Das Brett ist ein `GridView` mit 64 Feldern, Figuren als SVG (cburnett, BSD-3-Clause), Drehung über
`layoutDirection`, wie es shakkikello vormacht. Portrait, Zugeingabe per Antippen (Feld → Feld).

## 8. Meilensteine

| M | Inhalt | Abnahme |
|---|---|---|
| **M0** | Gerüst, CMake, Spec, Sailjail, leeres Brett, Stockfish im Paket | RPM baut und startet, Engine meldet sich |
| **M1** | Schachmodell, Brett, Zugeingabe, Partie gegen die Engine, PGN | perft grün, eine Partie spielbar |
| **M2** | Analyse: `MoveFeatures`, die 50 Erkennungsregeln, Fehlerliste nach der Partie | Taxonomietests grün |
| **M3** | Einstufungstest: Aufgabenbestand, Elo/Rasch-Schätzung, Trainingsplan | 25 Aufgaben, sechs Schätzwerte |
| **M4** | Wiederholung: FSRS, Kartenerzeugung aus eigenen Fehlern, die Tagessitzung | Kartenlebenslauf getestet |
| **M5** | Sparring mit Fehlerbudget, Rücknahme mit Erklärung, Blunder-Check-Drill | spielbar, Erklärungen im Klartext |
| **M6** | Endspiele mit Tablebase-Feedback, die Grundmatts, Opposition, Lucena/Philidor | jeder Zug exakt benotet |
| **M7** | Inhalte: Muster, Regelwerk, Glossar, Fortschrittsanzeige | vollständig auf Deutsch |
| **M8** | Lichess (`platform.md` §3) | erst nach M7 — erledigt: Board API, PKCE, Partieablauf, Partiedownload, Fair-Play-Sperre mit `tests/test_fairplay.cpp` |
