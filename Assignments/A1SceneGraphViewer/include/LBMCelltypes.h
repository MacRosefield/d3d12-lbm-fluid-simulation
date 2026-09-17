#pragma once

#include <gimslib/types.hpp>

enum CellType : gims::ui32
{
  Cell_Fluid    = 0,
  Cell_Inflow   = 1,
  Cell_Outflow  = 2,
  Cell_Obstacle = 3,
  Cell_Wall     = 4
};