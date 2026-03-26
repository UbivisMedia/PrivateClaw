# PrivateClaw Wiki

Diese Wiki-Dokumentation beschreibt den aktuellen Nutzungsstand der Anwendung `PrivateClaw`.
Sie ist auf den derzeit implementierten Funktionsumfang abgestimmt und richtet sich an Anwender, die mit Projekten, Workflows, Tools und Zeitplaenen arbeiten wollen.

## Inhaltsverzeichnis

- [Schnellstart](./Schnellstart.md)
- [Workflows](./Workflows.md)
- [Tools](./Tools.md)
- [Runs](./Runs.md)
- [Zeitplaene](./Zeitplaene.md)

## Was PrivateClaw aktuell kann

PrivateClaw ist eine Desktop-Anwendung zur projektbezogenen Automatisierung mit lokalen LLMs und Tool-Schritten.
Der aktuelle Schwerpunkt liegt auf:

- Projektverwaltung mit Provider- und Modellwahl
- Workflow-Ausfuehrung ueber `Ollama` oder `LM Studio`
- Persistenter Projekterinnerung mit anpinnbaren Schluessel-Eintraegen
- Visuellem Workflow-Editor mit JSON-Synchronisation
- Template-Bibliothek fuer schnell importierbare Workflow-Vorlagen
- Tool-Schritten fuer Dateien, Verzeichnisse, Memory-Ingest und `ComfyUI`
- Zeitgesteuerter Ausfuehrung von Workflows innerhalb der laufenden App

## Zentrale Begriffe

- `Projekt`: Behaelter fuer Modellwahl, Systemprompt, Workflows, Memory und Zeitplaene
- `Workflow`: JSON-definierte oder visuell bearbeitete Folge von Schritten
- `Schritt`: Ein einzelner Workflow-Baustein wie `prompt`, `decision`, `save_memory` oder `tool`
- `Memory`: Persistente Projekterinnerung, die spaeter wieder in Prompts einfliessen kann
- `Zeitplan`: Automatischer Start eines bestimmten Workflows zu einem definierten Zeitpunkt oder Intervall

## Navigation in der App

In der linken Navigation der App gibt es aktuell fuenf Hauptbereiche:

- `Projekte`
- `Workflows`
- `Erinnerung`
- `Runs`
- `Zeitplaene`

Das untere `Run-Protokoll` zeigt Laufmeldungen, Fehler und Hintergrundstarts.

## Wichtige Hinweise

- Automatische Zeitplaene laufen, solange die App selbst aktiv ist, bei verfuegbarem Tray auch im Hintergrund.
- Prompt-Antworten werden automatisch von typischen Reasoning-Tags wie `<think>...</think>` und offensichtlichen Thinking-/Meta-Bloecken bereinigt.
- Persistente Folgeschritte koennen zusaetzlich Guardrails wie `sanitize_before_persist`, `sanitize_before_write`, `skip_if_all_inputs_empty` und `fail_if_all_inputs_empty` nutzen.
- Sichtbare Workflow-Ausgaben verwenden die bereinigte Antwort.
- Extrahiertes Reasoning kann bei Bedarf separat ueber Variablen wie `{{last_reasoning}}` gespeichert werden.
- Tool-Zugriffe auf Dateien und Verzeichnisse sind auf den konfigurierten Workspace ausgerichtet.

## Beispiele

Beispiel-Workflows liegen unter:

- [docs/examples/README.md](../examples/README.md)
- [docs/examples/comfy_prompt_pipeline_workflow.json](../examples/comfy_prompt_pipeline_workflow.json)

## Empfohlene Lesereihenfolge

1. [Schnellstart](./Schnellstart.md)
2. [Workflows](./Workflows.md)
3. [Tools](./Tools.md)
4. [Runs](./Runs.md)
5. [Zeitplaene](./Zeitplaene.md)
