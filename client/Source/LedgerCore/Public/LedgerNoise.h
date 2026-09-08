// Coherent noise. Gradient, fractal, ridged, and the eroded variants that carry
// their own derivatives.
//
// **These live in LedgerCore rather than with the terrain.** The terrain is the
// largest consumer but not the only one: the material module generates its
// detail textures from the same primitives, and the terrain module depends on
// the material module. Leaving the noise with the terrain made that a cycle,
// and a cycle is never the answer to a layering question — the shared thing
// moves down, it does not get an extra edge.
//
// Every function here is pure and deterministic in its seed, which is what lets
// the same value be computed on a worker thread, in a texture-generation pass
// and in a headless test, and agree in all three.
//
// Floats throughout: this is the presentation layer, and design §5.2 forbids
// floating point only in *authoritative* state. A planet's shape is derived
// from a seed, so it is reproducible without being part of the sim's fold.

#pragma once

#include "CoreMinimal.h"

namespace LedgerNoise
{
	/// Deterministic gradient noise in 3d. Hash-based, so the same seed and
	/// position give the same value on every machine — which matters because the
	/// simulation will eventually place things on this surface by coordinate.
	///
	/// Gradient rather than value noise: value noise has visible axis-aligned
	/// structure that survives any amount of octave stacking, and on a planet it
	/// reads as a grid pressed into the continents.
	LEDGERCORE_API double Gradient(const FVector3d& Position, uint32 Seed);

	/// Fractal Brownian motion. Returns roughly `[-1, 1]`.
	LEDGERCORE_API double Fractal(
		const FVector3d& Position,
		uint32 Seed,
		int32 Octaves,
		double Lacunarity = 2.02,
		double Gain = 0.5);

	/// Ridged multifractal: folds each octave about zero and weights the next by
	/// the last, so ridges reinforce into connected ranges instead of scattering
	/// into isolated bumps. This is what makes mountains look like mountains.
	LEDGERCORE_API double Ridged(
		const FVector3d& Position,
		uint32 Seed,
		int32 Octaves,
		double Lacunarity = 2.03,
		double Gain = 0.5);

	/// A noise value together with its analytic gradient.
	struct FSample
	{
		double Value = 0.0;
		FVector3d Derivative = FVector3d::ZeroVector;
	};

	/// Gradient noise and its exact derivative, from the same eight corners.
	/// Finite differences would cost four evaluations and be wrong at the octave
	/// boundaries; this is one evaluation and exact.
	LEDGERCORE_API FSample GradientWithDerivative(const FVector3d& Position, uint32 Seed);

	/// Erosion, done analytically.
	///
	/// Real hydraulic erosion is an iterative simulation over a height field, and
	/// a quadtree cannot run one: patches are generated independently, at
	/// different times, at different resolutions, so any regional simulation
	/// produces seams at every LOD boundary. What it *can* do is the effect
	/// erosion has on the shape.
	///
	/// Each octave is damped by the accumulated slope of the octaves above it.
	/// Detail therefore collects in the flats and thins out on the steeps, which
	/// is what water does: valleys widen and smooth, ridges stay sharp, and the
	/// surface acquires drainage. It is a pure function of position, so it costs
	/// nothing at a patch boundary and is identical at every LOD.
	LEDGERCORE_API double Eroded(
		const FVector3d& Position,
		uint32 Seed,
		int32 Octaves,
		double ErosionStrength,
		double Lacunarity = 2.01,
		double Gain = 0.5);

	/// Ridged multifractal with the same slope damping. Mountains that drain.
	LEDGERCORE_API double ErodedRidged(
		const FVector3d& Position,
		uint32 Seed,
		int32 Octaves,
		double ErosionStrength,
		double Lacunarity = 2.01,
		double Gain = 0.5);
}
