# Workflow-Beispiele

## ComfyUI Prompt Pipeline

Datei:

`comfy_prompt_pipeline_workflow.json`

Zweck:

- erzeugt zuerst einen positiven Bildprompt per LLM
- erzeugt danach einen negativen Bildprompt per LLM
- speichert das Prompt-Paar optional im Projekt-Memory
- rendert anschliessend ein Bild ueber `comfyui.workflow`
- speichert die Render-Zusammenfassung ebenfalls im Memory

Vor der Ausfuehrung bitte anpassen:

- `checkpoint` im `comfyui.workflow`-Schritt auf einen in deiner ComfyUI-Instanz verfuegbaren Checkpoint setzen
- optional `width`, `height`, `steps`, `cfg` und `filename_prefix`

Hinweis:

Der Beispiel-Workflow nutzt den neuen Formular-/Builder-Modus von `comfyui.workflow`.
Falls du spaeter etwas Komplexeres brauchst, kannst du denselben Schritt im Editor jederzeit auf `Rohes Workflow-JSON` umstellen.

Prompt-Ausgaben werden beim Workflow-Lauf automatisch von typischen Reasoning-Tags wie `<think>...</think>` bereinigt.
Fuer sichtbare Folge-Schritte wie `{{last_response}}` oder `{{positive_prompt}}` wird nur die bereinigte Ausgabe verwendet.
Falls du das extrahierte Reasoning bewusst separat speichern willst, stehen zusaetzlich Variablen wie `{{last_reasoning}}` oder `{{positive_prompt_reasoning}}` zur Verfuegung.

## Buchautor Szene Workflow

Datei:

`buchautor_szene_workflow.json`

Zweck:

- restauriert Kapitel- und Szenenstand aus einem Fortschritts-Memory
- nutzt `variables.set` fuer Titel, Dateipfade, Labels und Zaehler
- bootstrappt Hauptcharakter-Memories nur dann, wenn noch keine vorhanden sind
- schreibt eine neue Szene und exportiert sie als Markdown-Datei
- legt bei Bedarf gezielte Update-Memories fuer Nebencharaktere, Orte und plotrelevante Ereignisse an
- speichert am Ende den naechsten Kapitel-/Szenenstand wieder als Fortschritts-Memory

Hinweis:

Der Workflow legt Erinnerungen fuer neue Figuren, Orte und Ereignisse als kategorisierte Update-Notizen an.
Solange es noch keine Schleifen oder Foreach-Schritte gibt, ist das der robusteste Weg, mehrere neue Entitaeten in einem Run gesammelt festzuhalten.
