# Workflows

Diese Seite beschreibt den Workflow-Editor, die Schrittarten, Variablen und typische Muster fuer die Arbeit mit `PrivateClaw`.

## Ueberblick

Ein Workflow ist eine Folge einzelner Schritte.
Aktuell werden vier Schrittarten unterstuetzt:

- `prompt`
- `save_memory`
- `decision`
- `tool`

Workflows koennen auf zwei Arten bearbeitet werden:

- direkt als JSON
- ueber den visuellen Editor

Wenn der visuelle Editor aktiv ist, blendet die App die JSON-Ansicht aus, um mehr Platz zu schaffen.
Beide Darstellungen bleiben inhaltlich synchron.

## Aufbau eines Workflows

Ein Workflow ist ein JSON-Objekt mit einem `steps`-Array:

```json
{
  "version": 1,
  "steps": [
    {
      "id": "step_a",
      "type": "prompt",
      "name": "Erster Schritt",
      "config": {
        "prompt": "Beschreibe hier die Aufgabe."
      }
    }
  ]
}
```

### Pflichtfelder pro Schritt

- `id`
- `type`

Optional:

- `name`
- `config`

## Der visuelle Editor

Im visuellen Editor kannst du:

- Schritte hinzufuegen
- Schrittarten wechseln
- Schrittfelder im Formular bearbeiten
- Schritte verschieben
- Schritte loeschen

Aktuell gibt es Buttons fuer:

- `Prompt`
- `Memory`
- `Decision`
- `Tool`

Die rechte Detailansicht ist scrollbar, damit auch umfangreiche Konfigurationen wie `ComfyUI` bequem bearbeitet werden koennen.

## Verfuegbare Platzhaltervariablen

Workflows nutzen Platzhalter im Format `{{variable_name}}`.
Diese Variablen koennen in Prompts, Memory-Inhalten, Decision-Feldern und Tool-Konfigurationen verwendet werden.

### Basisvariablen

- `{{project_name}}`
- `{{project_id}}`
- `{{project_description}}`
- `{{workflow_name}}`
- `{{selected_model}}`
- `{{workspace_root}}`
- `{{comfyui_base_url}}`
- `{{project_memory}}`
- `{{project_memory_count}}`

### Variablen aus Prompt-Schritten

Wenn ein Prompt-Schritt `output: "summary"` setzt, entstehen unter anderem:

- `{{summary}}`
- `{{summary_raw}}`
- `{{summary_reasoning}}` falls ein Reasoning-Block erkannt wurde

Zusatzvariablen fuer die letzte Prompt-Antwort:

- `{{last_response}}`
- `{{last_response_raw}}`
- `{{last_reasoning}}`
- `{{last_response_reasoning}}`

### Variablen aus Decision-Schritten

- `{{last_decision}}`
- die konfigurierte Output-Variable, zum Beispiel `{{csv_check}}`

### Variablen aus Tool-Schritten

- `{{last_tool_output}}`
- `{{last_tool_name}}`
- die konfigurierte Output-Variable, zum Beispiel `{{render_summary}}`

### Variablen aus Memory-Schritten

- `{{last_memory_content}}`
- `{{last_memory_type}}`

### Zusatzvariablen bei Zeitplaenen

Wenn ein Workflow durch einen Zeitplan gestartet wurde, stehen zusaetzlich bereit:

- `{{schedule_id}}`
- `{{schedule_trigger_type}}`
- `{{schedule_trigger_expression}}`
- `{{schedule_origin}}`

## Schrittart `prompt`

Ein `prompt`-Schritt sendet eine Anfrage an das im Projekt oder im Schritt definierte LLM-Modell.

### Wichtige Konfigurationsfelder

- `prompt`: eigentlicher Benutzerprompt
- `output`: Name der Ausgabevariablen
- `system_prompt`: optionaler Schritt-spezifischer Systemprompt
- `model`: optionales Modell-Override fuer diesen Schritt

### Beispiel

```json
{
  "id": "draft_positive_prompt",
  "type": "prompt",
  "config": {
    "prompt": "Erzeuge einen kompakten englischen Bildprompt fuer eine Fantasy-Stadt bei Nacht.",
    "output": "positive_prompt"
  }
}
```

### Verhalten von Memory im Prompt

Wenn du `{{project_memory}}` nicht selbst explizit einbaust, fuegt die Engine vorhandenes Projektwissen automatisch an den Systemprompt an.
So bekommen Prompts bereits Kontext, ohne dass du jeden Workflow vollstaendig ausformulieren musst.

### Reasoning-Bereinigung

Falls ein Modell Tags wie `<think>...</think>` oder aehnliche Reasoning-Abschnitte ausgibt, werden diese automatisch aus der sichtbaren Ausgabe entfernt.

Das bedeutet:

- `{{last_response}}` enthaelt nur den bereinigten Text
- `{{last_reasoning}}` kann das extrahierte Thinking enthalten
- `{{last_response_raw}}` enthaelt die Rohantwort

## Schrittart `save_memory`

Ein `save_memory`-Schritt legt einen Memory-Eintrag an.
Er wird im Lauf vorbereitet und nach erfolgreicher Workflow-Ausfuehrung gespeichert.

### Wichtige Konfigurationsfelder

- `content`: Inhalt des Memory-Eintrags
- `entry_type` oder `memory_type`: Typ, zum Beispiel `note` oder `artifact`
- `source`: Quelle des Eintrags
- `tags`: Tags als CSV oder Array
- `relevance`: Wert zwischen `0` und `100`

### Standardverhalten

Wenn `content` fehlt, wird automatisch `{{last_response}}` verwendet.

### Beispiel

```json
{
  "id": "save_summary",
  "type": "save_memory",
  "config": {
    "content": "{{last_response}}",
    "entry_type": "note",
    "source": "workflow:daily_summary",
    "tags": "status,daily",
    "relevance": 70
  }
}
```

### Reasoning separat speichern

Wenn du das Thinking bewusst getrennt sichern willst:

```json
{
  "id": "save_reasoning",
  "type": "save_memory",
  "config": {
    "content": "{{last_reasoning}}",
    "entry_type": "reasoning",
    "source": "workflow:reasoning_capture",
    "tags": "reasoning,llm",
    "relevance": 40
  }
}
```

## Schrittart `decision`

`decision` prueft Inhalte und entscheidet, ob ein Workflow normal weiterlaeuft oder zu einem anderen Schritt springt.

### Einfache Decision

Wichtige Felder:

- `input`
- `operator`
- `value`
- `output`
- `true_result`
- `false_result`
- `if_true`
- `if_false`
- `case_sensitive`

Unterstuetzte Operatoren:

- `equals`
- `not_equals`
- `contains`
- `not_contains`
- `starts_with`
- `ends_with`
- `empty`
- `not_empty`
- `regex`

### Beispiel

```json
{
  "id": "check_csv",
  "type": "decision",
  "config": {
    "input": "{{last_response}}",
    "operator": "contains",
    "value": ",",
    "output": "csv_check",
    "true_result": "csv_ok",
    "false_result": "needs_retry",
    "if_true": "save_world_map",
    "if_false": "retry_world_map"
  }
}
```

### Regelbasierte Decision

Statt einer einfachen Bedingung kann auch ein `rules`-Array verwendet werden.
Jede Regel kann einen eigenen Operator, Vergleichswert, ein Ergebnis und ein `next_step` definieren.

Wenn keine Regel trifft, greifen:

- `default_result`
- `default_next`

## Schrittart `tool`

`tool` fuehrt einen externen oder lokalen Arbeitsschritt aus.
Die eigentliche Tool-Logik liegt nicht in der Engine, sondern in der Tool-Schicht.

### Allgemeine Konfigurationsfelder

- `tool`: Name des Tools
- `output`: Name der Ausgabevariablen

Alle weiteren Felder haengen vom gewaehlten Tool ab.

Die Details dazu stehen in [Tools](./Tools.md).

## Beispiel: LLM plus Tool plus Memory

```json
{
  "version": 1,
  "steps": [
    {
      "id": "read_docs",
      "type": "tool",
      "config": {
        "tool": "directory.read_recursive",
        "path": "docs",
        "include_extensions": ".md",
        "output": "docs_snapshot"
      }
    },
    {
      "id": "summarize_docs",
      "type": "prompt",
      "config": {
        "prompt": "Fasse die folgenden Inhalte kurz zusammen:\n\n{{docs_snapshot}}",
        "output": "docs_summary"
      }
    },
    {
      "id": "save_docs_summary",
      "type": "save_memory",
      "config": {
        "content": "{{docs_summary}}",
        "entry_type": "artifact",
        "source": "workflow:docs_summary",
        "tags": "docs,summary",
        "relevance": 80
      }
    }
  ]
}
```

## Beispiel: Bildworkflow mit Prompt-Generierung

Ein fertiges Beispiel liegt unter:

- [docs/examples/comfy_prompt_pipeline_workflow.json](../examples/comfy_prompt_pipeline_workflow.json)

Dieses Muster:

- erzeugt einen positiven Prompt
- erzeugt einen negativen Prompt
- speichert das Prompt-Paar optional im Memory
- rendert anschliessend ein Bild ueber `ComfyUI`
- speichert das Render-Ergebnis ebenfalls

## Best Practices fuer stabile Workflows

- Gib `output`-Variablen bewusst Namen, statt nur `last_response` zu verwenden.
- Nutze `save_memory`, wenn Ergebnisse spaeter wiederverwendet werden sollen.
- Halte `decision`-Schritte einfach und nachvollziehbar.
- Baue groessere Workflows schrittweise auf und teste sie zuerst ohne Zeitplan.
- Verwende fuer umfangreiche Dateikontexte lieber Ingest-Tools statt riesiger Prompt-Texte.
- Nutze `{{last_reasoning}}` nur bewusst und getrennt, nicht als normale Endausgabe.
