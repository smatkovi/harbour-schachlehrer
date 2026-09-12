# Herkunft der mitgelieferten Fremdbestandteile

## Stockfish 17.1
Quelle: https://github.com/official-stockfish/Stockfish (Tag `sf_17.1`), GPL-3.0-only.
Gebaut von `tools/fetch-engine.sh` mit dem Small-Net-only-Patch (siehe dort und
`../chess-spec/platform.md` §1.4). Die App ruft Stockfish als eigenständiges Programm über UCI auf.
Der vollständige Quelltext samt Patch ist über dasselbe Skript reproduzierbar.

## NNUE-Netz `nn-37f18f62d772.nnue`
Quelle: https://tests.stockfishchess.org/api/nn/nn-37f18f62d772.nnue, Teil des Stockfish-Projekts,
unter denselben Bedingungen. 3,36 MiB.

## Syzygy-Endspieldatenbanken, 3 und 4 Steine
Quelle: https://tablebase.lichess.ovh/tables/standard/, erzeugt von Ronald de Man, gemeinfrei.
70 Dateien, 4 346 080 Bytes.
