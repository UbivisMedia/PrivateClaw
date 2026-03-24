# Changelog

Alle nennenswerten Aenderungen an `PrivateClaw` sollten in dieser Datei dokumentiert werden.

Das Release-Workflow auf GitHub sucht hier automatisch nach einem Abschnitt fuer den jeweiligen Tag und verwendet ihn als Release-Notizen.
Unterstuetzte Ueberschriften sind zum Beispiel:

- `## [v0.1.0] - 2026-03-24`
- `## [0.1.0] - 2026-03-24`
- `## v0.1.0 - 2026-03-24`
- `## 0.1.0 - 2026-03-24`

Wenn kein passender Abschnitt gefunden wird, faellt der Workflow automatisch auf GitHubs generierte Release-Notes zurueck.

## [Unreleased]

### Added

- Initiales Changelog-Template fuer GitHub Releases.

## [v0.1.0] - 2026-03-24

### Added

- Projektverwaltung mit Provider- und Modellwahl fuer `Ollama` und `LM Studio`
- Visueller Workflow-Editor mit JSON-Synchronisation
- Persistente Projekterinnerung auf Basis von `SQLite`
- Workflow-Schritte fuer `prompt`, `decision`, `save_memory` und `tool`
- Tool-Layer fuer Datei-, Verzeichnis-, Memory-, HTTP-, CSV-, JSON- und `ComfyUI`-Integrationen
- Zeitplaene fuer einmalige, intervallbasierte und taegliche Workflow-Ausfuehrung
- GitHub Actions fuer Windows-Pakete und GitHub Releases

### Changed

- Release-Notizen koennen jetzt automatisch aus dieser Datei gelesen werden.
