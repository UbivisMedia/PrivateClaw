# Entwicklungsplan: C++ Desktop-App fuer LLM-gestuetzte Projektautomation

## 1. Zielbild

Es soll eine Desktop-Anwendung in C++ mit funktionaler GUI entstehen, die lokale oder netzwerkbasierte LLM-Systeme wie `Ollama` und `LM Studio` ueber deren HTTP-Schnittstellen ansprechen kann. Die Anwendung soll projektbezogene Aufgaben in mehreren Schritten automatisiert ausfuehren, dabei persistente Erinnerung pro Projekt pflegen und fuer wiederkehrende oder zeitgesteuerte Aufgaben einen integrierten Scheduler bereitstellen.

Die Anwendung ist als "lokaler Automatisierungs-Client" gedacht:

- Benutzer legt Projekte an.
- Pro Projekt werden Workflows, Erinnerungen, Artefakte und Ausfuehrungshistorien gespeichert.
- Ein Workflow kann mehrere LLM- und Tool-Schritte kombinieren.
- Zeitgesteuerte Jobs koennen Workflows automatisch starten.
- Alle Ergebnisse, Entscheidungen und relevanten Fakten werden nachvollziehbar persistiert.

## 2. Produktvision

Die erste Version soll vor allem drei Dinge gut koennen:

1. Lokale LLM-Backends sauber anbinden.
2. Mehrschrittige Aufgaben reproduzierbar und transparent ausfuehren.
3. Projektwissen dauerhaft speichern und spaeter wieder in neue Runs einspielen.

Die Anwendung soll kein "Black Box Agent" sein, sondern ein kontrollierbares Desktop-Werkzeug mit klarer Historie, nachvollziehbaren Schritten und einem restriktiven Sicherheitsmodell.

## 3. Zielnutzer und Kern-Use-Cases

### 3.1 Zielnutzer

- Einzelentwickler
- Technical Writers
- kleine Teams mit lokalen KI-Workflows
- Anwender, die lokale Modelle statt Cloud-LLMs verwenden wollen

### 3.2 Kern-Use-Cases

- Projektbezogene Recherche und Zusammenfassung
- Wiederkehrende Generierung von Reports, Notizen oder Statusupdates
- Mehrschrittige Aufgaben mit Kontextuebernahme aus Projektwissen
- Zeitgesteuerte Routinen wie Tagesberichte, Ticket-Sichtungen oder Dateiauswertungen
- Halbautomatische Bearbeitung mit Benutzerfreigabe vor kritischen Tool-Aktionen

## 4. Pflichtenheft

## 4.1 Muss-Anforderungen fuer den MVP

- Desktop-GUI mit Projektliste, Workflow-Liste, Run-Historie und Memory-Ansicht
- Anbindung an `Ollama` ueber HTTP API
- Anbindung an `LM Studio` ueber OpenAI-kompatible lokale API
- Verwaltung mehrerer Projekte
- Persistente Speicherung in `SQLite`
- Manuell startbare Workflows
- Mehrschrittige Workflow-Ausfuehrung mit mindestens folgenden Schritttypen:
  - `PromptStep`
  - `DecisionStep`
  - `SaveMemoryStep`
  - `ToolStep`
- Erste reale Tools im MVP:
  - `file.read`
  - `directory.read_recursive`
  - `directory.read_changed`
  - `memory.ingest_directory`
  - `file.edit_diff`
  - `comfyui.workflow`
- Variablenkontext pro Run
- Projektbezogene Langzeit-Erinnerung
- Zeitplanung fuer einmalige und wiederkehrende Workflows
- Vollstaendiges Logging pro Ausfuehrung
- Einstellungsdialog fuer Provider, Modelle und Standardparameter

## 4.2 Soll-Anforderungen nach MVP

- Template-System fuer wiederverwendbare Workflows
- Tagging und Volltextsuche ueber Memory-Eintraege
- Import und Export von Workflows als JSON
- Optionaler visueller Workflow-Editor mit bidirektionaler Synchronisation zum JSON
- Streaming-Anzeige fuer laufende LLM-Antworten
- Freigabe-Dialog vor Tool-Schritten mit Schreibzugriff
- Benutzerdefinierte Systemprompts pro Projekt

## 4.3 Kann-Anforderungen fuer spaetere Versionen

- Embeddings und semantische Suche
- Dateiindizierung pro Projekt
- Visueller Workflow-Editor per Drag and Drop
- Erweiterte grafische Decision- und Tool-Konfiguration mit Kanten, Verzweigungen und Validierung
- Plugin-System fuer neue Provider oder Tools
- Rechte- und Rollenkonzept
- Multi-Agenten-Orchestrierung

## 4.4 Nicht-funktionale Anforderungen

- Plattformfokus zuerst auf `Windows`, spaeter optional `Linux`
- UI bleibt auch bei laengeren Runs responsiv
- Absturzsichere Speicherung von Runs und Logs
- Klare Fehlerdiagnose bei Provider-, Netzwerk- oder Parsing-Fehlern
- Saubere Trennung von GUI, Domain-Logik und Infrastruktur
- Moeglichst offline-faehige Nutzung mit lokalen Modellen
- Sichere Standardkonfiguration mit restriktiven Tool-Rechten

## 5. Technologievorschlag

### 5.1 Kernstack

- Sprache: `C++20`
- Build-System: `CMake`
- GUI: `Qt 6 Widgets`
- Netzwerk: `QNetworkAccessManager`
- JSON: `QJsonDocument`, `QJsonObject`
- Datenbank: `SQLite` via `Qt SQL`
- Tests: `Qt Test` oder `Catch2`
- Logging: eigene Logging-Schicht mit Datei- und UI-Appender

### 5.2 Begruendung

- `Qt 6` deckt GUI, HTTP, JSON, Timer, Threads und SQL in einem konsistenten Framework ab.
- `Qt Widgets` ist fuer einen produktiven Desktop-MVP einfacher und stabiler als ein frueher QML-Fokus.
- `SQLite` ist fuer lokale Projektpersistenz schnell, portabel und wartungsarm.
- `CMake` ermoeglicht spaetere Skalierung der Modulstruktur und CI-Integration.

## 6. Fachliches Domaenenmodell

### 6.1 Hauptobjekte

- `Project`
  - Name
  - Beschreibung
  - Standardmodell
  - Systemprompt
  - Tags

- `Workflow`
  - Name
  - Beschreibung
  - Aktiv/Inaktiv
  - Schrittliste

- `WorkflowStep`
  - Typ
  - Konfiguration
  - Eingangsvariablen
  - Ausgangsvariablen
  - Fehlerstrategie

- `Run`
  - Referenz auf Projekt
  - Referenz auf Workflow
  - Status
  - Startzeit
  - Endzeit
  - Logs
  - Ergebnis

- `MemoryEntry`
  - Projektbezug
  - Typ
  - Inhalt
  - Quelle
  - Tags
  - Relevanz
  - Zeitstempel

- `Schedule`
  - Projekt
  - Workflow
  - Trigger-Regel
  - naechste Ausfuehrung
  - letzter Status

## 7. Zielarchitektur

### 7.1 Schichtenmodell

1. `Presentation`
   - Qt Widgets UI
   - ViewModels / Controller

2. `Application`
   - Workflow-Orchestrierung
   - Run-Service
   - Scheduler-Service
   - Projekt- und Memory-Services

3. `Domain`
   - Entities
   - Workflow-Schritte
   - Regeln, Statusmodelle, Fehlerobjekte

4. `Infrastructure`
   - LLM-Provider
   - SQLite-Repositories
   - Logging
   - Dateizugriffe
   - HTTP-Clients

### 7.2 Hauptkomponenten

- `UiShell`
  - Hauptfenster, Navigation, Panels, Dialoge

- `WorkflowEngine`
  - Fuehrt Workflow-Schritte sequentiell aus
  - Verwaltet Variablenkontext
  - Fuehrt Retry-, Timeout- und Abbruchregeln aus

- `ProviderManager`
  - Verwaltet Providerinstanzen und Modellkonfigurationen

- `MemoryService`
  - Speichert, sucht und gewichtet Projekterinnerungen

- `SchedulerService`
  - Plant und startet zeitgesteuerte Runs

- `ToolExecutor`
  - Fuehrt erlaubte lokale Aktionen kontrolliert aus
  - Verwaltet Tool-spezifische Konfiguration getrennt von der Workflow-Engine
  - Erste Integrationen: Dateilesen, Diff-basiertes Editieren, ComfyUI-Workflow-API

- `Persistence`
  - Repositories fuer Projekte, Workflows, Runs, Memory, Schedules

## 8. Modulstruktur und vorgeschlagene Repo-Struktur

```text
PrivateClaw/
  CMakeLists.txt
  cmake/
  docs/
    entwicklungsplan_llm_desktop_app.md
  src/
    app/
      main.cpp
      ApplicationBootstrap.cpp
      ApplicationBootstrap.h
    ui/
      MainWindow.cpp
      MainWindow.h
      ProjectPanel.cpp
      WorkflowPanel.cpp
      MemoryPanel.cpp
      SchedulePanel.cpp
      RunLogPanel.cpp
      dialogs/
    core/
      WorkflowEngine.cpp
      WorkflowEngine.h
      RunContext.cpp
      RunContext.h
      WorkflowCompiler.cpp
      WorkflowCompiler.h
    domain/
      Project.h
      Workflow.h
      WorkflowStep.h
      Run.h
      MemoryEntry.h
      Schedule.h
    providers/
      ILlmProvider.h
      OllamaProvider.cpp
      OllamaProvider.h
      LmStudioProvider.cpp
      LmStudioProvider.h
      ProviderManager.cpp
      ProviderManager.h
    storage/
      DatabaseManager.cpp
      DatabaseManager.h
      ProjectRepository.cpp
      WorkflowRepository.cpp
      RunRepository.cpp
      MemoryRepository.cpp
      ScheduleRepository.cpp
    scheduler/
      SchedulerService.cpp
      SchedulerService.h
      TriggerCalculator.cpp
      TriggerCalculator.h
    tools/
      ToolExecutor.cpp
      ToolExecutor.h
      FileReadTool.cpp
      FileWriteTool.cpp
      ShellCommandTool.cpp
    services/
      ProjectService.cpp
      WorkflowService.cpp
      MemoryService.cpp
      SettingsService.cpp
      AuditService.cpp
    utils/
      Logger.cpp
      Result.h
      JsonHelpers.h
  resources/
    icons/
    styles/
  tests/
    unit/
    integration/
```

## 9. GUI-Konzept

### 9.1 Hauptfenster

Das Hauptfenster sollte aus vier Kernbereichen bestehen:

- linke Navigation: Projekte, Workflows, Schedules, Einstellungen
- mittlerer Hauptbereich: Listen, Editoren, Detailansichten
- rechte Seitenleiste: Kontext, Variablen, Memory-Vorschlaege
- unterer Bereich: Run-Logs, Fehlermeldungen, Streaming-Ausgaben

### 9.2 Zentrale Screens

- `Projektverwaltung`
  - Projekte anlegen, bearbeiten, archivieren

- `Workflow-Editor`
  - Schritte definieren
  - Parameter und Variablen mappen
  - Testlauf starten

- `Run-Monitor`
  - aktueller Schritt
  - Eingaben und Ausgaben
  - Fehler und Retries

- `Memory-Ansicht`
  - Eintraege durchsuchen
  - Tags verwalten
  - Eintraege an Runs koppeln

- `Scheduler-Ansicht`
  - Zeitplaene erstellen
  - naechste Ausfuehrungen sehen
  - letzte Ergebnisse pruefen

## 10. Provider-Abstraktion

### 10.1 Interface

Gemeinsames Interface fuer alle LLM-Backends:

- `listModels()`
- `chat(request)`
- `generate(request)`
- `healthCheck()`
- `supportsStreaming()`

### 10.2 Ollama

- Basis-URL konfigurierbar
- Modellliste ueber Provider-API abrufbar
- Chat- oder Generate-Endpunkte unterstuetzen
- Streaming optional fuer UI-Live-Ausgabe

### 10.3 LM Studio

- OpenAI-kompatibler Endpoint
- Modellname und Basis-URL in Settings speicherbar
- Fokus auf Chat-Completions-kompatiblen Zugriff

## 11. Workflow-Engine

### 11.1 MVP-Schritttypen

- `PromptStep`
  - sendet Kontext an LLM
  - speichert Antwort in Variable

- `DecisionStep`
  - prueft Regeln auf Basis von Variablen oder LLM-Ausgabe
  - waehlt naechsten Schritt

- `SaveMemoryStep`
  - persistiert Erkenntnisse im Projekt-Memory

- `DelayStep`
  - pausiert Ausfuehrung oder verschiebt naechsten Schritt

- `ToolStep`
  - startet erlaubte lokale Aktion

### 11.2 Laufzeitmodell

Pro Run wird ein `RunContext` aufgebaut:

- Projektmetadaten
- ausgewaehltes Modell
- Systemprompt
- Variablenmap
- relevante Memory-Eintraege
- Audit-Infos

Jeder Schritt liefert ein standardisiertes Ergebnis:

- `success`
- `outputVariables`
- `logs`
- `nextStepId`
- `error`

## 12. Persistente Erinnerung

### 12.1 Ziel

Das System soll pro Projekt Wissen ueber laengere Zeit nutzbar halten. Dazu gehoeren:

- stabile Fakten
- fruehere Entscheidungen
- wiederkehrende Anforderungen
- bekannte Fehlerbilder
- bevorzugte Formate oder Schreibstile

### 12.2 MVP-Strategie

Noch ohne Embeddings:

- Speicherung als strukturierte Eintraege in SQLite
- Tags und Typen fuer Filterung
- Volltextsuche
- Priorisierung nach Aktualitaet, Relevanz und Tag-Match

### 12.3 Erweiterung in V2

- Embeddings je Memory-Eintrag
- semantische Aehnlichkeitssuche
- automatische Memory-Konsolidierung

## 13. Zeitgesteuerte Aufgaben

### 13.1 Scheduler-MVP

Unterstuetzte Trigger:

- einmalig zu Datum/Uhrzeit
- taeglich
- woechentlich
- alle X Stunden oder Minuten

### 13.2 Verhalten

- Scheduler laeuft innerhalb der App
- beim App-Start werden ueberfaellige Jobs erkannt
- Jobs koennen optional nach Benutzerbestaetigung starten
- jeder automatisch gestartete Workflow erzeugt einen vollstaendigen Run-Eintrag

### 13.3 V2-Option

- Hintergrunddienst oder Tray-Modus
- Cron-artige Regeln
- Kalenderintegration

## 14. Datenmodell fuer SQLite

### 14.1 Tabellen

- `projects`
- `project_settings`
- `workflows`
- `workflow_steps`
- `workflow_edges`
- `runs`
- `run_logs`
- `run_variables`
- `memory_entries`
- `memory_tags`
- `memory_links`
- `schedules`
- `provider_configs`
- `tool_policies`

### 14.2 Wichtige Beziehungen

- Ein Projekt hat viele Workflows.
- Ein Workflow hat viele Schritte.
- Ein Workflow hat viele Runs.
- Ein Projekt hat viele Memory-Eintraege.
- Ein Schedule referenziert genau einen Workflow.
- Ein Run kann Memory-Eintraege erzeugen oder referenzieren.

## 15. Sicherheitsmodell

### 15.1 Grundsatz

LLM-Antworten duerfen nie ungeprueft kritische lokale Aktionen ausfuehren.

### 15.2 MVP-Regeln

- Tool-Aufrufe sind standardmaessig deaktiviert.
- Schreibende Dateizugriffe brauchen explizite Freigabe.
- Shell-Kommandos sind initial stark eingeschraenkt.
- Jeder Tool-Aufruf wird mit Input, Output und Exit-Status protokolliert.
- Sensible Pfade koennen global blockiert werden.

## 16. Entwicklungsphasen

## Phase 1: Grundlagen und Bootstrap

Ziel:

- CMake-Projekt aufsetzen
- Qt-6-Abhaengigkeiten integrieren
- Logging und Settings-Basis schaffen
- SQLite initialisieren

Ergebnis:

- startfaehige Desktop-App mit leerem Hauptfenster
- Konfigurations- und Datenbankdateien werden angelegt

## Phase 2: Projektverwaltung und Persistenz

Ziel:

- Projekte anlegen, bearbeiten, loeschen
- SQLite-Repositories fuer Kernobjekte
- Listen- und Detailansichten

Ergebnis:

- Projektverwaltung ist UI-seitig und persistent funktionsfaehig

## Phase 3: Provider-Anbindung

Ziel:

- `ILlmProvider`
- `OllamaProvider`
- `LmStudioProvider`
- Verbindungs- und Modelltest im UI

Ergebnis:

- Testprompt kann ueber beide Provider erfolgreich gesendet werden

## Phase 4: Workflow-Engine MVP

Ziel:

- JSON-basiertes Workflow-Modell
- Schrittausfuehrung
- Variablenkontext
- Run-Historie

Ergebnis:

- Ein einfacher Mehrschritt-Workflow laeuft stabil durch

## Phase 5: Memory-System

Ziel:

- Memory-CRUD
- Tagging
- relevante Memory-Eintraege vor LLM-Aufrufen abrufen

Ergebnis:

- Projektwissen wird ueber mehrere Runs hinweg wiederverwendet

## Phase 6: Scheduler

Ziel:

- einfache Triggerregeln
- automatische Workflow-Starts
- Wiederanlauf nach App-Neustart

Ergebnis:

- geplanter Workflow startet zeitgesteuert und wird protokolliert

## Phase 7: Tools und Guardrails

Ziel:

- sicherer ToolExecutor
- Datei-Lesen, Datei-Schreiben, Shell-Kommando als erste Tools
- Freigabe- und Auditkonzept

Ergebnis:

- Workflow kann kontrolliert lokale Aktionen ausfuehren

## Phase 8: Haertung und Release-Vorbereitung

Ziel:

- Tests erweitern
- Fehlerbehandlung verbessern
- Packaging
- Pilotbetrieb mit realen Projekten

Ergebnis:

- installierbare MVP-Version

## 17. Teststrategie

### 17.1 Unit-Tests

- Workflow-Schrittlogik
- Trigger-Berechnung
- Repository-Funktionen
- Memory-Ranking

### 17.2 Integrationstests

- Provider-Kommunikation mit Mock-Server
- SQLite-Persistenz
- Workflow-End-to-End mit Testdaten
- Scheduler mit simulierten Zeiten

### 17.3 Manuelle Tests

- GUI-Flows
- Fehlerdialoge
- Recovery nach Provider-Ausfall
- Restart-Verhalten bei ueberfaelligen Schedules

## 18. Risiken und Gegenmassnahmen

### 18.1 Unzuverlaessige LLM-Ausgaben

Risiko:

- Schritte liefern uneinheitliche oder unbrauchbare Ergebnisse.

Gegenmassnahmen:

- strukturierte Ausgabeformate
- Validierung pro Schritt
- Retries und Fallback-Prompts

### 18.2 Tool-Missbrauch

Risiko:

- LLM generiert unsichere Aktionen.

Gegenmassnahmen:

- restriktive Standardrechte
- Freigabepflicht
- Audit-Logs

### 18.3 Technische Komplexitaet

Risiko:

- zu viele Features vor MVP-Abschluss.

Gegenmassnahmen:

- klare MVP-Grenze
- Embeddings und visuellen Editor erst nach Stabilisierung

## 19. Abnahmekriterien fuer den MVP

Der MVP ist erreicht, wenn folgende Punkte gemeinsam erfuellt sind:

- Ein Projekt kann in der GUI angelegt und gespeichert werden.
- Ein Provider fuer `Ollama` oder `LM Studio` kann konfiguriert und getestet werden.
- Ein Workflow mit mindestens drei Schritten kann gespeichert und ausgefuehrt werden.
- Ein Run erzeugt Logs, Ergebnisdaten und mindestens einen speicherbaren Memory-Eintrag.
- Ein zeitgesteuerter Workflow startet automatisch und wird nachvollziehbar protokolliert.
- Die App bleibt waehrend der Ausfuehrung responsiv.

## 20. Empfohlene unmittelbare Naechstschritte

1. CMake- und Qt-6-Grundgeruest anlegen.
2. Datenbankschema fuer `projects`, `workflows`, `runs`, `memory_entries` und `schedules` definieren.
3. Minimal-GUI mit Projektliste und Einstellungsdialog erstellen.
4. `OllamaProvider` zuerst implementieren, `LM Studio` direkt danach.
5. Einen einfachen Workflow-End-to-End als Referenzfall bauen.

## 21. Vorschlag fuer den ersten Referenz-Workflow

Name:

`Projektstatus zusammenfassen`

Ablauf:

1. relevante Memory-Eintraege des Projekts laden
2. Benutzerprompt und Systemprompt kombinieren
3. LLM um strukturiertes Statusupdate bitten
4. Ergebnis als Run-Artefakt speichern
5. Kernaussagen als neue Memory-Eintraege ablegen

Damit laesst sich frueh pruefen, ob Provider, Workflow-Engine, Memory und GUI sinnvoll zusammenspielen.
