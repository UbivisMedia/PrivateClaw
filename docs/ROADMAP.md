# Roadmap-Vorschlaege fuer PrivateClaw

Stand: auf Basis von `0.1.2.2` und des aktuellen `unreleased`-Stands.

Diese Liste ist bewusst als Planungshilfe gedacht und nicht als verbindliche Zusage. Die Versionsvorschlaege orientieren sich an Aufwand, Risiko und Nutzen fuer reale Workflow-Projekte.

## Empfohlene Einplanung

### 0.1.2.3

Fokus: schnelles Hardening nach den letzten Workflow- und LM-Studio-Erweiterungen.

- Ausgabe-Validierung vor Persistenz
  - `save_memory`, Datei-Export und Folge-Schritte sollten optional pruefen koennen, ob eine Ausgabe leer, Meta-verseucht oder strukturell unbrauchbar ist.
  - Hoher Nutzen, kleiner Eingriff.

- Leere Entity-Memories automatisch verwerfen
  - Beispielsweise keine Orte/Ereignisse speichern, wenn alle relevanten Felder leer sind.
  - Verhindert verschmutzten Story-Kontext direkt an der Quelle.

- Sichtbare Warnungen bei gekuerztem oder bereinigtem Modell-Output
  - Im Run-Log und Debugger sollte klar stehen, wenn Reasoning entfernt, JSON repariert oder Kontext aggressiv gekuerzt wurde.
  - Hilft beim Debuggen ohne Datenbankblick.

- Schnellaktion fuer kontaminierte Runs
  - Ein Knopf in der Run-Ansicht, um Artefakt-Datei und zugehoerige Szene-Memories eines Runs gezielt zu bereinigen oder zu loeschen.
  - Passt gut zum bereits vorhandenen manuellen Abbruch.

### 0.1.3

Fokus: Workflow-Qualitaet, Debugging und Alltagstauglichkeit fuer Autoren und Automatisierungsprojekte.

- Schritt erneut ausfuehren / ab fehlendem Schritt fortsetzen
  - Im Debugger oder in der Run-Historie einzelne Prompt- oder Tool-Schritte neu anstossen.
  - Spart Zeit bei langen Workflows.

- Prompt-Preview mit final aufgeloesten Variablen
  - Vor dem Start oder im Debugger sollte sichtbar sein, welcher Prompt wirklich an das Modell geht.
  - Besonders wertvoll bei Memory-lastigen Workflows.

- Regelbasierte Schritt-Validatoren
  - Beispiele: `must_be_json`, `must_not_contain_meta`, `min_chars`, `required_keys`.
  - Macht Workflows robuster, ohne fuer alles Sonderlogik zu bauen.

- Bessere Memory-Ansicht
  - Filter nach `source`, `tags`, `entry_type`, `pinned` und Volltext.
  - Dazu eine kompakte Vorschau, welche Memories in den naechsten Run einfliessen.

- Run-Vergleich
  - Zwei Runs direkt vergleichen: Ausgabe, Logs, persistierte Memories, verwendetes Modell.
  - Sehr hilfreich beim Prompt-Tuning.

### 0.1.4

Fokus: Datenpflege, Sicherheit und weniger manuelle Rettungsarbeit.

- Backup- und Restore-UI fuer Projektdatenbank
  - Export und Import fuer Projekte, Workflows, Memories, Runs und Variablen.
  - Wichtig, sobald produktive Projekte damit laufen.

- Memory-Deduplizierung und Merge-Vorschlaege
  - Aehnliche oder identische Eintraege erkennen und zusammenfassen.
  - Verhindert, dass Kontexte langsam verrauschen.

- Automatische Projekt-Hygiene-Workflows
  - Vorlagen fuer Archivierung alter Runs, Komprimierung alter Memories und Aufraeumen leerer Eintraege.
  - Gute Ergaenzung zum bestehenden Scheduler.

- Desktop-Benachrichtigungen fuer abgeschlossene oder fehlgeschlagene Runs
  - Optional mit Schnellzugriff auf Logs.
  - Besonders praktisch fuer Hintergrund- und Zeitplan-Laeufe.

### 0.2.0

Fokus: groessere Produktivitaets-Spruenge mit mittlerem Architektur-Eingriff.

- Wiederverwendbare Subworkflows
  - Workflows als aufrufbare Bausteine mit Ein- und Ausgaben.
  - Macht grosse Pipelines deutlich wartbarer.

- Workflow-Versionierung mit Diff und Rollback
  - Aenderungen am Workflow nachvollziehen, alte Stande wiederherstellen.
  - Sehr wertvoll fuer Experimentierphasen.

- Provider-Faehigkeiten automatisch erkennen
  - Kontextfenster, JSON-Modus, Streaming, Reasoning-Support, sinnvolle Defaults.
  - Weniger manuelle Konfiguration pro Projekt.

- Kontextbudgetierung mit echter Token-Schaetzung
  - Statt nur heuristisch kuerzen sollten Prompts, Memories und Tool-Ergebnisse tokenbewusst budgetiert werden.
  - Reduziert Kontextfehler spuerbar.

- Memory-Links und Story-Beziehungen
  - Figuren, Orte und Ereignisse miteinander verknuepfen.
  - Macht spaetere Suche, Verdichtung und Story-Konsistenz deutlich staerker.

### 0.3.0

Fokus: Erweiterbarkeit und groessere, modulare Projekte.

- Plugin- oder Skript-SDK
  - Eigene Tools, Provider oder Schritt-Typen ohne Eingriff in den Kern.
  - Der groesste Hebel fuer langfristige Erweiterbarkeit.

- Projektpakete zum Teilen
  - Export eines Projekts inklusive Workflows, Templates, Variablen und optionaler Memories.
  - Erleichtert Community-Vorlagen und reproduzierbare Setups.

- Queue- und Orchestrierungsansicht
  - Mehrere Runs nacheinander planen, priorisieren, pausieren und ueberwachen.
  - Sinnvoll, wenn PrivateClaw groeßere Produktions-Pipelines traegt.

- Bewertungs- und Review-Schleifen
  - Ein Workflow kann Ergebnisse selbst pruefen, bewerten und bei Bedarf erneut ausfuehren.
  - Besonders interessant fuer Schreib-, JSON- und ComfyUI-Pipelines.

### 0.5.0

Fokus: Reifegrad in Richtung stabiler 1.0-Nutzung.

- Projektweite Qualitaetsprofile
  - Zum Beispiel `strikt strukturiert`, `kreativ schreiben`, `datenextraktion`, `batch rendering`.
  - Setzt sinnvolle Defaults fuer Modelle, Validatoren, Retry-Regeln und Logging.

- Integrierte Regressionstests fuer Workflows
  - Beispiel-Inputs und Soll-Erwartungen pro Workflow.
  - Das waere ein grosser Schritt in Richtung verlaesslicher Releases.

- Vollstaendige Auditierbarkeit
  - Nachvollziehen koennen, welche Eingaben, Memories, Tools und Provider zu welchem Ergebnis gefuehrt haben.
  - Wichtig fuer Vertrauen in laenger laufende Automationen.

- Onboarding und Setup-Assistent
  - Provider einrichten, Datenbankpfad erklaeren, erste Templates anbieten, Sicherheitsrichtlinien erklaeren.
  - Sehr sinnvoll vor einer breiteren 1.0-Freigabe.

## Priorisierte Kurzliste

Wenn nur wenige Themen sofort Platz bekommen sollen, waeren diese fünf aus meiner Sicht am staerksten:

1. Ausgabe-Validierung vor Persistenz (`0.1.2.3`)
2. Leere Entity-Memories automatisch verwerfen (`0.1.2.3`)
3. Schritt erneut ausfuehren / fortsetzen (`0.1.3`)
4. Prompt-Preview mit final aufgeloesten Variablen (`0.1.3`)
5. Workflow-Versionierung mit Diff und Rollback (`0.2.0`)

## Versionierungslogik hinter den Vorschlaegen

- Patch-Versionen wie `0.1.2.3` sollten vor allem Hardening, Guardrails und kleine UX-Hilfen enthalten.
- Minor-Versionen wie `0.1.3` oder `0.1.4` eignen sich fuer klar additive Features ohne tiefen Architekturbruch.
- Ein Sprung auf `0.2.0` ist passend fuer Subworkflows, Versionierung und groessere Memory-/Provider-Ausbauten.
- Spaetere groessere Spruenge sollten Themen sammeln, die das Produkt grundsaetzlich reifer oder erweiterbarer machen.
