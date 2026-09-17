# Troubleshooting

## CMake Cannot Find `glm`, `imgui`, or `assimp`

The vcpkg toolchain file was not included, or `VCPKG_ROOT` points to the wrong directory. Configuration requires the following argument:

```powershell
-DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"
```

Also verify that vcpkg was bootstrapped successfully. The dependencies defined in `vcpkg.json` are installed during configuration.

## NuGet Download Fails

The first CMake run downloads the DirectX Agility SDK and DXC from NuGet. Check the internet connection and the proxy and firewall settings. If a download is incomplete, remove the affected package directory under `<build-directory>/nuget/` and run the configuration again.

## `Mesh Shader not supported on this hardware or driver`

`V3MarchingCubes` checks mesh-shader support at runtime. Update Windows and the GPU driver. If the hardware does not support Mesh Shader Tier 1, the application cannot run with its complete feature set.

## The Application Cannot Find a glTF File

The application uses relative paths such as `../../../data/NobleCraftsman/scene.gltf`. Start the program from the generated `bin` directory. If the build uses a different directory structure, adjust the path in `Assignments/V3MarchingCubes/src/main.cpp`.

## Shader Compilation Fails

Verify that:

- `Microsoft.Direct3D.DXC` was downloaded completely,
- `dxcompiler.dll` is available next to the application,
- the GPU and driver support Shader Model 6.5, and
- the build was started from a current MSVC developer shell.

The complete DXC error message is printed to the console.

## Black Screen or No Fluid Surface

- Enable `Active Simulation` and `Display MESH` under **Configuration**.
- Restore the settings with `Defaults`.
- Check that Inflow, Outflow, and `LBM Tau` contain valid values.
- Wait for several frames after reinitializing the simulation.
- Use the debug slice to inspect cell type, density, mass, or fill level.

## Unstable Simulation or Invalid Values

Excessive flow velocities, an unsuitable relaxation time, or too many simulation steps per frame can affect numerical stability. Begin with the default values, change one parameter at a time, and monitor the debug views and GPU timings.

## Build Fails Because of Warnings

The project compiles with `/W4 /WX`, which treats warnings as errors. Resolve the first reported warning or determine whether it is caused by a different compiler version. Disabling `/WX` locally can help with diagnosis, but should not be used as a permanent solution.
