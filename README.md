# PrivateClaw

`PrivateClaw` ist eine Desktop-Anwendung in `C++20` und `Qt 6` fuer projektbezogene Automatisierung mit lokalen LLMs.
Die App kombiniert Projektverwaltung, visuelle Workflows, persistente Erinnerung, lokale Tools und zeitgesteuerte Ausfuehrung in einer gemeinsamen Oberflaeche.

## Kurzueberblick

Mit `PrivateClaw` lassen sich Projekte anlegen, einem lokalen Modell-Provider zuweisen und anschliessend ueber mehrstufige Workflows automatisieren.
Workflows koennen manuell gestartet oder ueber Zeitplaene ausgefuehrt werden.
Zwischenergebnisse, Zusammenfassungen, Artefakte und Projektwissen lassen sich als persistente Erinnerung speichern und spaeter wiederverwenden.

## Aktuelle Hauptfunktionen

- Projektverwaltung mit Provider-, Modell- und Systemprompt-Konfiguration
- Unterstuetzung fuer `Ollama` und `LM Studio`
- Visueller Workflow-Editor mit bidirektionaler JSON-Synchronisation
- Workflow-Schrittarten:
  - `prompt`
  - `decision`
  - `save_memory`
  - `tool`
- Persistente Projekterinnerung auf Basis von `SQLite`
- Persistente Run-Historie fuer manuelle und geplante Workflow-Laeufe
- Zeitplaene fuer einmalige, intervallbasierte und taegliche Workflow-Starts
- Hintergrundausfuehrung von Workflows, damit die UI benutzbar bleibt
- ComfyUI-Integration fuer `txt2img`, `img2img` und `inpainting`

## Integrierte Tools

Der aktuelle Tool-Layer deckt bereits mehrere typische Automatisierungsfaelle ab:

- Dateien und Verzeichnisse:
  - `file.read`
  - `file.write_text`
  - `file.edit_diff`
  - `directory.read_recursive`
  - `directory.read_changed`
  - `directory.list`
- Strukturierte Daten:
  - `json.extract`
  - `csv.read`
  - `csv.write`
- Projektgedaechtnis:
  - `memory.search`
  - `memory.summarize`
  - `memory.delete_old`
  - `memory.ingest_directory`
- Externe Integrationen:
  - `http.request`
  - `comfyui.workflow`
- Lokale Kommandos:
  - `shell.run` mit bewusst eingeschraenkter Sicherheitsliste

## Technischer Stack

- `C++20`
- `Qt 6 Widgets`
- `Qt Network`
- `Qt Sql`
- `Qt Concurrent`
- `SQLite`
- `CMake`

## Build

### Voraussetzungen

- `CMake >= 3.24`
- `Qt 6` mit den Komponenten `Widgets`, `Network`, `Sql`, `Concurrent`
- Visual Studio 2022 oder ein anderer passender `C++20`-Compiler
- Optional:
  - `Ollama`
  - `LM Studio`
  - `ComfyUI`

### Build mit Preset

Im Repository ist bereits ein Preset fuer eine lokale `Qt 6.10.3`-Installation enthalten:

```powershell
cmake --preset qt-debug
cmake --build --preset qt-debug-build
```

Hinweis:
Das Preset verweist aktuell auf `C:/Qt/6.10.3/msvc2022_64`.
Wenn `Qt` bei dir an einem anderen Ort installiert ist, passe `CMakePresets.json` oder `CMAKE_PREFIX_PATH` entsprechend an.

### Manueller Build

```powershell
cmake -S . -B build-qt -DCMAKE_PREFIX_PATH=C:/Qt/6.10.3/msvc2022_64
cmake --build build-qt --config Release
```

Das erzeugte Binary liegt anschliessend unter:

```text
build-qt/Release/PrivateClaw.exe
```

Unter Windows werden die noetigen `Qt 6`-Runtime-Dateien nach dem Build automatisch in das Ausgabeverzeichnis deployed.

## Schnellstart

1. App starten
2. Ein Projekt anlegen
3. Provider waehlen: `Ollama` oder `LM Studio`
4. Verfuegbares Modell laden und speichern
5. Im Bereich `Workflows` einen ersten Workflow anlegen
6. Optional Projektwissen in `Erinnerung` oder ueber `memory.ingest_directory` aufbauen
7. Workflow ausfuehren oder einen Zeitplan hinterlegen

## Doku

Weiterfuehrende Dokumentation liegt im Repository:

- [Wiki-Ueberblick](./docs/wiki/README.md)
- [Schnellstart](./docs/wiki/Schnellstart.md)
- [Workflows](./docs/wiki/Workflows.md)
- [Tools](./docs/wiki/Tools.md)
- [Runs](./docs/wiki/Runs.md)
- [Zeitplaene](./docs/wiki/Zeitplaene.md)
- [Beispiel-Workflows](./docs/examples/README.md)

## GitHub Wiki Sync

Die Dateien unter `docs/wiki` sind die pflegbare Quelle fuer die GitHub-Wiki.
Der Workflow [wiki-sync.yml](./.github/workflows/wiki-sync.yml) spiegelt sie automatisch nach `PrivateClaw.wiki.git`.

Fuer den Push in das Wiki-Repository wird ein Repository-Secret `WIKI_PUSH_TOKEN` erwartet.
Das Token braucht Schreibrechte fuer das Wiki-Repository von `UbivisMedia/PrivateClaw`.

## Aktuelle Hinweise

- Zeitplaene laufen derzeit nur, solange die App geoeffnet ist.
- Prompt-Antworten werden von typischen Reasoning-Tags wie `<think>...</think>` bereinigt, bevor sie als sichtbare Ausgabe weiterverwendet werden.
- Tool-Zugriffe auf Dateien und Verzeichnisse bleiben auf den konfigurierten Workspace begrenzt.
- `shell.run` ist absichtlich eingeschraenkt und nicht als allgemeine Shell-Exec gedacht.

## Geeignete Einsatzszenarien

- Code- und Projektanalyse mit persistenter Erinnerung
- Schreibprojekte mit Kapitelspeicher, Notizen und Zusammenfassungen
- Lokale Agenten-Workflows ohne Cloud-Zwang
- Bildpipelines mit LLM-gestuetzter Prompt-Erzeugung und `ComfyUI`
- Wiederkehrende Automatisierungsjobs innerhalb eines laufenden Desktop-Clients

## Projektstatus

`PrivateClaw` ist bereits funktional und fuer erste reale Workflows nutzbar, befindet sich aber weiterhin in aktiver Entwicklung.
Der Schwerpunkt liegt aktuell auf dem Ausbau des Tool-Layers, der Workflow-Usability und der Projekt-Memory-Verwaltung.

## DISCLAIMER !!!

Dieses ist ein reines Hobbyprojekt, verwendung auf eigene Gefahr!
