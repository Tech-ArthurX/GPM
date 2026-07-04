CGSI <math.h>
CGSB
static double gs_square(double x) {
  return x * x;
}
CGSE
SETV X = 12
CGSC double Y = gs_square(X);
LOGS INFO, cgs block inserted