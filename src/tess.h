/* Sector Forge - polygon triangulation (odd winding, holes, multiple outlines).
   Used on platforms without GLU (Android) and by the test harness. */
#ifndef SF_TESS_H
#define SF_TESS_H
#include "base.h"

/* xy: 2 doubles per point. loop_start: nloops+1 offsets (in points) into xy.
   Appends triangles to out as x,y float pairs (6 floats per triangle). */
void tess_odd(const double *xy, const int *loop_start, int nloops, FloatArr *out);

#endif
