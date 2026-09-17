# Fehlerbehebung

## CMake findet `glm`, `imgui` oder `assimp` nicht

Das vcpkg-Toolchain-File wurde nicht eingebunden oder `VCPKG_ROOT` zeigt auf das falsche Verzeichnis. Die Konfiguration benötigt dieses Argument:

```powershell
-DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"
```

Prüfen Sie außerdem, ob vcpkg erfolgreich gebootstrapped wurde. Die Abhängigkeiten werden beim Konfigurieren anhand von `vcpkg.json` installiert.

## NuGet-Download schlägt fehl

Beim ersten CMake-Lauf werden DirectX Agility SDK und DXC von NuGet geladen. Prüfen Sie Internetzugang, Proxy- und Firewall-Einstellungen. Bei einem unvollständigen Download kann der betroffene Paketordner unter `<Build-Verzeichnis>/nuget/` entfernt und die Konfiguration erneut gestartet werden.

## `Mesh Shader not supported on this hardware or driver`

`V2BoltzmannKarmann` und `V3MarchingCubes` prüfen die Mesh-Shader-Unterstützung zur Laufzeit. Aktualisieren Sie Windows und den GPU-Treiber. Unterstützt die Hardware Mesh Shader Tier 1 nicht, können diese Varianten nicht vollständig ausgeführt werden; ältere Anwendungen und Tutorials benötigen diese Funktion nicht durchgängig.

## Die Anwendung findet eine glTF-Datei nicht

Die Beispielanwendungen verwenden relative Pfade wie `../../../data/NobleCraftsman/scene.gltf`. Starten Sie das Programm aus dem erzeugten `bin`-Verzeichnis. Bei einer anderen Build-Struktur muss der Pfad in `Assignments/<Variante>/src/main.cpp` angepasst werden.

## Shader-Kompilierung schlägt fehl

Prüfen Sie:

- ob `Microsoft.Direct3D.DXC` vollständig geladen wurde,
- ob `dxcompiler.dll` neben der Anwendung verfügbar ist,
- ob GPU und Treiber Shader Model 6.5 unterstützen und
- ob der Build aus einer aktuellen MSVC-Developer-Shell gestartet wurde.

Die ausführliche DXC-Fehlermeldung wird in der Konsole ausgegeben.

## Schwarzes Bild oder keine Fluidoberfläche

- Aktivieren Sie unter **Configuration** `Active Simulation` und `Display MESH`.
- Setzen Sie die Werte über `Defaults` zurück.
- Prüfen Sie Inflow, Outflow und `LBM Tau` auf gültige Werte.
- Warten Sie nach einer Neuinitialisierung einige Frames.
- Verwenden Sie die Debug-Ebene, um Zelltyp, Dichte, Masse oder Füllgrad zu kontrollieren.

## Instabile Simulation oder ungültige Werte

Zu hohe Strömungsgeschwindigkeiten, eine ungeeignete Relaxationszeit oder viele Simulationsschritte pro Frame können die numerische Stabilität beeinträchtigen. Beginnen Sie mit den Default-Werten, verändern Sie jeweils nur einen Parameter und beobachten Sie Debug-Ansichten und GPU-Zeiten.

## Build bricht wegen Warnungen ab

Das Projekt kompiliert mit `/W4 /WX`; Warnungen werden als Fehler behandelt. Beheben Sie die zuerst gemeldete Warnung oder prüfen Sie, ob sie durch eine abweichende Compiler-Version verursacht wird. Das lokale Deaktivieren von `/WX` kann für die Diagnose hilfreich sein, sollte aber keine dauerhafte Lösung sein.
