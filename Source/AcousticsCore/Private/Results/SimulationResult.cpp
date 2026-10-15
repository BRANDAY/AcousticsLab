#include "Results/SimulationResult.h"

const FReceiverResult* FSimulationResult::FindReceiver(const FString& Name) const
{
	return Receivers.FindByPredicate([&Name](const FReceiverResult& Receiver) { return Receiver.Name == Name; });
}

FString FSimulationResult::Describe() const
{
	if (!bSuccess)
	{
		return FString::Printf(TEXT("%s: %s%s"), *SolverName, bCancelled ? TEXT("cancelled") : TEXT("failed"), Error.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (%s)"), *Error));
	}
	FString Text = FString::Printf(TEXT("%s: %d rays, %d triangles, %lld segments, mean order %.1f, %d escaped, %d workers, %.3f s (setup %.3f, trace %.3f, metrics %.3f)"),
		*SolverName, NumRays, NumTriangles, NumSegments, MeanOrder, NumRaysEscaped, Timing.NumWorkers, Timing.TotalSeconds, Timing.SetupSeconds, Timing.TraceSeconds, Timing.MetricsSeconds);
	for (const FReceiverResult& Receiver : Receivers)
	{
		Text += FString::Printf(TEXT("\n  %s at %.2f m (direct %.2f ms%s, E %.4g, %lld ray hits): %s"),
			*Receiver.Name, Receiver.DirectDistance, Receiver.DirectArrivalSeconds * 1000.0, Receiver.bDirectOccluded ? TEXT(" occluded") : TEXT(""),
			Receiver.DirectEnergy, Receiver.NumRayHits, *Receiver.Metrics.Band(0).ToString());
	}
	return Text;
}

void FSolverProgress::Reset(int64 InTotal)
{
	Completed.store(0, std::memory_order_relaxed);
	Total.store(InTotal, std::memory_order_relaxed);
	bCancelRequested.store(false, std::memory_order_relaxed);
}

float FSolverProgress::Fraction() const
{
	const int64 TotalValue = Total.load(std::memory_order_relaxed);
	if (TotalValue <= 0)
	{
		return 0.0f;
	}
	return FMath::Clamp(static_cast<float>(static_cast<double>(Completed.load(std::memory_order_relaxed)) / static_cast<double>(TotalValue)), 0.0f, 1.0f);
}
