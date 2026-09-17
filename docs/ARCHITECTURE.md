# Architektur und Datenfluss

Dieses Dokument beschreibt den technischen Aufbau der Hauptanwendung `V3MarchingCubes`.

Die mathematischen Hintergründe zu DQ-Modellen, Collision, Streaming und Randbedingungen sind unter [Methodische Grundlagen](THEORY.md) zusammengefasst.

## Überblick

Die Anwendung verbindet drei Aufgaben in einer Direct3D-12-Pipeline:

1. Import und Darstellung einer statischen 3D-Szene
2. Aktualisierung eines Lattice-Boltzmann-Gitters auf der GPU
3. Rekonstruktion und Rendering der freien Fluidoberfläche

Die Simulationsdaten bleiben während des laufenden Betriebs auf der GPU. Die CPU erstellt Ressourcen, aktualisiert Parameter, zeichnet Command Lists auf und liest ausgewählte Messwerte für die Benutzeroberfläche zurück.

## Komponenten

### Anwendungssteuerung

`SceneGraphViewerApp` ist der zentrale Orchestrator. Die Klasse verwaltet:

- DirectX-12-Root-Signatures und Pipeline-State-Objects,
- Render Targets, Depth Buffer und Descriptor Heaps,
- strukturierte LBM-Puffer und deren Ping-Pong-Zustand,
- Marching-Cubes-Lookup-Tabellen,
- ImGui-Konfiguration sowie
- GPU-Timestamps, VRAM-Abfragen und Dreiecksstatistik.

Die Hilfsfunktionen für Fenster, Device, Swapchain und Command Queue liegen in `gimslib/`.

### Szenenimport und Rendering

`SceneFactory` importiert glTF-Szenen über Assimp und erzeugt Nodes, Meshes, Materialien und Texturen. `Scene` traversiert den Szenengraphen und reicht die Draw Calls ein. `TriangleMeshD3D12`, `Texture2DD3D12` und `ConstantBufferD3D12` kapseln die zugehörigen GPU-Ressourcen.

### 2D-LBM

Die 2D-Variante verwendet ein D2Q9-Gitter. Neun diskrete Geschwindigkeitsrichtungen beschreiben die Verteilungsfunktionen einer Zelle. `LBM.hlsl` berechnet den Kollisionsschritt, `LBMStreaming.hlsl` transportiert die Verteilungen in benachbarte Zellen. Diese Variante wurde anhand einer Kármánschen Wirbelstraße als Vorstufe der 3D-Implementierung eingesetzt.

### 3D-LBM

Die 3D-Simulation verwendet ein D3Q19-Gitter. Der Zustand einer Zelle umfasst 19 Verteilungsfunktionen sowie Dichte `rho`, Geschwindigkeit `u`, Masse `m` und Füllgrad `epsilon`.

| Shader | Aufgabe |
| --- | --- |
| `LBM3DCommon.hlsl` | D3Q19-Richtungen, Gewichte, Zelltypen und Hilfsfunktionen |
| `LBM3D.hlsl` | Randbedingungen, Gleichgewichtsverteilung, BGK-Collision und Guo-Kraftterm |
| `LBM3DStreaming.hlsl` | Streaming und Aktualisierung makroskopischer Größen |
| `LBM3D_UpdateCellTypes.hlsl` | Übergänge zwischen Fluid-, Interface- und Leerzellen |

Zwei GPU-Puffer werden abwechselnd als Quelle und Ziel gebunden. UAV-Barriers stellen zwischen den Dispatches sicher, dass Schreibzugriffe für den jeweils folgenden Pass sichtbar sind.

### Zelltypen

Der separate Zelltyp-Puffer unterscheidet:

- Empty
- Interface
- Fluid
- Obstacle
- Wall
- Inflow
- Outflow

Nach Collision und Streaming klassifiziert ein dritter Compute-Pass die dynamischen Zellen anhand ihrer aktuellen Masse. Statische Randzellen werden dabei nicht überschrieben.

### Marching Cubes

`MarchingLBM.hlsl` liest acht Eckwerte pro Gitterzelle, bestimmt den Marching-Cubes-Fall und erzeugt mithilfe von Edge- und Triangle-Lookup-Tabellen die Dreiecke der Isofläche. Die Geometrie entsteht im Mesh Shader und wird direkt an die Rasterizer-Stufe weitergegeben. Ein vollständiges dynamisches Dreiecksnetz muss daher weder auf der CPU aufgebaut noch zur GPU übertragen werden.

### Debugging und Telemetrie

Die Anwendung kann Schnittebenen durch das 3D-Gitter anzeigen. Separate Shader visualisieren Zelltyp, Dichte, Masse und Füllgrad. Timestamp Queries messen Collision, Streaming, Cell-Type Update, Marching Cubes und weitere Abschnitte. Angezeigt werden außerdem MLUPS, VRAM-Nutzung sowie aktuelle, durchschnittliche und maximale Dreieckszahlen.

## Datenfluss eines Frames

```text
CPU / ImGui
  |  aktualisiert Constant Buffer und Simulationsparameter
  v
3D-LBM Collision
  |  Grid A -> Grid B
  v
3D-LBM Streaming
  |  Grid B -> Grid A
  v
Zelltyp-Update
  |  Interface-/Fluid-/Empty-Übergänge
  v
Marching-Cubes-Mesh-Shader
  |  liest aktuelles Gitter und Lookup-Tabellen
  v
Rasterisierung der Fluidoberfläche
  |
  +--> optionale Debug-Ebene
  +--> importierte Szene
  `--> Postprocessing und Präsentation
```

Resource Barriers wechseln die D3D12-Ressourcen zwischen UAV-, SRV-, Render-Target- und Present-Zuständen. Die Reihenfolge wird in der Command List explizit festgelegt.

## Performance-Messung

Ein `ID3D12QueryHeap` vom Typ `D3D12_QUERY_HEAP_TYPE_TIMESTAMP` erfasst Zeitstempel nach den relevanten GPU-Passes. Die Daten werden mit einer Frame Verzögerung über einen Readback-Buffer ausgewertet. Der LBM-Durchsatz wird als Million Lattice Updates per Second berechnet:

```text
MLUPS = Nx * Ny * Nz / (1,000,000 * tLBM)
tLBM  = tCollision + tStreaming
```

Zusätzlich protokolliert die Anwendung die vom Mesh Shader erzeugte Dreiecksanzahl und das lokale VRAM-Budget.

## Wichtige Verzeichnisse

| Pfad | Inhalt |
| --- | --- |
| `Assignments/V3MarchingCubes/src/` | Ablaufsteuerung und Ressourcenverwaltung |
| `Assignments/V3MarchingCubes/include/` | Klassen, UI-Daten und CPU-seitige GPU-Strukturen |
| `Assignments/V3MarchingCubes/shaders/` | Simulation, Oberflächenextraktion und Rendering |
| `gimslib/` | gemeinsames DirectX-12-Framework |
| `data/` | Szenen und Texturen |

## Mögliche Weiterentwicklungen

Die Bachelorarbeit nennt insbesondere:

- GPU Work Graphs für adaptives Work Dispatching,
- Cache-Optimierungen und bessere Datenlayouts,
- Fusion von Collision und Streaming,
- prozedurale Resurfacing-Techniken im Mesh Shader,
- Machine-Learning-Surrogatmodelle und Detailrekonstruktion sowie
- Dual Marching Cubes für topologisch konsistente Quad-Meshes.

Zusätzlich würden numerische Referenzfälle und GPU-/CPU-Vergleichstests die wissenschaftliche Validierung verbessern. Der fest codierte Szenenpfad könnte durch Kommandozeilenargumente oder eine Konfigurationsdatei ersetzt werden.
