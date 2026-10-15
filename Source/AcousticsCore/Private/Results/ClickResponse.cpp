#include "Results/ClickResponse.h"

#include "AcousticsCore.h"
#include "Results/RoomMetrics.h"

bool FClickResponse::Analyze(TConstArrayView<float> Interleaved, int32 NumChannels, int32 SampleRate, const FClickResponseSettings& Settings,
	FReceiverResult& Out, FString& OutError, int32* OutOnsetFrame)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FClickResponse::Analyze);
	if (NumChannels <= 0 || SampleRate <= 0 || Interleaved.Num() < NumChannels || Settings.BinSeconds <= 0.0 || Settings.SpeedOfSound <= 0.0)
	{
		OutError = TEXT("invalid recording or settings");
		return false;
	}
	const int32 NumFrames = Interleaved.Num() / NumChannels;
	TArray<double> Energy;
	Energy.SetNumUninitialized(NumFrames);
	double Peak = 0.0;
	for (int32 Frame = 0; Frame < NumFrames; ++Frame)
	{
		double Sum = 0.0;
		for (int32 Channel = 0; Channel < NumChannels; ++Channel)
		{
			const double Sample = Interleaved[Frame * NumChannels + Channel];
			Sum += Sample * Sample;
		}
		Energy[Frame] = Sum;
		Peak = FMath::Max(Peak, Sum);
	}
	if (Peak <= 0.0)
	{
		OutError = TEXT("silent recording");
		return false;
	}
	const double Threshold = Peak * FMath::Pow(10.0, Settings.OnsetThresholdDb / 10.0);
	int32 Onset = 0;
	while (Onset < NumFrames && Energy[Onset] < Threshold)
	{
		++Onset;
	}
	if (OutOnsetFrame)
	{
		*OutOnsetFrame = Onset;
	}

	Out.DirectDistance = Settings.DirectDistance;
	Out.DirectArrivalSeconds = Settings.DirectDistance / Settings.SpeedOfSound;
	Out.bDirectOccluded = false;
	Out.DirectEnergy = 0.0;
	Out.Histogram.Init(Settings.BinSeconds, Settings.MaxTimeSeconds);
	const int32 DirectFrames = FMath::Max(1, FMath::RoundToInt32(Settings.DirectWindowSeconds * SampleRate));
	for (int32 Frame = Onset; Frame < NumFrames; ++Frame)
	{
		const double Time = Out.DirectArrivalSeconds + static_cast<double>(Frame - Onset) / SampleRate;
		if (!Out.Histogram.AddEnergy(Time, 0, Energy[Frame]))
		{
			break;  // beyond MaxTimeSeconds
		}
		if (Frame - Onset < DirectFrames)
		{
			Out.DirectEnergy += Energy[Frame];
		}
	}
	Out.Metrics = FRoomMetrics::Compute(Out.Histogram, Out.DirectArrivalSeconds);
	return true;
}
