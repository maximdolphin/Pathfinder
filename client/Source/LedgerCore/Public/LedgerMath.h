// Constants that have to be double precision, because Unreal's are not.
//
// `PI`, `SMALL_NUMBER` and the rest of `UnrealMathUtility.h` are **float**
// constants. Written into a double expression they widen the float value rather
// than producing the double one, and the compiler says nothing, because
// float-to-double is the promotion that is normally safe.
//
// `2.0 * PI` is 6.2831854820251465. Two pi is 6.283185307179586. That gap --
// 1.7e-7 relative, single-precision magnitude in code that is double precision
// everywhere else -- turned this system's planet 1.1 metres past a full
// rotation, against a millimetre acceptance. See docs/coding-standards.md,
// "Precision", and `Ledger.Frames.TheBodyFrameActuallyRotates`, which is the
// test that found it.
//
// Its own header rather than a corner of LedgerBody.h so that terrain, material
// and harness code can have a correct pi without also having a description of a
// planet.

#pragma once

inline constexpr double LedgerPi = 3.14159265358979323846;
inline constexpr double LedgerTwoPi = 2.0 * LedgerPi;
