# harbour-schachlehrer

Eine Schach-Lern-App für Sailfish OS. Sie folgt keinem festen Lehrplan, sondern
einer Schleife: **messen → spielen → die eigenen Fehler diagnostizieren → genau
die drillen → nachmessen.**

Die Architektur steht in [`docs/design.md`](docs/design.md); die Lehrmethode und
die Technik in `../chess-spec/teacher.md` und `../chess-spec/platform.md`.

## Was im Paket steckt

| | Pfad im Paket |
|---|---|
| Stockfish 17.1 (eigener UCI-Prozess) | `/usr/share/harbour-schachlehrer/bin/stockfish` |
| Syzygy 3+4 Steine, WDL und DTZ, 70 Dateien | `/usr/share/harbour-schachlehrer/syzygy/` |
| Silica-QML | `/usr/share/harbour-schachlehrer/qml/` |
| cburnett-Figuren (BSD-3-Clause) | `/usr/share/harbour-schachlehrer/assets/pieces/cburnett/` |

Kein Pflichtdownload, keine Netzverbindung für die Grundfunktion. Fehlt die
Engine, sagt die App das im Klartext und bleibt bedienbar: Brett, Karten und
Regeln funktionieren ohne sie.

## Bauen

Die Engine wird **außerhalb** dieses Baums cross-gebaut (platform.md §8.3) und
als `third_party/prebuilt/<arch>/stockfish` abgelegt; ohne sie bricht der
RPM-Bau mit einer Erklärung ab.

```sh
HOST=$(sh ../nfsshift-sfos/tools/buildhost.sh)
tar -C .. -cf - --exclude=build --exclude=.git harbour-schachlehrer |
  ssh "$HOST" "docker exec -i sfossdk52 tar -xf - -C /home/mersdk"
ssh "$HOST" "docker exec sfossdk52 bash -lc '
  cd ~/harbour-schachlehrer &&
  nice -n 15 mb2 -t SailfishOS-5.2.0.15-aarch64 build'"
```

Die Übersetzungen entstehen mit `lupdate sailfish -ts translations/*.ts`.

## Lizenz

GPL-3.0-or-later. Herkunft und Lizenz jedes mitgelieferten Fremdbestandteils
stehen in [`CREDITS/ASSETS.md`](CREDITS/ASSETS.md).
