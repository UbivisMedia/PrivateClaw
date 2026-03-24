# Tools

Diese Seite beschreibt die aktuell verfuegbaren Workflow-Tools in `PrivateClaw`, ihre typischen Konfigurationsfelder und bewaehrte Einsatzmuster.

## Ueberblick

Aktuell verfuegbare Tools:

- `file.read`
- `json.extract`
- `csv.read`
- `csv.write`
- `directory.read_recursive`
- `directory.read_changed`
- `directory.list`
- `memory.search`
- `memory.summarize`
- `memory.delete_old`
- `memory.ingest_directory`
- `file.write_text`
- `file.edit_diff`
- `http.request`
- `shell.run`
- `comfyui.workflow`

Tool-Schritte werden im Workflow ueber `type: "tool"` und `config.tool` verwendet.

Allgemeines Muster:

```json
{
  "id": "example_tool_step",
  "type": "tool",
  "config": {
    "tool": "file.read",
    "output": "tool_result"
  }
}
```

## Allgemeine Hinweise

- Tools arbeiten relativ zum konfigurierten Workspace.
- Dateipfade sollten innerhalb dieses Workspace liegen oder vorab zur Allowlist hinzugefuegt werden.
- Tool-Ausgaben koennen mit `output` in eine Workflow-Variable geschrieben werden.
- Manche Tools liefern nur Text, andere JSON-Zusammenfassungen.
- `memory.ingest_directory` erzeugt zusaetzlich echte Memory-Eintraege.
- Projekt-Secrets koennen in Tool-Konfigurationen ueber `{{secret.name}}` verwendet werden.
- Riskante Tools koennen pro Projekt bestaetigungspflichtig sein.

## Riskante Tools und Freigaben

Aktuell gelten diese Tools als riskant:

- `shell.run`
- `file.edit_diff`
- `http.request`

Wenn die Projekt-Policy das verlangt, fragt `PrivateClaw` vor manuellen Laeufen nach einer Freigabe.
Automatische Zeitplaene brechen solche Schritte standardmaessig ab, bis das Projekt unbeaufsichtigte riskante Tools ausdruecklich erlaubt.

## `file.read`

Liest eine Datei aus dem Workspace.

### Wichtige Felder

- `path`
- `line_start`
- `line_end`
- `max_chars`
- `output`

### Beispiel

```json
{
  "id": "read_plan",
  "type": "tool",
  "config": {
    "tool": "file.read",
    "path": "docs/entwicklungsplan_llm_desktop_app.md",
    "line_start": 1,
    "line_end": 120,
    "max_chars": 4000,
    "output": "plan_text"
  }
}
```

## `json.extract`

Extrahiert gezielt einen Wert aus JSON-Text, zum Beispiel aus einer Modellantwort oder einem API-Response.

### Wichtige Felder

- `input`
- `path`
- `pretty`
- `output`

### Beispiel

```json
{
  "id": "extract_title",
  "type": "tool",
  "config": {
    "tool": "json.extract",
    "input": "{{last_response}}",
    "path": "items[0].title",
    "pretty": true,
    "output": "first_title"
  }
}
```

## `csv.read`

Liest eine CSV-Datei und gibt sie wahlweise als JSON oder als Texttabelle zurueck.

### Wichtige Felder

- `path`
- `delimiter`
- `has_header`
- `max_rows`
- `output_format`
- `output`

### Werte fuer `output_format`

- `json`
- `text`

### Beispiel

```json
{
  "id": "read_scores",
  "type": "tool",
  "config": {
    "tool": "csv.read",
    "path": "data/scores.csv",
    "delimiter": ";",
    "has_header": true,
    "max_rows": 200,
    "output_format": "json",
    "output": "scores_json"
  }
}
```

## `csv.write`

Schreibt CSV-Dateien entweder aus einem JSON-Zeilenarray oder aus rohem CSV-Text.

### Wichtige Felder

- `path`
- `source_format`
- `content`
- `delimiter`
- `has_header`
- `create_dirs`
- `return_content`
- `output`

### Werte fuer `source_format`

- `rows_json`
- `csv_text`

### Beispiel

```json
{
  "id": "write_scores",
  "type": "tool",
  "config": {
    "tool": "csv.write",
    "path": "artifacts/export/scores.csv",
    "source_format": "rows_json",
    "delimiter": ",",
    "has_header": true,
    "create_dirs": true,
    "content": "[{\"name\":\"Alice\",\"score\":\"42\"},{\"name\":\"Bob\",\"score\":\"39\"}]",
    "output": "csv_write_summary"
  }
}
```

## `directory.read_recursive`

Liest rekursiv Textdateien aus einem Verzeichnis und kombiniert sie zu einem Kontextblock.

### Wichtige Felder

- `path`
- `include_extensions`
- `exclude_paths`
- `include_hidden`
- `skip_binary`
- `max_files`
- `max_chars_per_file`
- `max_total_chars`
- `output`

### Standardverhalten

Wenn `exclude_paths` fehlt, werden standardmaessig unter anderem diese Pfade vermieden:

- `.git`
- `build`
- `node_modules`
- `__pycache__`

### Beispiel

```json
{
  "id": "read_project_tree",
  "type": "tool",
  "config": {
    "tool": "directory.read_recursive",
    "path": "src",
    "include_extensions": ".cpp,.h,.md",
    "exclude_paths": ".git,build,node_modules",
    "max_files": 60,
    "max_chars_per_file": 6000,
    "max_total_chars": 120000,
    "output": "project_snapshot"
  }
}
```

## `directory.read_changed`

Liest nur geaenderte Dateien aus einem Verzeichnis.

### Wichtige Felder

- `path`
- `within_minutes`
- `modified_after_iso`
- `include_extensions`
- `exclude_paths`
- `include_hidden`
- `skip_binary`
- `max_files`
- `max_chars_per_file`
- `max_total_chars`
- `output`

### Pflichtlogik

Mindestens eines dieser Felder muss sinnvoll gesetzt sein:

- `within_minutes`
- `modified_after_iso`

### Beispiel

```json
{
  "id": "read_changes",
  "type": "tool",
  "config": {
    "tool": "directory.read_changed",
    "path": "src",
    "include_extensions": ".cpp,.h,.md",
    "exclude_paths": ".git,build,node_modules",
    "within_minutes": 240,
    "max_files": 20,
    "max_chars_per_file": 6000,
    "max_total_chars": 80000,
    "output": "changed_snapshot"
  }
}
```

## `directory.list`

Listet Dateien und Verzeichnisse ohne deren Inhalt einzulesen.
Das ist praktisch fuer Projektueberblicke, Buchstrukturen oder als leichter Vorab-Schritt vor gezieltem `file.read`.

### Wichtige Felder

- `path`
- `recursive`
- `directories_only`
- `include_extensions`
- `exclude_paths`
- `include_hidden`
- `max_entries`
- `output`

### Beispiel

```json
{
  "id": "list_story_tree",
  "type": "tool",
  "config": {
    "tool": "directory.list",
    "path": "roman",
    "recursive": true,
    "directories_only": false,
    "include_extensions": ".md,.txt",
    "exclude_paths": ".git,build",
    "max_entries": 250,
    "output": "story_tree"
  }
}
```

## `memory.ingest_directory`

Dieses Tool ist der Komfortweg fuer Projekt- oder Textkontext.
Es liest ein Verzeichnis ein und legt das Ergebnis direkt als Memory-Eintrag an.

### Wichtige Felder

- `path`
- `mode`
- `within_minutes`
- `modified_after_iso`
- `include_extensions`
- `exclude_paths`
- `include_hidden`
- `skip_binary`
- `max_files`
- `max_chars_per_file`
- `max_total_chars`
- `entry_type`
- `source`
- `tags`
- `relevance`
- `output`

### Werte fuer `mode`

- `recursive`
- `changed`

### Beispiel fuer Vollimport

```json
{
  "id": "ingest_book_folder",
  "type": "tool",
  "config": {
    "tool": "memory.ingest_directory",
    "path": "roman",
    "mode": "recursive",
    "include_extensions": ".md,.txt",
    "exclude_paths": ".git,build",
    "entry_type": "artifact",
    "tags": "roman,kapitel,kontext",
    "relevance": 85,
    "output": "ingest_summary"
  }
}
```

### Beispiel fuer Delta-Ingest

```json
{
  "id": "ingest_code_delta",
  "type": "tool",
  "config": {
    "tool": "memory.ingest_directory",
    "path": "src",
    "mode": "changed",
    "within_minutes": 240,
    "include_extensions": ".cpp,.h,.md",
    "exclude_paths": ".git,build,node_modules",
    "entry_type": "artifact",
    "tags": "codebase,delta,context",
    "relevance": 90,
    "output": "ingest_summary"
  }
}
```

## `memory.search`

Sucht projektbezogen in der persistenten Erinnerung und liefert Treffer als Snippets oder Volltext.

### Wichtige Felder

- `query`
- `entry_type`
- `tags`
- `limit`
- `max_chars`
- `format`
- `output`

### Werte fuer `format`

- `snippets`
- `full`

### Beispiel

```json
{
  "id": "find_plot_notes",
  "type": "tool",
  "config": {
    "tool": "memory.search",
    "query": "Plot Twist",
    "entry_type": "note",
    "tags": "roman,plot",
    "limit": 8,
    "max_chars": 12000,
    "format": "snippets",
    "output": "plot_memory"
  }
}
```

## `memory.summarize`

Sucht passende Memory-Eintraege, fasst sie mit dem aktuell ausgewaehlten LLM zusammen und kann die Verdichtung wieder als neuen Memory-Eintrag ablegen.

### Wichtige Felder

- `query`
- `entry_type`
- `tags`
- `limit`
- `max_chars`
- `prompt`
- `system_prompt`
- `save_as_memory`
- `summary_entry_type`
- `summary_source`
- `summary_tags`
- `summary_relevance`
- `output`

### Beispiel

```json
{
  "id": "summarize_code_memory",
  "type": "tool",
  "config": {
    "tool": "memory.summarize",
    "query": "Refactor UI",
    "tags": "codebase,ui",
    "limit": 12,
    "max_chars": 24000,
    "save_as_memory": true,
    "summary_entry_type": "summary",
    "summary_tags": "codebase,summary,ui",
    "summary_relevance": 80,
    "output": "memory_summary"
  }
}
```

## `memory.delete_old`

Bereinigt alte Memory-Eintraege projektbezogen.
Hohe Relevanz oder die neuesten Eintraege koennen geschuetzt werden.

### Wichtige Felder

- `older_than_days`
- `keep_latest`
- `keep_relevance_at_or_above`
- `query`
- `entry_type`
- `tags`
- `dry_run`
- `output`

### Beispiel

```json
{
  "id": "cleanup_memory",
  "type": "tool",
  "config": {
    "tool": "memory.delete_old",
    "older_than_days": 45,
    "keep_latest": 20,
    "keep_relevance_at_or_above": 90,
    "dry_run": true,
    "output": "cleanup_preview"
  }
}
```

## `file.write_text`

Schreibt Text direkt in eine Datei.
Das Tool eignet sich fuer Notizen, Exportdateien, Zwischenartefakte oder generierte Assets.

### Wichtige Felder

- `path`
- `content`
- `mode`
- `create_dirs`
- `return_content`
- `output`

### Werte fuer `mode`

- `overwrite`
- `append`

### Beispiel

```json
{
  "id": "write_outline",
  "type": "tool",
  "config": {
    "tool": "file.write_text",
    "path": "artifacts/story/outline.txt",
    "mode": "overwrite",
    "create_dirs": true,
    "content": "{{last_response}}",
    "return_content": false,
    "output": "write_summary"
  }
}
```

## `file.edit_diff`

Wendet einen Unified Diff auf eine Datei an.

### Wichtige Felder

- `diff` oder `patch`
- `path` optional
- `return_content`
- `output`

### Beispiel

```json
{
  "id": "patch_plan",
  "type": "tool",
  "config": {
    "tool": "file.edit_diff",
    "path": "docs/entwicklungsplan_llm_desktop_app.md",
    "return_content": false,
    "diff": "--- a/docs/entwicklungsplan_llm_desktop_app.md\n+++ b/docs/entwicklungsplan_llm_desktop_app.md\n@@ -1,1 +1,1 @@\n-# Alter Titel\n+# Neuer Titel\n",
    "output": "patch_summary"
  }
}
```

## `http.request`

Fuehrt einen allgemeinen HTTP-Request aus.
Damit lassen sich externe APIs, interne Webhooks oder kleine Hilfsdienste in Workflows einbinden.

`http.request` gilt als riskantes Tool und kann projektbezogen bestaetigungspflichtig sein.

### Wichtige Felder

- `url`
- `method`
- `headers_json`
- `body`
- `body_json`
- `timeout_ms`
- `output`

### Hinweise

- Wenn `body_json` gesetzt ist, wird der Body als JSON behandelt.
- `headers_json` erwartet ein JSON-Objekt.
- Bei textuellen Antworten wird standardmaessig der Response-Body direkt als Tool-Ausgabe zurueckgegeben.
- Bei leerem Body wird stattdessen eine kleine JSON-Zusammenfassung erzeugt.

### Beispiel

```json
{
  "id": "call_webhook",
  "type": "tool",
  "config": {
    "tool": "http.request",
    "url": "https://example.org/api/task",
    "method": "POST",
    "headers_json": "{\n  \"Authorization\": \"Bearer {{secret.webhook_token}}\"\n}",
    "body_json": "{\n  \"project\": \"{{project_name}}\",\n  \"summary\": \"{{last_response}}\"\n}",
    "timeout_ms": 30000,
    "output": "webhook_result"
  }
}
```

## `shell.run`

Fuehrt einen bewusst eingeschraenkten lokalen Prozess aus.
Der Schritt nutzt **kein** Shell-Parsing, sondern startet Programme direkt mit `QProcess`.
Auch dieses Tool kann pro Projekt bestaetigungspflichtig sein.

### Wichtige Felder

- `command`
- `working_directory`
- `timeout_ms`
- `max_output_chars`
- `include_stderr`
- `output`

### Sicherheitsmodell

- Nur eine kleine Positivliste ist erlaubt: aktuell `rg`, `git`, `cmake`, `ctest`, `where`
- Fuer `git` sind nur sichere Unterbefehle freigegeben, zum Beispiel `status`, `diff`, `show`, `log`, `branch`, `rev-parse`, `ls-files`, `grep`
- Shell-Ketten, Pipes und Umleitungen werden blockiert
- Das Arbeitsverzeichnis bleibt innerhalb des Workspace

### Beispiel

```json
{
  "id": "show_git_status",
  "type": "tool",
  "config": {
    "tool": "shell.run",
    "command": "git status --short",
    "working_directory": ".",
    "timeout_ms": 10000,
    "max_output_chars": 12000,
    "include_stderr": true,
    "output": "git_status"
  }
}
```

## `comfyui.workflow`

Dieses Tool startet einen Bildworkflow in `ComfyUI`.

Es gibt zwei Nutzungsarten:

- Formular-/Builder-Modus
- Rohes Workflow-JSON

### Builder-Modi

Aktuell unterstuetzt:

- `txt2img`
- `img2img`
- `inpainting`
- `raw_json` als Editor-Modus im UI

### Typische Builder-Felder

- `builder_mode`
- `checkpoint`
- `positive_prompt`
- `negative_prompt`
- `width`
- `height`
- `batch_size`
- `steps`
- `seed`
- `randomize_seed`
- `cfg`
- `denoise`
- `sampler_name`
- `scheduler`
- `clip_skip`
- `filename_prefix`
- `save_outputs_to`
- `download_images`
- `include_history_json`
- `poll_interval_ms`
- `timeout_ms`
- `output`

Zusaetzlich je nach Modus:

- `image` oder lokaler Bildpfad fuer `img2img`
- `mask_image`
- `mask_channel`
- `mask_grow`

### Beispiel `txt2img`

```json
{
  "id": "render_image",
  "type": "tool",
  "config": {
    "tool": "comfyui.workflow",
    "builder_mode": "txt2img",
    "checkpoint": "REPLACE_WITH_YOUR_CHECKPOINT",
    "positive_prompt": "{{positive_prompt}}",
    "negative_prompt": "{{negative_prompt}}",
    "width": 1024,
    "height": 1024,
    "batch_size": 1,
    "steps": 28,
    "seed": 0,
    "randomize_seed": true,
    "cfg": 6.5,
    "denoise": 1.0,
    "sampler_name": "euler",
    "scheduler": "normal",
    "clip_skip": -1,
    "filename_prefix": "PrivateClaw/example_prompt_pipeline",
    "download_images": true,
    "save_outputs_to": "artifacts/comfy/example_prompt_pipeline",
    "include_history_json": false,
    "poll_interval_ms": 1500,
    "timeout_ms": 0,
    "output": "render_summary"
  }
}
```

### Beispiel mit rohem Workflow-JSON

```json
{
  "id": "render_raw",
  "type": "tool",
  "config": {
    "tool": "comfyui.workflow",
    "workflow_json": "{\n  \"3\": { \"class_type\": \"KSampler\", \"inputs\": { ... } }\n}",
    "save_outputs_to": "artifacts/comfy/raw",
    "download_images": true,
    "output": "render_summary"
  }
}
```

## Tool-Ketten in der Praxis

### Kontext fuer Programmieraufgaben aufbauen

1. `memory.ingest_directory` auf `src`
2. `prompt` fuer Analyse oder Planung
3. `directory.list` fuer einen schnellen Strukturueberblick
4. `file.read` fuer gezielte Stellen
5. `memory.search` fuer vorhandene Projekterkenntnisse
6. `file.edit_diff` fuer kontrollierte Aenderungen
7. `file.write_text` fuer Reports, Artefakte oder Exportdateien

### Kontext fuer Schreibprojekte

1. `memory.ingest_directory` auf ein Kapitelverzeichnis
2. `memory.search` fuer Figuren-, Plot- oder Stilnotizen
3. `prompt` fuer Stilabgleich oder Zusammenfassung
4. `file.write_text` fuer Exporte oder Kapitelentwuerfe
5. `save_memory` fuer neue Erkenntnisse

### Memory-Pflege

1. `memory.search` fuer bestehende relevante Eintraege
2. `memory.summarize` fuer Verdichtung
3. `memory.delete_old` im `dry_run`
4. `memory.delete_old` ohne `dry_run`, wenn die Vorschau passt

### Bildworkflow

1. positiver Prompt per `prompt`
2. negativer Prompt per `prompt`
3. `comfyui.workflow`
4. `save_memory` fuer Prompt-Paar und Render-Zusammenfassung

## Best Practices fuer Tools

- Setze immer `output`, wenn ein Folge-Schritt das Ergebnis weiterverwendet.
- Nutze `json.extract`, wenn ein Modell oder eine API JSON liefert und du nur einzelne Teile brauchst.
- Nutze `csv.read` und `csv.write`, wenn Daten zwischen LLM, Datei und Tooling ausgetauscht werden muessen.
- Begrenze Dateikontexte mit `max_files` und `max_total_chars`.
- Nutze `directory.list`, wenn du erst die Struktur verstehen willst und noch keinen Volltext brauchst.
- Nutze `memory.search` vor neuen Prompt-Schritten, wenn projektbezogenes Vorwissen relevant ist.
- Nutze `memory.summarize` und `memory.delete_old`, um dein Projektgedaechtnis regelmaessig schlank zu halten.
- Nutze `memory.ingest_directory`, wenn du Projektwissen langfristig aufbauen willst.
- Nutze `file.write_text` fuer klar definierte Zielartefakte statt Antworten nur im Run-Log zu lassen.
- Verwende `file.edit_diff` statt unstrukturierter Dateischreibaktionen.
- Nutze `http.request` fuer einfache API-Anbindungen, wenn dafuer noch kein spezialisiertes Tool existiert.
- Nutze `shell.run` nur fuer die bewusst freigegebenen lokalen Lese-, Build- und Testkommandos.
- Nutze fuer `ComfyUI` bevorzugt den Builder-Modus, solange du keinen Spezialgraph brauchst.
