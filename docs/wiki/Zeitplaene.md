# Zeitplaene

Diese Seite beschreibt die Zeitplanung in `PrivateClaw`, also die automatische Ausfuehrung gespeicherter Workflows zu festgelegten Zeiten.

## Ueberblick

Die Zeitplanung ist im Bereich `Zeitplaene` verfuegbar.
Ein Zeitplan verknuepft:

- genau ein Projekt
- genau einen Workflow
- genau eine Triggerart

Aktive Zeitplaene werden im Hintergrund geprueft und bei Faelligkeit automatisch ausgefuehrt.

## Wichtige Einschraenkung

Aktuell gilt:

- Zeitplaene laufen nur, solange die App geoeffnet ist
- es gibt derzeit keinen separaten Systemdienst oder Hintergrundprozess ausserhalb der App

## Unterstuetzte Triggerarten

### `once`

Ein einmaliger Start zu einem konkreten Zeitpunkt.

In der UI:

- Trigger `Einmalig`
- Zeitpunkt mit Datum und Uhrzeit setzen

Wichtig:

- der Zeitpunkt muss in der Zukunft liegen
- nach erfolgreicher Ausfuehrung wird der Zeitplan deaktiviert

### `interval_minutes`

Ein wiederkehrender Start im Abstand von X Minuten.

In der UI:

- Trigger `Alle X Minuten`
- Minutenwert setzen

Beispiel:

- `60` bedeutet: nach jeder Ausfuehrung wird der naechste Lauf 60 Minuten spaeter angesetzt

### `daily_time`

Ein taeglicher Start zu einer festen Uhrzeit.

In der UI:

- Trigger `Taeglich um`
- Uhrzeit setzen, zum Beispiel `08:30`

Verhalten:

- wenn die Uhrzeit fuer heute schon vorbei ist, wird automatisch morgen geplant

## Zeitplan in der UI anlegen

1. Zu `Zeitplaene` wechseln
2. Projekt waehlen
3. Workflow waehlen
4. `Zeitplan ist aktiv` setzen oder deaktivieren
5. Trigger waehlen
6. Trigger-Konfiguration ausfuellen
7. Vorschau pruefen
8. `Zeitplan speichern`

Die App zeigt danach:

- eine Trigger-Beschreibung
- den voraussichtlichen naechsten Lauf
- den letzten Lauf, falls vorhanden

## Manueller Start ueber die Zeitplan-Ansicht

Mit `Jetzt ausfuehren` kann ein gespeicherter Zeitplan sofort manuell gestartet werden.

Das ist praktisch fuer:

- Funktionstests
- Modellwechsel
- Debugging von Prompt- oder Tool-Schritten

Wichtig:

- der manuelle Start ist nicht dasselbe wie eine echte Faelligkeit
- der Zeitplan selbst bleibt dabei bestehen

## Was bei automatischer Ausfuehrung passiert

Wenn ein Zeitplan faellig ist, fuehrt die App intern denselben Workflow-Mechanismus aus wie bei einem manuellen Workflow-Start.

Dabei passiert im Kern:

1. Projekt wird geladen
2. Workflow wird geladen und validiert
3. Provider und Modell werden aus dem Projekt uebernommen
4. aktuelles Projekt-Memory wird in den Laufkontext geladen
5. Workflow wird im Hintergrund ausgefuehrt
6. neue Memory-Eintraege werden gespeichert
7. der Zeitplan berechnet seinen naechsten Lauf neu

## Zusatzvariablen im Workflow bei Zeitplaenen

Wenn ein Workflow durch einen Zeitplan ausgefuehrt wurde, stehen folgende Variablen zur Verfuegung:

- `{{schedule_id}}`
- `{{schedule_trigger_type}}`
- `{{schedule_trigger_expression}}`
- `{{schedule_origin}}`

### Beispiel

```json
{
  "id": "scheduled_summary",
  "type": "prompt",
  "config": {
    "prompt": "Erstelle einen kurzen Statusbericht fuer den geplanten Lauf {{schedule_id}} mit Trigger {{schedule_trigger_type}}."
  }
}
```

## Beispiel: taeglicher Projektstatus

```json
{
  "version": 1,
  "steps": [
    {
      "id": "draft_summary",
      "type": "prompt",
      "config": {
        "prompt": "Fasse die wichtigsten Entwicklungen seit dem letzten Lauf in drei Stichpunkten zusammen.",
        "output": "daily_summary"
      }
    },
    {
      "id": "save_summary",
      "type": "save_memory",
      "config": {
        "content": "{{daily_summary}}",
        "entry_type": "note",
        "source": "schedule:daily_status",
        "tags": "daily,status,scheduled",
        "relevance": 75
      }
    }
  ]
}
```

Dazu passt ein Zeitplan:

- Projekt: dein Arbeitsprojekt
- Workflow: dieser Status-Workflow
- Trigger: `Taeglich um`
- Uhrzeit: zum Beispiel `09:00`

## Beispiel: regelmaessiger Kontext-Ingest

Wenn du ein Schreib- oder Softwareprojekt aktuell halten willst, kannst du einen wiederkehrenden Ingest-Workflow planen.

Beispiel-Workflow:

```json
{
  "version": 1,
  "steps": [
    {
      "id": "ingest_changes",
      "type": "tool",
      "config": {
        "tool": "memory.ingest_directory",
        "path": "src",
        "mode": "changed",
        "within_minutes": 180,
        "include_extensions": ".cpp,.h,.md",
        "exclude_paths": ".git,build,node_modules",
        "entry_type": "artifact",
        "tags": "codebase,delta,scheduled",
        "relevance": 85,
        "output": "ingest_summary"
      }
    }
  ]
}
```

Dazu passt ein Zeitplan:

- Trigger: `Alle X Minuten`
- Intervall: `180`

## Verhalten nach dem Lauf

Nach einer erfolgreichen oder abgeschlossenen Ausfuehrung aktualisiert die App den Zeitplan:

- `once`: wird deaktiviert
- `interval_minutes`: naechster Lauf = aktueller Abschlusszeitpunkt + Intervall
- `daily_time`: naechster Lauf = naechste passende Tageszeit

Der letzte Laufzeitpunkt wird ebenfalls gespeichert.

## Run-Protokoll und Kontrolle

Automatische Zeitplaene schreiben ihre Laufmeldungen in das `Run-Protokoll`.
Dort kannst du sehen:

- wann ein Zeitplan gestartet wurde
- ob das Projekt und der Workflow geladen wurden
- ob Fehler aufgetreten sind
- ob Memory gespeichert wurde
- wann der naechste Lauf berechnet wurde

## Typische Fehlerquellen

### Zeitplan startet nicht

Pruefe:

- Ist die App ueberhaupt geoeffnet?
- Ist `Zeitplan ist aktiv` gesetzt?
- Liegt der naechste Lauf wirklich in der Vergangenheit oder Gegenwart?
- Ist der referenzierte Workflow noch vorhanden?
- Ist der Workflow aktiv?
- Hat das Projekt noch ein gueltiges Modell?

### Zeitplan ist faellig, aber der Lauf scheitert

Pruefe:

- Ist der Provider erreichbar?
- Ist das Modell noch vorhanden?
- Ist das Workflow-JSON weiterhin gueltig?
- Greift ein Tool auf einen ungueltigen Pfad zu?
- Laeuft `ComfyUI`, falls Bild-Workflows verwendet werden?

### Einmaliger Zeitplan laesst sich nicht speichern

Das passiert typischerweise, wenn der gewaehlte Zeitpunkt bereits in der Vergangenheit liegt.

## Empfohlene Vorgehensweise

- Teste neue Workflows immer zuerst manuell.
- Speichere wichtige Ergebnisse mit `save_memory`.
- Nutze fuer Langzeitprojekte lieber mehrere kleine Zeitplaene statt eines grossen Allzweck-Workflows.
- Plane `memory.ingest_directory` oder Status-Zusammenfassungen regelmaessig, damit spaetere Prompts besseren Kontext haben.
