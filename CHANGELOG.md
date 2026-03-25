# Changelog

Alle nennenswerten Aenderungen an `PrivateClaw` sollten in dieser Datei dokumentiert werden.

Das Release-Workflow auf GitHub sucht hier automatisch nach einem Abschnitt fuer den jeweiligen Tag und verwendet ihn als Release-Notizen.
Unterstuetzte Ueberschriften sind zum Beispiel:

- `## [v0.1.0] - 2026-03-24`
- `## [0.1.0] - 2026-03-24`
- `## v0.1.0 - 2026-03-24`
- `## 0.1.0 - 2026-03-24`

Wenn kein passender Abschnitt gefunden wird, faellt der Workflow automatisch auf GitHubs generierte Release-Notes zurueck.

## [v0.1.2]

### Added

- System-Tray mit GitHub-Update-Check, blinkendem Update-Hinweis und Direktsprung zur Release-Seite per Doppelklick
- Template-Bibliothek im Workflow-Editor mit importierbaren Vorlagen fuer Status, Ingest, Memory-Pflege, Zeitplaene und ComfyUI
- Verschluesselte Projekt-Secrets mit Nutzung ueber `{{secret.name}}` in Workflow-Prompts und Tool-Konfigurationen
- Workflow-Debugger-MVP mit Debug-Ausfuehrung, Schritt-Trace, Variablen-Snapshots und Detailansicht im Workflow-Editor
- Live-Streaming fuer Prompt-Schritte im Run-Protokoll bei manuellen und geplanten Workflow-Laeufen, inklusive sichtbarer modellseitiger Reasoning-Bloecke waehrend der Ausgabe

### Changed

- Die Entwicklungs-Version der App ist jetzt als `0.1.1.1` im Programm hinterlegt und wird fuer Update-Vergleiche gegen GitHub-Releases verwendet
- Beim Schliessen laeuft `PrivateClaw` bei verfuegbarem System-Tray jetzt im Hintergrund weiter, damit Zeitplaene aktiv bleiben
- Projekt-Memory priorisiert jetzt angepinnte Eintraege und verdichtet Restkontext fuer Workflow-Runs automatisch
- Projekte haben jetzt Sicherheitsrichtlinien fuer `shell.run`, `file.edit_diff` und `http.request`, inklusive optionaler Freigabe fuer unbeaufsichtigte Zeitplaene
- Neue Workflows fragen jetzt zwischen leerem Start und Template-Auswahl, und die Template-Bibliothek laesst sich platzsparend ein- und ausklappen
- Die finale Prompt-Ausgabe bleibt weiterhin von `<think>`- und aehnlichen Reasoning-Tags bereinigt, waehrend die Roh-Ausgabe live im Run-Protokoll sichtbar wird

### Fixed

- Der rechte Projekt-Editor ist jetzt scrollbar, damit Sicherheitsrichtlinien und Secret-Verwaltung nicht mehr mit der Projektansicht ueberlappen
- Das Beenden ueber das Tray-Menue faehrt Fenster, Tray und Hintergrund-Timer jetzt sauberer herunter, damit keine haengenden Instanzen zurueckbleiben

## [v0.1.1] - 2026-03-24

### Added

- Persistente Run-Historie mit eigener `Runs`-Ansicht fuer manuelle und geplante Workflow-Laeufe
- Globale Dateipfad-Allowlist fuer Datei-Tools mit Pflege direkt in der App

### Changed

- Provider-Endpunkte fuer `Ollama` und `LM Studio` sind jetzt pro Projekt konfigurierbar statt fest verdrahtet
- `ComfyUI` kann pro Workflow-Tool-Schritt ueber eine eigene `base_url` angesprochen werden, inklusive passender Metadaten-Abfrage
- Datei-Tools koennen jetzt auch ausserhalb des Workspace arbeiten, wenn ein Pfad explizit zur Allowlist freigegeben wurde
- Run-Kontexte laden Projekt-Memory jetzt als Mischform aus direkten Eintraegen plus komprimierter Rest-Zusammenfassung statt nur einer festen Liste von 6 Eintraegen

### Fixed

- Zeitplan-Laeufe koennen die Run-Historie wieder korrekt starten, auch wenn zu Beginn noch keine `output_text`-Ausgabe vorliegt
- Die Dateipfad-Allowlist in der Projektansicht aktualisiert sich jetzt nach neuen Freigaben aus Workflows und kann zusaetzlich manuell neu geladen werden
- Manuell gestartete Zeitplaene schreiben beim Run-Start keine `NULL`-Werte mehr in `runs.output_text`
- Neue Runs speichern jetzt bereits beim Start erste Log-Zeilen in die Run-Historie, und haengen gebliebene `running`-Runs werden beim App-Start als `interrupted` markiert

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
