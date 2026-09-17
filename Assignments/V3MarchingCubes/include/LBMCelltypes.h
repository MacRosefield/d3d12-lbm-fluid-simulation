#pragma once

#include <gimslib/types.hpp>

enum CellType : gims::ui32
{
  CELL_EMPTY     = 0,
  CELL_INTERFACE = 1,
  CELL_FLUID     = 2,
  CELL_OBSTACLE  = 3,
  CELL_WALL      = 4,
  CELL_INFLOW    = 5,
  CELL_OUTFLOW   = 6
};