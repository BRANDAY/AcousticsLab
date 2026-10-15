#include "Simulation/SolverSettings.h"

#include "Solvers/BeamTracer.h"
#include "Solvers/ImageSourceSolver.h"
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

FImageSourceSettings FSolverSettings::ToImageSourceSettings() const
{
	FImageSourceSettings Result;
	Result.MaxOrder = FMath::Max(0, ImageSourceOrder);
	Result.ReceiverRadius = ReceiverRadius > 0.0f ? ReceiverRadius : Acoustics::DefaultReceiverRadius;
	Result.BinSeconds = FMath::Max(0.0001, static_cast<double>(BinMs) * 0.001);
	Result.MaxTimeSeconds = FMath::Max(Result.BinSeconds * 2.0, static_cast<double>(TMax));
	Result.bAirAbsorption = bAirAbsorption;
	for (double& Value : Result.AirAbsorption)
	{
		Value = FMath::Max(0.0f, AirAbsorptionPerMeter);
	}
	Result.MaxImageSources = FMath::Max(1, MaxImageSources);
	Result.bUseBVH = bUseBVH;
	return Result;
}

FBeamTracerSettings FSolverSettings::ToBeamTracerSettings() const
{
	FBeamTracerSettings Result;
	Result.SubdivisionLevel = FMath::Clamp(SubdivisionLevel, 0, 7);
	Result.MaxBeamOrder = FMath::Max(0, MaxBeamOrder);
	Result.ReceiverRadius = ReceiverRadius > 0.0f ? ReceiverRadius : Acoustics::DefaultReceiverRadius;
	Result.BinSeconds = FMath::Max(0.0001, static_cast<double>(BinMs) * 0.001);
	Result.MaxTimeSeconds = FMath::Max(Result.BinSeconds * 2.0, static_cast<double>(TMax));
	Result.bAirAbsorption = bAirAbsorption;
	for (double& Value : Result.AirAbsorption)
	{
		Value = FMath::Max(0.0f, AirAbsorptionPerMeter);
	}
	Result.MaxBeamNodes = FMath::Max(1, MaxBeamNodes);
	Result.bUseBVH = bUseBVH;
	Result.NumThreads = FMath::Max(0, NumThreads);
	Result.bRefraction = bRefraction;
	return Result;
}

FString FSolverSettings::ToString(EAcousticSolver Solver) const
{
	const FString Common = FString::Printf(TEXT("r %.2f m, bin %.1f ms, T_max %.1f s, air %s (m %.4f), BVH %s"),
		ReceiverRadius, BinMs, TMax, bAirAbsorption ? TEXT("on") : TEXT("off"), AirAbsorptionPerMeter, bUseBVH ? TEXT("on") : TEXT("off"));
	switch (Solver)
	{
	case EAcousticSolver::ImageSource:
		return FString::Printf(TEXT("image sources up to order %d (budget %d), %s"), ImageSourceOrder, MaxImageSources, *Common);
	case EAcousticSolver::BeamTracer:
		return FString::Printf(TEXT("beams: subdivision %d (%d root beams), order <= %d, node budget %d, refraction %s, %s"), SubdivisionLevel, 20 * (1 << (2 * FMath::Max(0, SubdivisionLevel))), MaxBeamOrder, MaxBeamNodes, bRefraction ? TEXT("on") : TEXT("off"), *Common);
	default:
		return FString::Printf(TEXT("%d rays, max order %d, seed %d, %s, threads %d, paths %d"), NumRays, MaxOrder, Seed, *Common, NumThreads, MaxPathsForVisualization);
	}
}
