Name:    harbour-schachlehrer
Version: 0.2.1
Release: 1
Summary: Chess coach for Sailfish OS: measure, play, diagnose, drill
License: GPL-3.0-or-later
URL:     https://github.com/smatkovi/harbour-schachlehrer
Source0: %{name}-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-root

# aarch64 and armv7hl. The engine binary below is architecture-dependent and is
# taken prebuilt from the tree; everything else is not.
ExclusiveArch:  aarch64 %{arm}

Requires:       sailfishsilica-qt5
Requires:       qt5-plugin-sqldriver-sqlite
BuildRequires:  pkgconfig(sailfishapp)
# The Lichess token goes into the Sailfish Secrets store (platform.md §3.3).
# The build works without it — the app then uses the 0600 file fallback — but
# the package is meant to have it, so the dependency is declared rather than
# left to chance.
BuildRequires:  pkgconfig(sailfishsecrets)
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Network)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(Qt5Sql)
BuildRequires:  qt5-qttools-linguist
BuildRequires:  cmake

%description
Schachlehrer is a chess coach for Sailfish OS. It does not follow a fixed
syllabus; it runs a loop: measure what you can do, let you play, diagnose the
mistakes you actually made, drill exactly those, then measure again.

A local Stockfish engine and the Syzygy tablebases for three and four men are
part of the package, so the app is complete offline from the first start. Your
own games become the exercises: every mistake is classified, the classes are
aggregated into six skill dimensions, and spaced repetition schedules what you
see next. Every evaluation is given as a sentence; the number is never the
answer on its own.

%if 0%{?_chum}
Title: Schachlehrer
Type: desktop-application
DeveloperName: Sebastian Matkovich
Categories:
 - Game
 - Education
Custom:
  Repo: https://github.com/smatkovi/harbour-schachlehrer
Links:
  Homepage: https://github.com/smatkovi/harbour-schachlehrer
  Bugtracker: https://github.com/smatkovi/harbour-schachlehrer/issues
%endif

%prep
%setup -q

%build
# Stockfish 17.1 is cross-built out of tree (chess-spec/platform.md §1.1, §8.3)
# with -DNNUE_EMBEDDING_OFF and the small-net-only patch, stripped, and copied
# to third_party/prebuilt/<arch>/stockfish. It is architecture-dependent, so
# the RPM build picks the directory that matches its own target and refuses to
# produce a package without it: an RPM whose engine is missing would install
# fine and then be a permanently crippled app on the device.
#
# Two spellings are accepted, because the two names for the same target differ:
# %%{_arch} is "aarch64" and "arm", while the cross-build and the SDK target
# call the 32-bit one "armv7hl" (= %%{_target_cpu}). Whichever directory holds
# the binary wins.
engine_arch=
for a in %{_arch} %{_target_cpu}; do
    if [ -f "third_party/prebuilt/$a/stockfish" ]; then
        engine_arch=$a
        break
    fi
done

if [ -z "$engine_arch" ]; then
    echo "" >&2
    echo "=============================================================" >&2
    echo " %{name}: no engine for %{_arch} (%{_target_cpu})." >&2
    echo "" >&2
    echo " Expected: third_party/prebuilt/%{_arch}/stockfish" >&2
    [ "%{_arch}" = "%{_target_cpu}" ] || \
        echo "       or: third_party/prebuilt/%{_target_cpu}/stockfish" >&2
    echo "" >&2
    echo " Cross-build it in the Sailfish SDK container as described in" >&2
    echo " chess-spec/platform.md §8.3, strip it, and copy it there:" >&2
    echo "" >&2
    echo "   make -C Stockfish/src -j4 ARCH=armv8 COMP=gcc \\" >&2
    echo "        EXTRACXXFLAGS=-DNNUE_EMBEDDING_OFF build   # aarch64" >&2
    echo "   make -C Stockfish/src -j4 ARCH=armv7-neon COMP=gcc \\" >&2
    echo "        EXTRACXXFLAGS=-DNNUE_EMBEDDING_OFF build   # armv7hl" >&2
    echo "   strip -s Stockfish/src/stockfish" >&2
    echo "=============================================================" >&2
    echo "" >&2
    exit 1
fi
chmod 0755 "third_party/prebuilt/$engine_arch/stockfish"

# The engine is built with -DNNUE_EMBEDDING_OFF, so the small net is a separate
# file and the engine is useless without it (platform.md §1.4). Same reasoning
# as above: better no package than a package that installs and cannot evaluate.
if [ -z "$(ls assets/*.nnue third_party/prebuilt/net/*.nnue 2>/dev/null)" ]; then
    echo "" >&2
    echo " %{name}: no NNUE net in assets/." >&2
    echo " The engine is built with -DNNUE_EMBEDDING_OFF and needs" >&2
    echo " nn-37f18f62d772.nnue (3,36 MiB) beside it. tools/fetch-engine.sh" >&2
    echo " downloads it and checks its hash." >&2
    echo "" >&2
    exit 1
fi

# Syzygy 3+4 men, WDL and DTZ, 70 files (platform.md §2, Empfehlung 2). Ship
# them or say why not: rpmbuild --without tablebases builds a package without.
%if 0%{!?_without_tablebases:1}
syzygy_count=$(ls assets/syzygy/*.rtbw assets/syzygy/*.rtbz 2>/dev/null | wc -l)
if [ "$syzygy_count" -ne 70 ]; then
    echo "" >&2
    echo " %{name}: assets/syzygy holds $syzygy_count files, expected 70" >&2
    echo " (35 .rtbw + 35 .rtbz, 4 346 080 bytes, chess-spec/platform.md §2.1)." >&2
    echo " Fetch them with the loop in platform.md §2, Empfehlung 2, or build" >&2
    echo " without endgame tablebases: rpmbuild --without tablebases" >&2
    echo "" >&2
    exit 1
fi
%endif

mkdir -p build
cd build
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=%{_prefix} \
    -DSCHACH_ENGINE_ARCH="$engine_arch" \
    -DSCHACH_REQUIRE_ENGINE=ON
make %{?_smp_mflags}

%install
rm -rf %{buildroot}
make -C build install DESTDIR=%{buildroot}

# Directories the app opens by path even when they are still empty: the
# optional big NNUE net (platform.md §6.2) and the translation catalogues.
mkdir -p %{buildroot}%{_datadir}/%{name}/net
mkdir -p %{buildroot}%{_datadir}/%{name}/translations

# cp/install out of a working tree keeps group-writable bits; the package wants
# plain 755 directories and 644 files (rpmlint non-standard-dir-perm). The
# engine gets its executable bit back right after.
find %{buildroot}%{_datadir}/%{name} -type d -exec chmod 0755 {} \;
find %{buildroot}%{_datadir}/%{name} -type f -exec chmod 0644 {} \;
chmod 0755 %{buildroot}%{_datadir}/%{name}/bin/stockfish

# rpmlint says arch-dependent-file-in-usr-share about the engine, and it is
# right that this is unusual. It is deliberate: SailfishApp::pathTo() resolves
# against /usr/share/%{name}, the app looks the engine up there, and the whole
# directory is the app's own (platform.md §5.3). The fallback if a future
# Sailjail release ever forbids exec from /usr/share is %{_bindir}/%{name}-engine.

# rpmbuild does not strip for us here; the app binary is ours to strip, the
# engine came stripped out of the cross-build.
strip %{buildroot}%{_bindir}/%{name} || :

# GPL duty: the licence of the app and the provenance of everything third
# party that ships with it (Stockfish GPL-3.0, chess-library MIT, cburnett
# BSD-3-Clause, Syzygy public domain). platform.md §1.7.
mkdir -p %{buildroot}%{_datadir}/licenses/%{name}
for f in LICENSE COPYING; do
    [ -f "$f" ] && install -m 644 "$f" %{buildroot}%{_datadir}/licenses/%{name}/
done
mkdir -p %{buildroot}%{_datadir}/doc/%{name}
for f in CREDITS/ASSETS.md CREDITS/CODE.md CREDITS/ENGINE.md CREDITS/TABLEBASES.md README.md docs/design.md; do
    [ -f "$f" ] && install -m 644 "$f" %{buildroot}%{_datadir}/doc/%{name}/
done
exit 0

%files
%defattr(-,root,root,-)
%{_bindir}/%{name}
%attr(0755,root,root) %{_bindir}/%{name}-engine

%dir %{_datadir}/%{name}
%dir %{_datadir}/%{name}/qml
%{_datadir}/%{name}/qml/*.qml
%{_datadir}/%{name}/qml/qmldir
%{_datadir}/%{name}/qml/icons
%{_datadir}/%{name}/assets
%{_datadir}/%{name}/items
%{_datadir}/%{name}/net
%{_datadir}/%{name}/translations

# The engine: a separate UCI program, executable, started with QProcess
# (platform.md §1.9). Not a library, not linked in.
%dir %{_datadir}/%{name}/bin
%attr(0755,root,root) %{_datadir}/%{name}/bin/stockfish

# The tablebases stay plain read-only data.
%if 0%{!?_without_tablebases:1}
%dir %{_datadir}/%{name}/syzygy
%{_datadir}/%{name}/syzygy/*.rtbw
%{_datadir}/%{name}/syzygy/*.rtbz
%endif

%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
%{_datadir}/doc/%{name}
%{_datadir}/licenses/%{name}

%changelog
* Wed Sep 24 2026 smatkovi - 0.2.1-1
- Die Sparringpartie überlebt das Schließen der App. Sie wird nach jedem Zug
  weggeschrieben und nicht erst beim Beenden — ein Programm, das der
  Taskmanager abschießt, bekommt kein aboutToQuit mehr zu sehen. Auf der
  Startseite steht dann "Partie fortsetzen"; der zweite Knopf heißt so lange
  "Neue Partie", damit niemand die alte aus Versehen wegwirft.
- Beim Üben bleibt eine falsch beantwortete Aufgabe stehen, bis man weitergeht.
  Bisher lud sofort die nächste, und "Lösung ansehen" zeigte deren Lösung — die
  Aufgabe, an der man gerade gescheitert war, war nicht mehr zu sehen, und die
  frische wurde nebenbei als ungelöst verbucht. Jetzt ist die Lösung die der
  verlorenen Aufgabe, sie kostet nichts mehr, und danach geht es mit "Weiter".
* Fri Sep 18 2026 smatkovi - 0.2.0-1
- Aufgaben gehen über einen Zug hinaus: die ganze Folge wird eingegeben,
  die Gegnerzüge eingeschlossen. Nur die erste Begegnung mit einem Muster
  zieht noch für den Gegner (teacher.md §6.5, §6.1).
- Der Einstufungstest wiederholt sich nicht mehr: die Aufgaben werden im
  Umkreis der Zielschwierigkeit gelost statt genommen, bereits gesehene
  werden gemieden, und ein erneuter Test beginnt bei der letzten Messung
  statt wieder bei 1000.
- Wer eine Aufgabe nicht löst, bekommt keine schwerere mehr. Die Auswahl
  greift jetzt nach unten und nicht nach oben, wenn nichts Passendes da ist,
  und die Schätzung läuft nicht mehr aus dem Bereich, den die App abdeckt.
- Die Schätzung rechnet mit der Schwierigkeit der Aufgabe, die tatsächlich
  gestellt wurde, statt mit der, die gesucht war.
- 3 944 mehrzügige Aufgaben aus der Lichess-Aufgabendatenbank (CC0) liegen
  bei; sie bringen die Quote stiller Züge und Verteidigungszüge aus §6.3
  erstmals auf die geforderten Anteile.
- Auf Wunsch holt die App beim Start neue Aufgaben über die offene
  Lichess-Schnittstelle nach und bewahrt sie für offline auf. Standardmäßig
  aus; ohne Netz, Konto und Erlaubnis ändert sich nichts (§0.2).
- Die Lösung lässt sich ansehen: Halbzug für Halbzug auf dem Brett, vor und
  zurück, mit dem Satz dazu. Auch bei den Übungen, nicht nur im Test — jede
  beantwortete Aufgabe bleibt erreichbar. Wer die Lösung einer noch offenen
  Aufgabe ansieht, gibt sie damit auf; der Knopf sagt es vorher.
- Ein zweiter Einstufungstest fängt mit einer leeren Durchsicht an statt die
  Aufgaben des letzten mitzuzählen, und das Brett kommt aus der Durchsicht
  richtig herum zurück.
- Der Lichess-Zugangsschlüssel liegt im verschlüsselten Schlüsselspeicher
  von Sailfish OS; ein vorhandener aus der Datei wird einmalig übernommen.
  Antwortet der Speicher nicht, sagt die Einstellungsseite das.
* Sun Sep 13 2026 smatkovi - 0.1.1-1
- The engine evaluates again: both network options point at the small net,
  and an engine error after the handshake reaches the screen.
- Online play has a way out of every state; the live board says why it is
  not taking a move.
- Starter cards are stored before the first answer, so a solved task is
  scheduled; the placement test gives no hints.
* Sat Sep 12 2026 smatkovi - 0.1.0-1
- M0: package skeleton. Board, cards and rules work without the engine; the
  engine binary and the Syzygy 3+4 tablebases ship inside the package.
