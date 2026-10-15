// Solver parameters edited in the HUD and stored per level (SPEC §3.5).
#pragma once

#include "CoreMinimal.h"
#include "SolverSettings.generated.h"

struct FRayTracerSettings;

/** Which solver a run uses. Only the ray tracer exists in M2; the others arrive with M5 / M8. */
UENUM(BlueprintType)
enum class EAcousticSolver : uint8
{
	RayTracer UMETA(DisplayName = "Ray tracer"),
	ImageSource UMETA(DisplayName = "Image sources"),
	BeamTracer UMETA(DisplayName = "Beam tracer"),
	Hybrid UMETA(DisplayName = "Hybrid beam/ray")
};

/**
 * One struct for every solver so the HUD, the manager and the subsystem carry the same settings.
 * Ray-tracer fields map to FRayTracerSettings through ToRayTracerSettings(); beam / hybrid / refraction
 * fields are stored now and used from M5 on.
 */
USTRUCT(BlueprintType)
struct ACOUSTICSLAB_API FSolverSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rays", meta = (ClampMin = "1", ClampMax = "5000000"))
	int32 NumRays = 20000;

	/** A ray stops once its reflection order exceeds this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rays", meta = (ClampMin = "0", ClampMax = "100000"))
	int32 MaxOrder = 200;

	/** Seed of the per-ray random streams; the same seed reproduces the histogram bit for bit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rays", meta = (ClampMin = "0"))
	int32 Seed = 1;

	/** Receiver sphere radius in meters applied to every receiver; 0 keeps each receiver's own radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Receiver", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float ReceiverRadius = 0.5f;

	/** Histogram bin, ms. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Histogram", meta = (ClampMin = "0.1", ClampMax = "100.0"))
	float BinMs = 1.0f;

	/** Histogram length, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Histogram", meta = (ClampMin = "0.1", ClampMax = "30.0"))
	float TMax = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Air")
	bool bAirAbsorption = false;

	/** Broadband air absorption exponent m in exp(-m d), 1/m. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Air", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AirAbsorptionPerMeter = 0.002f;

	/** Worker threads; 0 = all task-graph workers, 1 = single-threaded. Results do not depend on it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance", meta = (ClampMin = "0", ClampMax = "256"))
	int32 NumThreads = 0;

	/** Brute-force triangle search when off (M7 benchmark). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
	bool bUseBVH = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visualization", meta = (ClampMin = "0", ClampMax = "100000"))
	int32 MaxPathsForVisualization = 2000;

	/** Icosahedron subdivisions of the beam emission (M5): 0 = 20 beams, 2 = 320, 3 = 1280. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Beams", meta = (ClampMin = "0", ClampMax = "5"))
	int32 SubdivisionLevel = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Beams", meta = (ClampMin = "0", ClampMax = "10"))
	int32 MaxBeamOrder = 3;

	/** Beam order up to which the hybrid tracer uses beams (M8). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hybrid", meta = (ClampMin = "0", ClampMax = "10"))
	int32 HybridK = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Refraction")
	bool bRefraction = false;

	FRayTracerSettings ToRayTracerSettings() const;
	FString ToString() const;
};
