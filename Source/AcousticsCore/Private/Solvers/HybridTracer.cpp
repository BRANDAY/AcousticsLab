#include "Solvers/HybridTracer.h"

#include "AcousticsCore.h"
#include "Geometry/AcousticScene.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

DECLARE_CYCLE_STAT(TEXT("HybridTracer Run"), STAT_AcousticsHybridTracerRun, STATGROUP_Acoustics);

bool FHybridTracerSettings::Validate(FString& OutError) const
{
	if (K < 0 || K > 10)
	{
		OutError = TEXT("K must lie in [0, 10]");
		return false;
	}
	if (!(MixingTimeSeconds > 0.0))
	{
		OutError = TEXT("MixingTimeSeconds must be positive");
		return false;
	}
	FString PartError;
	if (!MakeRaySettings().Validate(PartError))
	{
		OutError = TEXT("ray part: ") + PartError;
		return false;
	}
	if (!MakeBeamSettings().Validate(PartError))
	{
		OutError = TEXT("beam part: ") + PartError;
		return false;
	}
	return true;
}

FBeamTracerSettings FHybridTracerSettings::MakeBeamSettings() const
{
	FBeamTracerSettings Result = Beam;
	Result.MaxBeamOrder = FMath::Max(0, K);
	// The ray tracer models neither refraction nor a directional source, so the beams do not either (same physics in both parts).
	Result.bRefraction = false;
	Result.bDirectionalSource = false;
	Result.bScatteringLoss = bScatteringLoss;
	Result.bAnalyticDirectSound = true;
	// Histogram layout, receiver radius and air absorption must match the ray part: the two are added bin by bin.
	Result.BinSeconds = Ray.BinSeconds;
	Result.MaxTimeSeconds = Ray.MaxTimeSeconds;
	Result.ReceiverRadius = Ray.ReceiverRadius;
	Result.bAirAbsorption = Ray.bAirAbsorption;
	for (int32 Band = 0; Band < Acoustics::NumBands; ++Band)
	{
		Result.AirAbsorption[Band] = Ray.AirAbsorption[Band];
	}
	return Result;
}

FRayTracerSettings FHybridTracerSettings::MakeRaySettings() const
{
	FRayTracerSettings Result = Ray;
	Result.ExcludeSpecularMaxOrder = FMath::Max(0, K);
	Result.ExcludeSpecularMaxSeconds = MixingTimeSeconds;
	Result.bRecordSpecularArrivals = bCountMissedReflections && K > 0;
	Result.bAnalyticDirectSound = true;
	return Result;
}

FString FHybridResult::Describe() const
{
	if (!IsValid())
	{
		return Hybrid.IsValid() ? Hybrid->Describe() : TEXT("no hybrid result");
	}
	FString Text = FString::Printf(TEXT("Hybrid K=%d, t_mix %.1f ms%s: beams %.3f s (%d nodes), rays %.3f s (%d rays), composition %.3f s"),
		K, MixingTimeSeconds * 1000.0, bScatteringLoss ? TEXT(", beams x (1-s)") : TEXT(""), BeamSeconds, PureBeam->NumBeamNodes, RaySeconds, PureRay->NumRays, ComposeSeconds);
	for (int32 Index = 0; Index < Stats.Num() && Index < Hybrid->Receivers.Num() && Index < 8; ++Index)
	{
		const FHybridReceiverStats& S = Stats[Index];
		const FBandMetrics& H = Hybrid->Receivers[Index].Metrics.Band(0);
		const FBandMetrics& R = PureRay->Receivers[Index].Metrics.Band(0);
		Text += FString::Printf(TEXT("\n  %s: %d early beam reflections (+%d after t_mix), rays found %d / missed %d; energy beams %.4g vs rays %.4g, late %.4g; T30 hybrid %.3f s / ray %.3f s, C80 %.2f / %.2f dB"),
			*Hybrid->Receivers[Index].Name, S.NumBeamEarly, S.NumBeamDropped, S.NumFoundByRays, S.NumMissedByRays, S.BeamEarlyEnergy, S.RayExcludedEnergy, S.RayLateEnergy,
			H.T30, R.T30, H.C80, R.C80);
	}
	if (Stats.Num() > 8)
	{
		Text += FString::Printf(TEXT("\n  ... %d more receivers"), Stats.Num() - 8);
	}
	return Text;
}

FHybridResult FHybridTracer::Run(const FAcousticScene& Scene, const FVector3d& Source, TConstArrayView<FSolverReceiver> Receivers, const FHybridTracerSettings& Settings, FSolverProgress* Progress)
{
	SCOPE_CYCLE_COUNTER(STAT_AcousticsHybridTracerRun);
	TRACE_CPUPROFILER_EVENT_SCOPE(FHybridTracer::Run);

	const double StartTime = FPlatformTime::Seconds();
	FHybridResult Out;
	Out.K = Settings.K;
	Out.MixingTimeSeconds = Settings.MixingTimeSeconds;
	Out.bScatteringLoss = Settings.bScatteringLoss;

	TSharedRef<FSimulationResult> Hybrid = MakeShared<FSimulationResult>();
	Hybrid->SolverName = Name();
	Hybrid->SourcePosition = Source;
	Hybrid->NumTriangles = Scene.NumTriangles();
	Out.Hybrid = Hybrid;

	if (!Settings.Validate(Hybrid->Error))
	{
		UE_LOG(LogAcoustics, Error, TEXT("HybridTracer: invalid settings: %s"), *Hybrid->Error);
		return Out;
	}
	const FBeamTracerSettings BeamSettings = Settings.MakeBeamSettings();
	const FRayTracerSettings RaySettings = Settings.MakeRaySettings();

	// Early part: the beam tree up to order K (M7 pool, deterministic for any thread count).
	const double BeamStart = FPlatformTime::Seconds();
	TSharedRef<FSimulationResult> Beam = MakeShared<FSimulationResult>(FBeamTracer::Run(Scene, Source, Receivers, BeamSettings, Progress));
	Out.BeamSeconds = FPlatformTime::Seconds() - BeamStart;
	Out.PureBeam = Beam;
	if (!Beam->bSuccess)
	{
		Hybrid->bCancelled = Beam->bCancelled;
		Hybrid->Error = Beam->bCancelled ? FString() : TEXT("beam part: ") + Beam->Error;
		return Out;
	}

	// Late part: one ray run; early-specular deposits go to ExcludedHistogram (pure ray = both histograms).
	const double RayStart = FPlatformTime::Seconds();
	FSimulationResult Ray = FRayTracer::Run(Scene, Source, Receivers, RaySettings, Progress);
	Out.RaySeconds = FPlatformTime::Seconds() - RayStart;
	if (!Ray.bSuccess)
	{
		Hybrid->bCancelled = Ray.bCancelled;
		Hybrid->Error = Ray.bCancelled ? FString() : TEXT("ray part: ") + Ray.Error;
		Out.PureRay = MakeShared<FSimulationResult>(MoveTemp(Ray));
		return Out;
	}
	if (Ray.Receivers.Num() != Beam->Receivers.Num())
	{
		Hybrid->Error = TEXT("ray and beam parts returned different receiver counts");
		UE_LOG(LogAcoustics, Error, TEXT("HybridTracer: %s"), *Hybrid->Error);
		return Out;
	}

	// Composition, receiver by receiver on this thread (fixed order: deterministic).
	const double ComposeStart = FPlatformTime::Seconds();
	const int32 NumReceivers = Ray.Receivers.Num();
	Hybrid->Receivers.SetNum(NumReceivers);
	Out.LateHistograms.SetNum(NumReceivers);
	Out.Stats.SetNum(NumReceivers);
	for (int32 Index = 0; Index < NumReceivers; ++Index)
	{
		const FReceiverResult& RayReceiver = Ray.Receivers[Index];
		const FReceiverResult& BeamReceiver = Beam->Receivers[Index];
		FReceiverResult& Target = Hybrid->Receivers[Index];
		FHybridReceiverStats& Stats = Out.Stats[Index];

		Target.Name = RayReceiver.Name;
		Target.Position = RayReceiver.Position;
		Target.Radius = RayReceiver.Radius;
		Target.Histogram = RayReceiver.Histogram; // direct sound + every deposit that is not early-specular
		Target.DirectDistance = RayReceiver.DirectDistance;
		Target.DirectArrivalSeconds = RayReceiver.DirectArrivalSeconds;
		Target.bDirectOccluded = RayReceiver.bDirectOccluded;
		Target.DirectEnergy = RayReceiver.DirectEnergy;
		Target.NumRayHits = RayReceiver.NumRayHits;
		Target.ContainingBeamNodes = BeamReceiver.ContainingBeamNodes;
		Out.LateHistograms[Index] = RayReceiver.Histogram;
		Stats.RayLateEnergy = RayReceiver.Histogram.TotalEnergy(0);
		Stats.RayExcludedEnergy = RayReceiver.ExcludedHistogram.IsInitialized() ? RayReceiver.ExcludedHistogram.TotalEnergy(0) : 0.0;

		for (const FEarlyReflection& Reflection : BeamReceiver.EarlyReflections)
		{
			if (Reflection.Order < 1 || Reflection.Order > Settings.K)
			{
				continue;
			}
			if (!IsEarlyBeamReflection(Reflection, Settings.K, Settings.MixingTimeSeconds))
			{
				++Stats.NumBeamDropped; // after t_mix: the rays keep this path
				continue;
			}
			Target.Histogram.AddEnergy(Reflection.TimeSeconds, Reflection.Energy);
			Target.EarlyReflections.Add(Reflection);
			++Stats.NumBeamEarly;
			Stats.BeamEarlyEnergy += Reflection.Energy[0];
			Stats.LastEarlyArrivalSeconds = FMath::Max(Stats.LastEarlyArrivalSeconds, Reflection.TimeSeconds);
			if (RaySettings.bRecordSpecularArrivals)
			{
				const bool bFound = RayReceiver.SpecularRayArrivals.ContainsByPredicate([&Reflection](const FSpecularRayArrival& Arrival) { return IsSameImage(Arrival, Reflection); });
				++(bFound ? Stats.NumFoundByRays : Stats.NumMissedByRays);
			}
		}
		for (const FSpecularRayArrival& Arrival : RayReceiver.SpecularRayArrivals)
		{
			++(Arrival.bExcluded ? Stats.NumRayGroupsExcluded : Stats.NumRayGroupsLate);
			const bool bHasBeam = Target.EarlyReflections.ContainsByPredicate([&Arrival](const FEarlyReflection& Reflection) { return IsSameImage(Arrival, Reflection); });
			Stats.NumRayGroupsWithoutBeam += Arrival.bExcluded && !bHasBeam ? 1 : 0;
		}
		Target.Metrics = FRoomMetrics::Compute(Target.Histogram, Target.DirectArrivalSeconds);
	}

	Hybrid->Paths = Ray.Paths;
	Hybrid->PathSampleStride = Ray.PathSampleStride;
	Hybrid->NumRays = Ray.NumRays;
	Hybrid->NumSegments = Ray.NumSegments;
	Hybrid->NumRaysEscaped = Ray.NumRaysEscaped;
	Hybrid->MeanOrder = Ray.MeanOrder;
	Hybrid->BeamTree = Beam->BeamTree;
	Hybrid->NumRootBeams = Beam->NumRootBeams;
	Hybrid->NumBeamNodes = Beam->NumBeamNodes;

	// Pure ray: the same run with nothing excluded.
	TSharedRef<FSimulationResult> PureRay = MakeShared<FSimulationResult>(MoveTemp(Ray));
	for (FReceiverResult& Receiver : PureRay->Receivers)
	{
		if (Receiver.ExcludedHistogram.IsInitialized())
		{
			Receiver.Histogram.Accumulate(Receiver.ExcludedHistogram);
		}
		Receiver.NumRayHits += Receiver.NumExcludedHits;
		Receiver.Metrics = FRoomMetrics::Compute(Receiver.Histogram, Receiver.DirectArrivalSeconds);
	}
	Out.PureRay = PureRay;
	Out.ComposeSeconds = FPlatformTime::Seconds() - ComposeStart;

	Hybrid->Timing.SetupSeconds = Out.BeamSeconds;
	Hybrid->Timing.TraceSeconds = Out.RaySeconds;
	Hybrid->Timing.MetricsSeconds = Out.ComposeSeconds;
	Hybrid->Timing.TotalSeconds = FPlatformTime::Seconds() - StartTime;
	Hybrid->Timing.NumWorkers = PureRay->Timing.NumWorkers;
	Hybrid->bSuccess = true;

	UE_LOG(LogAcoustics, Log, TEXT("%s"), *Out.Describe());
	return Out;
}
