# Theoretical Background

This page summarizes the theoretical foundations presented in the accompanying presentation and bachelor's thesis. It provides the scientific context for the data structures and shader passes used in the project.

## Classification of the Lattice Boltzmann Method

Fluid flows can be described at different scales:

| Scale | Perspective | Typical model |
| --- | --- | --- |
| microscopic | individual particles and their interactions | molecular dynamics, Newtonian mechanics |
| mesoscopic | statistical distribution of particle ensembles | Boltzmann equation and LBM |
| macroscopic | directly observable quantities such as pressure and velocity | Navier-Stokes equations |

The Lattice Boltzmann Method operates at the mesoscopic scale. A lattice node does not represent an individual particle, but a local particle distribution. Macroscopic quantities such as density and velocity are reconstructed from the distribution functions. The local operations and regular memory-access patterns are well suited to execution across many GPU threads.

## DQ Models

The notation `DdQq` describes a discrete velocity model:

- `d` specifies the number of spatial dimensions.
- `q` specifies the number of discrete velocity directions.

The 2D validation in this project uses D2Q9. The main simulation uses D3Q19 and therefore stores 19 distribution functions per lattice cell.

![D3Q15, D3Q19, and D3Q27 velocity models](images/dq-3d-models.png)

*Comparison of three-dimensional DQ models. Illustration source: Krüger et al. (2017), reproduced from the accompanying presentation.*

## Collision and Streaming

An LBM iteration consists of two main phases:

1. **Collision:** The local distribution functions relax toward an equilibrium distribution. This project uses the BGK operator. The Guo forcing term adds the effect of gravity.
2. **Streaming:** The updated distributions propagate to neighboring lattice cells along their discrete velocity directions.

![Schematic collision and streaming steps of the Lattice Boltzmann Method](images/lbm-collision-streaming.png)

*Schematic sequence of collision and streaming. Illustration source: Krüger et al. (2017), reproduced from the accompanying presentation.*

In the GPU implementation, both steps operate on two structured buffers. Collision reads the current state and writes to the second buffer. Streaming then binds the buffers in the opposite direction. UAV barriers ensure that the results are visible between dispatches.

## Simulation Iteration

The computational process can be summarized as follows:

1. Initialize the distribution functions.
2. Determine density and velocity from the distributions.
3. Calculate the equilibrium distribution.
4. Perform collision with relaxation and external forces.
5. Stream the distributions to neighboring cells.
6. Apply boundary conditions.
7. Update cell types according to mass and fill level.
8. Begin the next time step.

After the compute passes, the application evaluates the Marching Cubes surface and renders the resulting isosurface.

## Boundary Conditions

Boundary conditions define how distributions are handled at walls, obstacles, inflow boundaries, and outflow boundaries.

- **Bounce-back / no-slip:** Distributions traveling toward a solid wall are reflected in the opposite direction. The velocity at the wall is zero.
- **Inflow:** Prescribed density and velocity values introduce fluid into the simulation domain.
- **Outflow:** Distributions leave the domain without being reflected at the boundary.
- **Obstacle and wall:** These cells represent solid geometry and do not participate in the regular fluid update.

![Inflow and outflow boundary conditions in a discrete flow lattice](images/lbm-inflow-outflow.png)

*Conceptual illustration of the inflow and outflow boundary conditions from the accompanying presentation.*

## Visualization of Physical Quantities

Before the three-dimensional Marching Cubes visualization was implemented, several 2D visualizations were used to validate the LBM implementation. These include color maps for scalar quantities and vector fields for the flow direction.

![Comparison of a scalar-field visualization and a velocity vector field](images/lbm-visualization-fields.png)

*Examples of a scalar-field view and a velocity field from the accompanying presentation.*

The main application extends this principle to three dimensions. Density, mass, fill level, and cell type can be inspected on a debug slice. For the visible surface, Marching Cubes evaluates the scalar field across the entire 3D lattice.

## Reference

The source marked as `[TIM+2017]` in two of the figures is:

> Timm Krüger, Halim Kusumaatmaja, Alexandr Kuzmin, Orest Shardt, Gonçalo Silva, and Erlend Magnus Viggen: *The Lattice Boltzmann Method: Principles and Practice*. Springer International Publishing, 2017.

Before public distribution, the usage rights for all figures reproduced from presentation sources should be reviewed.
