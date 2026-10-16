#include "Simulation/AcousticsExperimentLibrary.h"

#include "AcousticsCore.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Geometry/AcousticScene.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Levels/AmphitheatreActor.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Results/RoomMetrics.h"
#include "Results/SimulationResult.h"
#include "Scene/AcousticReceiver.h"
#include "Scene/AcousticSource.h"
#include "Scene/ReceiverGrid.h"
#include "Simulation/AcousticSimulationManager.h"
#include "Solvers/HybridTracer.h"
#include "Solvers/RayTracer.h"

namespace
{
	FString ExperimentDir()
	{
		const FString Dir = FPaths::ProjectSavedDir() / TEXT("Experiments");
		IFileManager::Get().MakeDirectory(*Dir, true);
		return Dir;
	}

	FString Num(double Value, bool bValid = true, int32 Decimals = 3)
	{
		return bValid ? FString::SanitizeFloat(FMath::RoundToDouble(Value * FMath::Pow(10.0, Decimals)) / FMath::Pow(10.0, Decimals), 0) : FString(TEXT("n/a"));
	}

	/** Amphitheatre scene of the loaded level with the point receivers R1..R4 (no grid); false with a message. */
	bool PrepareAmphitheatre(UObject* WorldContextObject, bool bAudience, bool bVelarium, FAcousticScene& OutScene, FVector3d& OutSource,
		TArray<FSolverReceiver>& OutReceivers, AAcousticSimulationManager*& OutManager, FString& OutError)
	{
		UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
		AAmphitheatreActor* Amphitheatre = AAmphitheatreActor::Find(World);
		OutManager = AAcousticSimulationManager::FindOrSpawn(World);
		TArray<AAcousticSource*> Sources = AAcousticSource::FindAll(World);
		if (!Amphitheatre || !OutManager || Sources.Num() == 0)
		{
			OutError = TEXT("no AmphitheatreActor / simulation manager / source in this world");
			return false;
		}
		Amphitheatre->SetVelarium(bVelarium);
		Amphitheatre->SetAudience(bAudience);
		if (!OutManager->ExtractScene(OutScene, OutError))
		{
			return false;
		}
		OutSource = Sources[0]->GetPositionMeters();
		OutReceivers.Reset();
		for (AAcousticReceiver* Receiver : AAcousticReceiver::FindAll(World))
		{
			OutReceivers.Emplace(Receiver->ReceiverId.ToString(), Receiver->GetPositionMeters(), 0.5);
		}
		return OutReceivers.Num() > 0;
	}

	void RestoreAmphitheatre(UObject* WorldContextObject)
	{
		UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
		if (AAmphitheatreActor* Amphitheatre = AAmphitheatreActor::Find(World))
		{
			Amphitheatre->SetVelarium(false);
			Amphitheatre->SetAudience(false);
		}
	}

	struct FStat
	{
		TArray<double> Values;
		double Mean() const { double Sum = 0.0; for (const double V : Values) { Sum += V; } return Values.Num() ? Sum / Values.Num() : 0.0; }
		double StdDev() const
		{
			if (Values.Num() < 2) { return 0.0; }
			const double M = Mean();
			double Sum = 0.0;
			for (const double V : Values) { Sum += FMath::Square(V - M); }
			return FMath::Sqrt(Sum / (Values.Num() - 1));
		}
	};
}

FString UAcousticsExperimentLibrary::RunShoeboxAlphaSweep(int32 NumRays, FString& OutCsvPath)
{
	const FVector3d RoomSize(10.0, 7.0, 3.0);
	const double Volume = RoomSize.X * RoomSize.Y * RoomSize.Z;
	const double Surface = 2.0 * (RoomSize.X * RoomSize.Y + RoomSize.X * RoomSize.Z + RoomSize.Y * RoomSize.Z);
	const FVector3d Source(-3.0, -1.5, 1.5);
	TArray<FSolverReceiver> Receivers;
	Receivers.Emplace(TEXT("R1"), FVector3d(3.0, 1.5, 1.5), 0.5);
	Receivers.Emplace(TEXT("R2"), FVector3d(0.0, 2.5, 1.2), 0.5);

	const double Alphas[] = { 0.05, 0.1, 0.2, 0.3, 0.5 };
	const double Scatterings[] = { 0.1, 1.0 };
	FString Csv = TEXT("scattering,alpha,sabine_s,eyring_s,t30_r1_s,t30_r2_s,t30_mean_s,dev_eyring_pct,dev_sabine_pct,c80_r1_db,trace_s\n");
	FString Md = FString::Printf(TEXT("| s | alpha | Sabine, s | Eyring, s | T30 R1, s | T30 R2, s | mean vs Eyring | mean vs Sabine |\n|---|---|---|---|---|---|---|---|\n"));
	for (const double Scattering : Scatterings)
	{
		for (const double Alpha : Alphas)
		{
			const FAcousticScene Scene = FAcousticScene::MakeShoebox(RoomSize, FAcousticMaterialProps(TEXT("Sweep"), Alpha, Scattering));
			FRayTracerSettings Settings;
			Settings.NumRays = NumRays;
			Settings.Seed = 1;
			Settings.MaxPathsForVisualization = 0;
			const FSimulationResult Result = FRayTracer::Run(Scene, Source, Receivers, Settings);
			const double Sabine = RoomAcoustics::SabineT60(Volume, Surface, Alpha);
			const double Eyring = RoomAcoustics::EyringT60(Volume, Surface, Alpha);
			const FBandMetrics& B1 = Result.Receivers[0].Metrics.Band(0);
			const FBandMetrics& B2 = Result.Receivers[1].Metrics.Band(0);
			const bool bBoth = B1.bT30Valid && B2.bT30Valid;
			const double Mean = bBoth ? 0.5 * (B1.T30 + B2.T30) : (B1.bT30Valid ? B1.T30 : B2.T30);
			const double DevE = 100.0 * (Mean - Eyring) / Eyring;
			const double DevS = 100.0 * (Mean - Sabine) / Sabine;
			Csv += FString::Printf(TEXT("%.1f,%.2f,%.3f,%.3f,%s,%s,%.3f,%.1f,%.1f,%.2f,%.2f\n"), Scattering, Alpha, Sabine, Eyring,
				*Num(B1.T30, B1.bT30Valid), *Num(B2.T30, B2.bT30Valid), Mean, DevE, DevS, B1.C80, Result.Timing.TotalSeconds);
			Md += FString::Printf(TEXT("| %.1f | %.2f | %.3f | %.3f | %s | %s | %+.1f %% | %+.1f %% |\n"), Scattering, Alpha, Sabine, Eyring,
				*Num(B1.T30, B1.bT30Valid), *Num(B2.T30, B2.bT30Valid), DevE, DevS);
			UE_LOG(LogAcoustics, Display, TEXT("E1: s %.1f alpha %.2f: Sabine %.3f Eyring %.3f T30 %.3f / %.3f (%.1f s)"), Scattering, Alpha, Sabine, Eyring, B1.T30, B2.T30, Result.Timing.TotalSeconds);
		}
	}
	OutCsvPath = ExperimentDir() / TEXT("E1_shoebox_alpha.csv");
	FFileHelper::SaveStringToFile(Csv, *OutCsvPath);
	return Md;
}

FString UAcousticsExperimentLibrary::RunAmphitheatreConditions(UObject* WorldContextObject, int32 NumRays, FString& OutCsvPath)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	AAmphitheatreActor* Amphitheatre = AAmphitheatreActor::Find(World);
	AAcousticSimulationManager* Manager = AAcousticSimulationManager::FindOrSpawn(World);
	if (!Amphitheatre || !Manager)
	{
		return TEXT("E5: no AmphitheatreActor / simulation manager in this world");
	}
	TArray<AAcousticSource*> Sources = AAcousticSource::FindAll(World);
	if (Sources.Num() == 0)
	{
		return TEXT("E5: no source");
	}
	const FVector3d Source = Sources[0]->GetPositionMeters();

	TArray<FSolverReceiver> Receivers;
	for (AAcousticReceiver* Receiver : AAcousticReceiver::FindAll(World))
	{
		Receivers.Emplace(Receiver->ReceiverId.ToString(), Receiver->GetPositionMeters(), 0.5);
	}
	const int32 NumPoints = Receivers.Num();
	AReceiverGrid* Grid = nullptr;
	for (AReceiverGrid* Candidate : AReceiverGrid::FindAll(World))
	{
		if (Candidate->GridId == Amphitheatre->GridId)
		{
			Grid = Candidate;
		}
	}
	if (Grid)
	{
		Amphitheatre->ApplyGrid(Grid);
		Grid->AppendSolverReceivers(Receivers, 0.5);
	}

	double A, B, Depth, Rise, Step;
	int32 Rows;
	Amphitheatre->GetDimensions(A, B, Rows, Depth, Rise, Step);

	struct FCondition { const TCHAR* Name; bool bAudience; bool bVelarium; };
	const FCondition Conditions[] = { { TEXT("empty"), false, false }, { TEXT("audience"), true, false }, { TEXT("velarium"), false, true }, { TEXT("audience+velarium"), true, true } };
	const bool bOldAudience = Amphitheatre->bAudience;
	const bool bOldVelarium = Amphitheatre->bVelarium;

	// A regression over a nearly flat Schroeder tail (few late hits) gives absurd decay times; they are not averaged.
	constexpr double MaxPlausibleT30 = 10.0;
	FString PointCsv = TEXT("condition,receiver,row,x_m,y_m,z_m,c80_db,t30_s,t30_valid,edt_s,d50\n");
	FString RowCsv = TEXT("condition,row,points,c80_mean_db,t30_mean_s,t30_valid_points,edt_mean_s\n");
	FString MdPoints = TEXT("| condition | R1 C80 / T30 | R2 C80 / T30 | R3 C80 / T30 | R4 C80 / T30 | triangles | trace, s |\n|---|---|---|---|---|---|---|\n");
	TMap<FString, TArray<FString>> RowCells; // row -> per-condition "C80 / T30"
	for (const FCondition& Condition : Conditions)
	{
		Amphitheatre->bAudience = Condition.bAudience;
		Amphitheatre->SetVelarium(Condition.bVelarium);
		Amphitheatre->SetAudience(Condition.bAudience);
		FAcousticScene Scene;
		FString Error;
		if (!Manager->ExtractScene(Scene, Error))
		{
			return FString::Printf(TEXT("E5: extraction failed (%s): %s"), Condition.Name, *Error);
		}
		FRayTracerSettings Settings;
		Settings.NumRays = NumRays;
		Settings.Seed = 1;
		Settings.MaxPathsForVisualization = 0;
		// The ray tracer keeps a histogram per receiver and chunk until its reduction; batches of receivers bound that memory
		// (same seed = the same rays in every batch, so the result equals one run with all receivers).
		FSimulationResult Result;
		double TraceSeconds = 0.0;
		constexpr int32 BatchSize = 220;
		for (int32 First = 0; First < Receivers.Num(); First += BatchSize)
		{
			const TConstArrayView<FSolverReceiver> Batch(Receivers.GetData() + First, FMath::Min(BatchSize, Receivers.Num() - First));
			FSimulationResult Part = FRayTracer::Run(Scene, Source, Batch, Settings);
			TraceSeconds += Part.Timing.TotalSeconds;
			Result.Receivers.Append(MoveTemp(Part.Receivers));
		}
		Result.Timing.TotalSeconds = TraceSeconds;
		UE_LOG(LogAcoustics, Display, TEXT("E5: %s: %d triangles, %d receivers, %.1f s"), Condition.Name, Scene.Triangles.Num(), Receivers.Num(), Result.Timing.TotalSeconds);

		TArray<double> SumC80, SumT30, SumEDT;
		TArray<int32> Count, CountT30;
		SumC80.Init(0.0, Rows + 1); SumT30.Init(0.0, Rows + 1); SumEDT.Init(0.0, Rows + 1);
		Count.Init(0, Rows + 1); CountT30.Init(0, Rows + 1);
		FString PointCells;
		for (int32 Index = 0; Index < Result.Receivers.Num(); ++Index)
		{
			const FReceiverResult& R = Result.Receivers[Index];
			const FBandMetrics& Band = R.Metrics.Band(0);
			const int32 Row = Index >= NumPoints && Grid ? Grid->GetTileRow(Index - NumPoints) : 0;
			PointCsv += FString::Printf(TEXT("%s,%s,%d,%.2f,%.2f,%.2f,%.2f,%.3f,%d,%.3f,%.3f\n"), Condition.Name, *R.Name.Replace(TEXT(","), TEXT(";")), Row,
				R.Position.X, R.Position.Y, R.Position.Z, Band.C80, Band.T30, Band.bT30Valid ? 1 : 0, Band.EDT, Band.D50);
			if (Index < NumPoints)
			{
				PointCells += FString::Printf(TEXT(" %.1f dB / %s s |"), Band.C80, *Num(Band.T30, Band.bT30Valid && Band.T30 < MaxPlausibleT30, 2));
			}
			else if (Row >= 1 && Row <= Rows && R.Metrics.bValid)
			{
				SumC80[Row] += Band.C80;
				SumEDT[Row] += Band.EDT;
				++Count[Row];
				if (Band.bT30Valid && Band.T30 < MaxPlausibleT30)
				{
					SumT30[Row] += Band.T30;
					++CountT30[Row];
				}
			}
		}
		MdPoints += FString::Printf(TEXT("| %s |%s %d | %.1f |\n"), Condition.Name, *PointCells, Scene.Triangles.Num(), Result.Timing.TotalSeconds);
		for (int32 Row = 1; Row <= Rows; ++Row)
		{
			const double C80 = Count[Row] ? SumC80[Row] / Count[Row] : 0.0;
			const double T30 = CountT30[Row] ? SumT30[Row] / CountT30[Row] : 0.0;
			RowCsv += FString::Printf(TEXT("%s,%d,%d,%.2f,%.3f,%d,%.3f\n"), Condition.Name, Row, Count[Row], C80, T30, CountT30[Row], Count[Row] ? SumEDT[Row] / Count[Row] : 0.0);
			RowCells.FindOrAdd(FString::FromInt(Row)).Add(FString::Printf(TEXT("%.1f / %s"), C80, *Num(T30, CountT30[Row] > 0, 2)));
		}
	}
	Amphitheatre->bAudience = bOldAudience;
	Amphitheatre->SetVelarium(bOldVelarium);
	Amphitheatre->SetAudience(bOldAudience);

	FString MdRows = TEXT("| row | empty C80 dB / T30 s | audience | velarium | audience + velarium |\n|---|---|---|---|---|\n");
	for (int32 Row = 1; Row <= Rows; ++Row)
	{
		const TArray<FString>& Cells = RowCells.FindChecked(FString::FromInt(Row));
		MdRows += FString::Printf(TEXT("| %d | %s |\n"), Row, *FString::Join(Cells, TEXT(" | ")));
	}
	const FString Dir = ExperimentDir();
	OutCsvPath = Dir / TEXT("E5_amphitheatre_rows.csv");
	FFileHelper::SaveStringToFile(RowCsv, *OutCsvPath);
	FFileHelper::SaveStringToFile(PointCsv, *(Dir / TEXT("E5_amphitheatre_points.csv")));
	return FString::Printf(TEXT("Rays %d, %d point receivers + %d grid points.\n\n%s\n%s"), NumRays, NumPoints, Receivers.Num() - NumPoints, *MdPoints, *MdRows);
}

FString UAcousticsExperimentLibrary::RunRayConvergence(UObject* WorldContextObject, const TArray<int32>& RayCounts, int32 NumSeeds, FString& OutCsvPath)
{
	struct FSceneCase { FString Name; FAcousticScene Scene; FVector3d Source; TArray<FSolverReceiver> Receivers; };
	TArray<FSceneCase> Cases;
	{
		FSceneCase& Shoebox = Cases.AddDefaulted_GetRef();
		Shoebox.Name = TEXT("shoebox a0.1 s0.2");
		Shoebox.Scene = FAcousticScene::MakeShoebox(FVector3d(10.0, 7.0, 3.0), FAcousticMaterialProps(TEXT("Convergence"), 0.1, 0.2));
		Shoebox.Source = FVector3d(-3.0, -1.5, 1.5);
		Shoebox.Receivers.Emplace(TEXT("R1"), FVector3d(3.0, 1.5, 1.5), 0.5);
		Shoebox.Receivers.Emplace(TEXT("R2"), FVector3d(0.0, 2.5, 1.2), 0.5);
	}
	{
		FSceneCase Amph;
		Amph.Name = TEXT("amphitheatre audience");
		AAcousticSimulationManager* Manager = nullptr;
		FString Error;
		if (PrepareAmphitheatre(WorldContextObject, true, false, Amph.Scene, Amph.Source, Amph.Receivers, Manager, Error))
		{
			Cases.Add(MoveTemp(Amph));
		}
		else
		{
			UE_LOG(LogAcoustics, Warning, TEXT("E2: amphitheatre skipped: %s"), *Error);
		}
		RestoreAmphitheatre(WorldContextObject);
	}
	TArray<int32> Counts = RayCounts;
	Counts.Sort();
	NumSeeds = FMath::Max(1, NumSeeds);

	FString Csv = TEXT("scene,receiver,rays,seeds,t30_mean_s,t30_sd_s,t30_valid,c80_mean_db,c80_sd_db,edt_mean_s,edt_sd_s,trace_mean_s\n");
	FString Md = FString::Printf(TEXT("Seeds per N: %d. Cells: mean +- standard deviation over the seeds; dev = mean minus the mean at the largest N.\n\n"), NumSeeds);
	for (FSceneCase& Case : Cases)
	{
		// [receiver][count] -> stats
		TArray<TArray<FStat>> T30, C80, EDT;
		TArray<TArray<int32>> ValidT30;
		TArray<double> TraceMean;
		T30.SetNum(Case.Receivers.Num()); C80.SetNum(Case.Receivers.Num()); EDT.SetNum(Case.Receivers.Num()); ValidT30.SetNum(Case.Receivers.Num());
		for (int32 R = 0; R < Case.Receivers.Num(); ++R)
		{
			T30[R].SetNum(Counts.Num()); C80[R].SetNum(Counts.Num()); EDT[R].SetNum(Counts.Num()); ValidT30[R].Init(0, Counts.Num());
		}
		TraceMean.Init(0.0, Counts.Num());
		for (int32 C = 0; C < Counts.Num(); ++C)
		{
			for (int32 Seed = 1; Seed <= NumSeeds; ++Seed)
			{
				FRayTracerSettings Settings;
				Settings.NumRays = Counts[C];
				Settings.Seed = static_cast<uint32>(Seed);
				Settings.MaxPathsForVisualization = 0;
				const FSimulationResult Result = FRayTracer::Run(Case.Scene, Case.Source, Case.Receivers, Settings);
				TraceMean[C] += Result.Timing.TotalSeconds / NumSeeds;
				for (int32 R = 0; R < Result.Receivers.Num(); ++R)
				{
					const FBandMetrics& Band = Result.Receivers[R].Metrics.Band(0);
					if (Band.bT30Valid && Band.T30 < 10.0)
					{
						T30[R][C].Values.Add(Band.T30);
						++ValidT30[R][C];
					}
					C80[R][C].Values.Add(Band.C80);
					EDT[R][C].Values.Add(Band.EDT);
				}
			}
			UE_LOG(LogAcoustics, Display, TEXT("E2: %s, %d rays x %d seeds: %.2f s per run"), *Case.Name, Counts[C], NumSeeds, TraceMean[C]);
		}
		Md += FString::Printf(TEXT("**%s** (%d triangles)\n\n| N | trace, s |"), *Case.Name, Case.Scene.NumTriangles());
		FString Separator = TEXT("|---|---|");
		for (const FSolverReceiver& Receiver : Case.Receivers)
		{
			Md += FString::Printf(TEXT(" %s T30, s (dev) | %s C80, dB (dev) |"), *Receiver.Name, *Receiver.Name);
			Separator += TEXT("---|---|");
		}
		Md += TEXT("\n") + Separator + TEXT("\n");
		const int32 Last = Counts.Num() - 1;
		for (int32 C = 0; C < Counts.Num(); ++C)
		{
			Md += FString::Printf(TEXT("| %d | %.2f |"), Counts[C], TraceMean[C]);
			for (int32 R = 0; R < Case.Receivers.Num(); ++R)
			{
				const bool bT30 = T30[R][C].Values.Num() > 0;
				const bool bRefT30 = T30[R][Last].Values.Num() > 0;
				Md += FString::Printf(TEXT(" %s +- %s (%s) | %.2f +- %.2f (%+.2f) |"),
					*Num(T30[R][C].Mean(), bT30, 3), *Num(T30[R][C].StdDev(), bT30, 3), *Num(T30[R][C].Mean() - T30[R][Last].Mean(), bT30 && bRefT30, 3),
					C80[R][C].Mean(), C80[R][C].StdDev(), C80[R][C].Mean() - C80[R][Last].Mean());
				Csv += FString::Printf(TEXT("%s,%s,%d,%d,%.4f,%.4f,%d,%.3f,%.3f,%.4f,%.4f,%.3f\n"), *Case.Name, *Case.Receivers[R].Name, Counts[C], NumSeeds,
					T30[R][C].Mean(), T30[R][C].StdDev(), ValidT30[R][C], C80[R][C].Mean(), C80[R][C].StdDev(), EDT[R][C].Mean(), EDT[R][C].StdDev(), TraceMean[C]);
			}
			Md += TEXT("\n");
		}
		Md += TEXT("\n");
	}
	OutCsvPath = ExperimentDir() / TEXT("E2_convergence.csv");
	FFileHelper::SaveStringToFile(Csv, *OutCsvPath);
	return Md;
}

FString UAcousticsExperimentLibrary::RunMissedReflections(UObject* WorldContextObject, const TArray<int32>& RayCounts, int32 K, int32 SubdivisionLevel, float MixingTimeMs, int32 MaxBeamNodes, FString& OutCsvPath)
{
	FAcousticScene Scene;
	FVector3d Source;
	TArray<FSolverReceiver> Receivers;
	AAcousticSimulationManager* Manager = nullptr;
	FString Error;
	const bool bReady = PrepareAmphitheatre(WorldContextObject, false, false, Scene, Source, Receivers, Manager, Error);
	RestoreAmphitheatre(WorldContextObject);
	if (!bReady)
	{
		return FString::Printf(TEXT("E3: %s"), *Error);
	}
	FString Csv = TEXT("rays,receiver,k,t_mix_ms,beam_early,beam_dropped,found_by_rays,missed_by_rays,beam_early_energy,ray_early_energy,ray_vs_beam_pct,beam_s,ray_s,beam_nodes\n");
	FString Md = FString::Printf(TEXT("Amphitheatre empty, %d triangles, beams of order <= %d, t_mix %.0f ms after the emission, beam tree cap %d nodes. Cells: early specular reflections found / missed by the pure ray run (of the beam count), early energy rays vs beams.\n\n| rays |"),
		Scene.NumTriangles(), K, MixingTimeMs, MaxBeamNodes);
	FString Separator = TEXT("|---|");
	for (const FSolverReceiver& Receiver : Receivers)
	{
		Md += FString::Printf(TEXT(" %s found / missed of beams, energy |"), *Receiver.Name);
		Separator += TEXT("---|");
	}
	Md += TEXT(" beams, s | rays, s |\n") + Separator + TEXT("---|---|\n");
	for (const int32 Rays : RayCounts)
	{
		FHybridTracerSettings Settings = Manager->Settings.ToHybridTracerSettings();
		Settings.K = K;
		Settings.MixingTimeSeconds = MixingTimeMs / 1000.0;
		if (MaxBeamNodes > 0)
		{
			Settings.Beam.MaxBeamNodes = MaxBeamNodes;
		}
		Settings.bCountMissedReflections = true;
		Settings.Ray.NumRays = Rays;
		Settings.Ray.Seed = 1;
		Settings.Ray.MaxPathsForVisualization = 0;
		if (SubdivisionLevel >= 0)
		{
			Settings.Beam.SubdivisionLevel = SubdivisionLevel;
		}
		const FHybridResult Result = FHybridTracer::Run(Scene, Source, Receivers, Settings);
		if (!Result.IsValid())
		{
			return FString::Printf(TEXT("E3: hybrid run failed at %d rays"), Rays);
		}
		const int32 Nodes = Result.PureBeam->NumBeamNodes;
		Md += FString::Printf(TEXT("| %d |"), Rays);
		for (int32 R = 0; R < Result.Stats.Num(); ++R)
		{
			const FHybridReceiverStats& Stat = Result.Stats[R];
			const double Ratio = Stat.BeamEarlyEnergy > 0.0 ? 100.0 * Stat.RayExcludedEnergy / Stat.BeamEarlyEnergy : 0.0;
			Md += FString::Printf(TEXT(" %d / %d of %d, %.0f %% |"), Stat.NumFoundByRays, Stat.NumMissedByRays, Stat.NumBeamEarly, Ratio);
			Csv += FString::Printf(TEXT("%d,%s,%d,%.0f,%d,%d,%d,%d,%.6g,%.6g,%.1f,%.3f,%.3f,%d\n"), Rays, *Receivers[R].Name, K, MixingTimeMs, Stat.NumBeamEarly, Stat.NumBeamDropped,
				Stat.NumFoundByRays, Stat.NumMissedByRays, Stat.BeamEarlyEnergy, Stat.RayExcludedEnergy, Ratio, Result.BeamSeconds, Result.RaySeconds, Nodes);
		}
		Md += FString::Printf(TEXT(" %.2f | %.2f |\n"), Result.BeamSeconds, Result.RaySeconds);
		UE_LOG(LogAcoustics, Display, TEXT("E3: %d rays: beams %.2f s (%d nodes), rays %.2f s"), Rays, Result.BeamSeconds, Nodes, Result.RaySeconds);
	}
	OutCsvPath = ExperimentDir() / TEXT("E3_missed_reflections.csv");
	FFileHelper::SaveStringToFile(Csv, *OutCsvPath);
	return Md;
}

FString UAcousticsExperimentLibrary::RunEmptyDecayCheck(UObject* WorldContextObject, int32 NumRays, FString& OutCsvPath)
{
	FAcousticScene BaseScene;
	FVector3d Source;
	TArray<FSolverReceiver> Receivers;
	AAcousticSimulationManager* Manager = nullptr;
	FString Error;
	const bool bReady = PrepareAmphitheatre(WorldContextObject, false, false, BaseScene, Source, Receivers, Manager, Error);
	RestoreAmphitheatre(WorldContextObject);
	if (!bReady)
	{
		return FString::Printf(TEXT("E5b: %s"), *Error);
	}
	struct FCase { const TCHAR* Name; int32 MaxOrder; double TMax; double StoneScattering; };
	const FCase Cases[] = {
		{ TEXT("fast pass: order <= 200, T_max 3 s, s 0.2"), 200, 3.0, -1.0 },
		{ TEXT("order <= 2000, T_max 3 s, s 0.2"), 2000, 3.0, -1.0 },
		{ TEXT("order <= 2000, T_max 8 s, s 0.2"), 2000, 8.0, -1.0 },
		{ TEXT("order <= 2000, T_max 8 s, s 0.5"), 2000, 8.0, 0.5 },
		{ TEXT("order <= 2000, T_max 8 s, s 0.8"), 2000, 8.0, 0.8 },
	};
	FString Csv = TEXT("case,receiver,max_order,t_max_s,stone_scattering,c80_db,t30_s,t30_valid,t20_s,edt_s,trace_s\n");
	FString Md = FString::Printf(TEXT("Empty amphitheatre, %d rays, R1..R4. Cells: C80 dB / T30 s / EDT s.\n\n| case |"), NumRays);
	FString Separator = TEXT("|---|");
	for (const FSolverReceiver& Receiver : Receivers)
	{
		Md += FString::Printf(TEXT(" %s |"), *Receiver.Name);
		Separator += TEXT("---|");
	}
	Md += TEXT(" trace, s |\n") + Separator + TEXT("---|\n");
	for (const FCase& Case : Cases)
	{
		FAcousticScene Scene = BaseScene;
		if (Case.StoneScattering >= 0.0)
		{
			for (FAcousticMaterialProps& Material : Scene.Materials)
			{
				if (Material.Name.Equals(TEXT("Stone"), ESearchCase::IgnoreCase))
				{
					for (int32 Band = 0; Band < Acoustics::NumBands; ++Band)
					{
						Material.Scattering[Band] = Case.StoneScattering;
					}
				}
			}
		}
		FRayTracerSettings Settings;
		Settings.NumRays = NumRays;
		Settings.Seed = 1;
		Settings.MaxOrder = Case.MaxOrder;
		Settings.MaxTimeSeconds = Case.TMax;
		Settings.MaxPathsForVisualization = 0;
		const FSimulationResult Result = FRayTracer::Run(Scene, Source, Receivers, Settings);
		Md += FString::Printf(TEXT("| %s |"), Case.Name);
		for (int32 R = 0; R < Result.Receivers.Num(); ++R)
		{
			const FBandMetrics& Band = Result.Receivers[R].Metrics.Band(0);
			Md += FString::Printf(TEXT(" %.1f / %s / %.2f |"), Band.C80, *Num(Band.T30, Band.bT30Valid, 2), Band.EDT);
			Csv += FString::Printf(TEXT("%s,%s,%d,%.1f,%.2f,%.2f,%.3f,%d,%.3f,%.3f,%.2f\n"), *FString(Case.Name).Replace(TEXT(","), TEXT(";")), *Receivers[R].Name, Case.MaxOrder, Case.TMax,
				Case.StoneScattering < 0.0 ? 0.2 : Case.StoneScattering, Band.C80, Band.T30, Band.bT30Valid ? 1 : 0, Band.T20, Band.EDT, Result.Timing.TotalSeconds);
			// Where the energy arrives (relative to the direct sound): explains a C80 / D50 without a measurable decay.
			const FReceiverResult& Receiver = Result.Receivers[R];
			const FEnergyHistogram& H = Receiver.Histogram;
			const double Total = FMath::Max(H.TotalEnergy(0), 1e-30);
			const double Windows[] = { 0.0, 0.05, 0.08, 0.2, 0.5, 1.0, 2.0, 1.0e9 };
			FString Shares;
			for (int32 W = 0; W + 1 < UE_ARRAY_COUNT(Windows); ++W)
			{
				const int32 First = FMath::Clamp(H.BinIndex(Receiver.DirectArrivalSeconds + Windows[W]), 0, H.NumBins);
				const int32 End = Windows[W + 1] > 100.0 ? H.NumBins : FMath::Clamp(H.BinIndex(Receiver.DirectArrivalSeconds + Windows[W + 1]), 0, H.NumBins);
				Shares += FString::Printf(TEXT(" %.0f-%.0f ms %.1f %%;"), Windows[W] * 1000.0, FMath::Min(Windows[W + 1], 99.0) * 1000.0, 100.0 * H.SumRange(First, End, 0) / Total);
			}
			UE_LOG(LogAcoustics, Display, TEXT("E5b: %s %s: direct %.3g of %.3g (%.1f %%), hits %lld, last bin %d:%s"), Case.Name, *Receiver.Name, Receiver.DirectEnergy, Total,
				100.0 * Receiver.DirectEnergy / Total, Receiver.NumRayHits, H.LastNonEmptyBin(0), *Shares);
		}
		Md += FString::Printf(TEXT(" %.1f |\n"), Result.Timing.TotalSeconds);
		UE_LOG(LogAcoustics, Display, TEXT("E5b: %s: %.1f s"), Case.Name, Result.Timing.TotalSeconds);
	}
	OutCsvPath = ExperimentDir() / TEXT("E5b_empty_decay.csv");
	FFileHelper::SaveStringToFile(Csv, *OutCsvPath);
	return Md;
}
