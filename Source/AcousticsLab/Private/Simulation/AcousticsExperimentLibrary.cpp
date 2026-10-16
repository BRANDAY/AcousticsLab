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
