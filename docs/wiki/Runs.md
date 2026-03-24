# Runs

Diese Seite beschreibt die persistente Run-Historie in `PrivateClaw`.

## Ueberblick

Jeder manuelle oder geplante Workflow-Lauf kann als Run gespeichert werden.
Damit lassen sich vergangene Ausfuehrungen spaeter nachvollziehen, auch wenn das Live-Protokoll unten in der App laengst weitergelaufen ist.

Die Run-Historie ist vor allem hilfreich fuer:

- Fehlersuche nach fehlgeschlagenen Workflows
- Vergleich mehrerer Ausfuehrungen desselben Workflows
- Nachvollziehen geplanter Zeitplan-Laeufe
- Pruefen von Ausgabe, Modellwahl und gespeicherten Memory-Eintraegen

## Wo die Historie zu finden ist

In der linken Navigation gibt es den Bereich `Runs`.

Dort zeigt die App:

- eine projektbezogene Liste gespeicherter Laeufe
- Status und Startzeit pro Lauf
- die gespeicherte Ausgabe
- die gespeicherten Logs
- Metadaten wie Provider, Modell, Herkunft und Memory-Anzahl

## Welche Laeufe gespeichert werden

Aktuell werden gespeichert:

- manuelle Workflow-Starts aus dem Workflow-Editor
- manuelle Starts aus einem Zeitplan
- automatische Zeitplan-Ausfuehrungen

## Gespeicherte Informationen

Ein Run enthaelt unter anderem:

- Projekt-ID
- Workflow-ID
- Status
- Herkunft des Laufs
- Provider
- Modell
- Kurz-Zusammenfassung
- Ausgabe
- Logs
- Fehlermeldung
- Anzahl der waehrend des Laufs gespeicherten Memory-Eintraege
- Start- und Endzeit

## Statuswerte

Typische Statuswerte sind:

- `running`
- `completed`
- `failed`

Hinweis:
Ein Run kann auch dann in der Historie auftauchen, wenn das eigentliche UI-Live-Protokoll schon weitergelaufen ist.

## Herkunft eines Runs

Die Herkunft zeigt, wie der Lauf gestartet wurde.
Aktuell kommen vor:

- `manual`
- `schedule:manual`
- `schedule:auto`

Damit ist schnell sichtbar, ob ein Lauf direkt vom Benutzer oder durch die Zeitplanung gestartet wurde.

## Detailansicht

Nach Auswahl eines Runs zeigt die rechte Seite:

- Status
- Herkunft
- Projekt
- Workflow
- Provider
- Modell
- Startzeit
- Endzeit
- Anzahl gespeicherter Memory-Eintraege
- Zusammenfassung
- Fehlertext

Zusätzlich gibt es zwei Bereiche:

- `Ausgabe`
- `Logs`

## Unterschied zum Run-Protokoll unten

Das untere `Run-Protokoll` in der App ist der Live-Bereich fuer aktuelle Meldungen.
Die Run-Historie ist dagegen die persistente, spaeter wieder oeffenbare Speicherung.

Kurz gesagt:

- `Run-Protokoll` = live und fortlaufend
- `Runs` = dauerhaft gespeichert

## Typische Nutzung

Ein sinnvoller Ablauf ist oft:

1. Workflow starten
2. Lauf im unteren Protokoll beobachten
3. Danach in `Runs` den abgeschlossenen Lauf oeffnen
4. Ausgabe und Logs vergleichen
5. Bei Bedarf den Workflow anpassen und erneut ausfuehren

## Was aktuell noch nicht drin ist

Der aktuelle Stand ist bewusst als MVP gehalten.
Noch nicht eingebaut sind unter anderem:

- eigene Volltextsuche ueber Runs
- Export einzelner Runs
- Loeschen einzelner Run-Eintraege aus der UI
- diff-basierter Vergleich zweier Runs
- separate Artefaktvorschau fuer erzeugte Dateien oder Bilder

## Zusammenhang mit Zeitplaenen

Wenn ein Zeitplan einen Workflow startet, wird ebenfalls ein Run erzeugt.
So bleibt nachvollziehbar:

- wann der Lauf gestartet wurde
- ob er erfolgreich war
- welche Ausgabe entstanden ist
- ob Memory gespeichert wurde

Die Details zur Planung selbst stehen in [Zeitplaene](./Zeitplaene.md).
