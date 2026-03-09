#pragma once
#include "sketch/sketch_columns.h"
#include "sketch/sketch_concept.h"


extern vec_t sketch_len;
extern vec_t sketch_err;

#ifdef USE_RESIZEABLE_SKETCH
using DefaultSketchColumn = ResizeableSketchColumn;
#else
using DefaultSketchColumn = FixedSizeSketchColumn;
#endif
