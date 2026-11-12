# Eigene RD-Geometrieprobe

`rectangle.rd` ist eine selbst erzeugte binäre Importprobe, kein freigegebener Geräteauftrag. Sie wird ausschließlich gelesen, niemals ausgeführt oder übertragen.

Dekodierte Folge: D8/12, F0, 88 (10000,20000), A8 (30000,20000), A8 (30000,30000), A8 (10000,30000), A8 (10000,20000), EB, E7/00, D7. Absolute Koordinaten bestehen aus fünf 7-Bit-Gruppen, höchstwertige zuerst, in Mikrometern. Bei Magic 0x88 werden Bit 0 und 7 getauscht, dann mit 0x88 XOR-verknüpft und eins modulo 256 addiert.

Nach Umkehr der nativen Y-Richtung und gemeinsamer Platzierung links unten: (0,10), (20,10), (20,0), (0,0), (0,10) mm; 60 mm Kontur, ein Teilpfad.

Primäre Protokollforschung: [Befehlstabelle](https://github.com/kkaempf/ruida/blob/1503ba9cf6fd3d73a8a2b4eb64cd06f331e6a49b/doc/commands.md) und [256 Byteabbildungen](https://github.com/kkaempf/ruida/blob/1503ba9cf6fd3d73a8a2b4eb64cd06f331e6a49b/doc/lookuptable_88.md). Die Prosa der Befehlstabelle enthält einen Fehler bei der absoluten Zahlenbasis: Aus sieben Nutzbits folgt Basis 128, nicht 127. Die unabhängige reale Quadratprobe bestätigt 00 00 03 06 50 = 50000 µm. Es wurden keine Parserquellen oder fremden Grafiken übernommen.
