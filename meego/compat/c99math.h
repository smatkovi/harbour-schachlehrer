// Force-included into the Harmattan builds.
//
// The GCC 14 cross toolchain was configured against Harmattan's glibc 2.10,
// and its libstdc++ came out without the C99 maths functions in namespace
// std: ::round exists, std::round does not, and the same holds for trunc,
// rint, log2, hypot, copysign, fmin/fmax and the rest. Only the C++98
// overloads (abs, pow, log, exp, sqrt, floor, ceil) are there.
//
// Pulling the global names in is the usual remedy; it is done only where the
// name is genuinely absent, so a toolchain that has them is left alone.
#pragma once
#include <cmath>

#if defined(__GLIBCXX__) && !defined(_GLIBCXX_USE_C99_MATH_TR1)
namespace std {
// No long double variants: on ARM glibc 2.10 long double is double and the
// *l names are not declared at all (roundl is the one that shows up).
using ::round;   using ::roundf;
using ::lround;  using ::llround;
using ::trunc;   using ::rint;     using ::nearbyint;
using ::log2;    using ::exp2;     using ::expm1;    using ::log1p;
using ::cbrt;    using ::hypot;    using ::copysign;
using ::fmin;    using ::fmax;     using ::fma;
using ::remainder;
}
#endif
