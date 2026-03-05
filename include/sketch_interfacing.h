#pragma once
#include "sketch.h"
#include "sketch/sketch_columns.h"
#include "sketch/sketch_concept.h"


extern vec_t sketch_len;
extern vec_t sketch_err;

// using DefaultSketchColumn = FixedSizeSketchColumn;
using DefaultSketchColumn = ResizeableSketchColumn;
