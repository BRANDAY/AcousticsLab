#include "Simulation/SolverSettings.h"

#include "Solvers/RayTracer.h"

FRayTracerSettings FSolverSettings::ToRayTracerSettings() const
{
	FRayTracerSettings Result;
	Result.NumRays = FMath::Max(1, NumRays);
	Result.MaxOrder = FMath::Max(0, MaxOrder);
	Result.ReceiverRadius = ReceiverRadius > 0.0f ? ReceiverRadius : Acoustics::DefaultReceiverRadius;
	Result.BinSeconds = FMath::Max(0.0001, static_cast<double>(BinMs) * 0.001);
	Result.MaxTimeSeconds = FMath::Max(Result.BinSeconds * 2.0, static_cast<double>(TMax));
	Result.Seed = static_cast<uint32>(FMath::Max(0, Seed));
	Result.bAirAbsorption = bAirAbsorption;
	for (double& Value : Result.AirAbsorption)
	{
		Value = FMath::Max(0.0f, AirAbsorptionPerMeter);
	}
	Result.NumThreads = FMath::Max(0, NumThreads);
	Result.bUseBVH = bUseBVH;
	Result.MaxPathsForVisualization = FMath::Max(0, MaxPathsForVisualization);
	return Result;
}

FString FSolverSettings::ToString() const
{
	return FString::Printf(TEXT("%d rays, max order %d, seed %d, r %.2f m, bin %.1f ms, T_max %.1f s, air %s (m %.4f), threads %d, BVH %s, paths %d"),
		NumRays, MaxOrder, Seed, ReceiverRadius, BinMs, TMax, bAirAbsorption ? TEXT("on") : TEXT("off"), AirAbsorptionPerMeter, NumThreads, bUseBVH ? TEXT("on") : TEXT("off"), MaxPathsForVisualization);
}
