// Small deterministic random generator for the stochastic solvers.
#pragma once

#include "CoreMinimal.h"

/**
 * PCG32 (O'Neill): 64-bit state, 32-bit output, one independent stream per (seed, stream) pair.
 * Every ray gets its own stream seeded from (Seed, RayIndex), so the sequence a ray sees never depends
 * on which thread traced it or on how many rays ran before it.
 */
struct ACOUSTICSCORE_API FAcousticRandom
{
	uint64 State = 0;
	uint64 Increment = 1;

	FAcousticRandom() = default;
	FAcousticRandom(uint64 Seed, uint64 Stream)
	{
		Seed = Mix(Seed);
		Increment = (Mix(Stream ^ 0x9E3779B97F4A7C15ull) << 1) | 1ull;
		State = 0;
		Next();
		State += Seed;
		Next();
	}

	uint32 Next()
	{
		const uint64 Old = State;
		State = Old * 6364136223846793005ull + Increment;
		const uint32 XorShifted = static_cast<uint32>(((Old >> 18u) ^ Old) >> 27u);
		const uint32 Rotation = static_cast<uint32>(Old >> 59u);
		return (XorShifted >> Rotation) | (XorShifted << ((0u - Rotation) & 31u));
	}

	/** Uniform in [0, 1) with 53 random bits. */
	double NextDouble()
	{
		const uint64 Bits = (static_cast<uint64>(Next()) << 32) | static_cast<uint64>(Next());
		return static_cast<double>(Bits >> 11) * (1.0 / 9007199254740992.0);
	}

private:
	/** SplitMix64 finalizer: spreads small seeds and ray indices over the whole state space. */
	static uint64 Mix(uint64 Value)
	{
		Value += 0x9E3779B97F4A7C15ull;
		Value = (Value ^ (Value >> 30)) * 0xBF58476D1CE4E5B9ull;
		Value = (Value ^ (Value >> 27)) * 0x94D049BB133111EBull;
		return Value ^ (Value >> 31);
	}
};
