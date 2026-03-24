# Schnellstart

Diese Seite zeigt den schnellsten Weg zu einem funktionierenden Projekt in `PrivateClaw`.

## 1. Voraussetzungen

Vor dem ersten produktiven Einsatz solltest du sicherstellen, dass mindestens eines der folgenden Systeme laeuft:

- `Ollama`
- `LM Studio`

Optional fuer Bild-Workflows:

- `ComfyUI`

## 2. Projekt anlegen

Wechsle in der App zu `Projekte` und lege ein neues Projekt an.
Die wichtigsten Felder sind:

- `Name`: Frei waehlbarer Projektname
- `Provider`: `Ollama` oder `LM Studio`
- `Modell`: Modellname des gewaehlten Providers
- `Systemprompt`: Optionaler projektweiter Steuerprompt

### Modellwahl

Nutze `Modelle laden`, um dir die verfuegbaren Modelle des gewaehlten Providers anzeigen zu lassen.
Waehle danach das Modell, mit dem Workflows standardmaessig laufen sollen.

## 3. Ersten Workflow anlegen

Wechsle zu `Workflows`.
Dort kannst du:

- einen neuen Workflow anlegen
- das Zielprojekt auswaehlen
- den visuellen Editor nutzen
- oder direkt JSON bearbeiten

Ein minimaler Workflow sieht so aus:

```json
{
  "version": 1,
  "steps": [
    {
      "id": "draft_summary",
      "type": "prompt",
      "name": "Projektstatus entwerfen",
      "config": {
        "prompt": "Fasse den aktuellen Projektstand in drei Stichpunkten zusammen."
      }
    }
  ]
}
```

Speichere den Workflow und starte ihn ueber `Workflow ausfuehren`.

## 4. Memory nutzen

Wechsle zu `Erinnerung`, um Projektwissen anzulegen oder zu pruefen.
Memory-Eintraege koennen sein:

- Notizen
- Artefakte
- Prompt-Ergebnisse
- automatisch erzeugte Ingest-Snapshots

Dieses Wissen wird bei Prompt-Schritten automatisch als Projektkontext beruecksichtigt, solange du es nicht bewusst anders steuerst.

## 5. Beispiel fuer Prompt plus Memory

```json
{
  "version": 1,
  "steps": [
    {
      "id": "draft_summary",
      "type": "prompt",
      "config": {
        "prompt": "Fasse die letzten Projektentwicklungen in drei Stichpunkten zusammen.",
        "output": "summary"
      }
    },
    {
      "id": "save_summary",
      "type": "save_memory",
      "config": {
        "content": "{{summary}}",
        "entry_type": "note",
        "source": "workflow:status_update",
        "tags": "status,zusammenfassung",
        "relevance": 70
      }
    }
  ]
}
```

## 6. Tool-Schritte nutzen

Tool-Schritte erlauben Dateiarbeit, Kontext-Ingest oder Bildgenerierung.
Beispiel fuer rekursives Einlesen eines Verzeichnisses:

```json
{
  "id": "read_src",
  "type": "tool",
  "config": {
    "tool": "directory.read_recursive",
    "path": "src",
    "include_extensions": ".cpp,.h,.md",
    "exclude_paths": ".git,build,node_modules",
    "output": "source_snapshot"
  }
}
```

Die Details dazu stehen in [Tools](./Tools.md).

## 7. Zeitplan anlegen

Wechsle zu `Zeitplaene`, um einen gespeicherten Workflow automatisch zu starten.
Aktuell verfuegbare Trigger:

- `Einmalig`
- `Alle X Minuten`
- `Taeglich um`

Nach dem Speichern zeigt die App direkt den voraussichtlichen naechsten Lauf an.

Die Details dazu stehen in [Zeitplaene](./Zeitplaene.md).

## 8. Typischer Arbeitsablauf

Ein praxisnaher Ablauf sieht oft so aus:

1. Projekt anlegen
2. Modell waehlen
3. Memory oder Dateien als Kontext aufbauen
4. Workflow im visuellen Editor erstellen
5. Workflow testen
6. Ergebnis ins Memory schreiben
7. Optional ueber Zeitplaene automatisieren

## 9. Fehlersuche

Wenn etwas nicht klappt, pruefe zuerst:

- Ist der richtige Provider im Projekt gesetzt?
- Ist das Modell noch verfuegbar?
- Ist der Workflow gespeichert und JSON-gueltig?
- Ist der Pfad eines Tools innerhalb des Workspace?
- Laeuft `ComfyUI`, wenn Bild-Tools verwendet werden?
- Zeigt das `Run-Protokoll` eine konkrete Fehlermeldung?
