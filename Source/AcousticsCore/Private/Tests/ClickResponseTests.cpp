// M9 (optional part): impulse response from a recorded click (FClickResponse), checked on a synthetic stereo recording.
#include "Misc/AutomationTest.h"
#include "Results/ClickResponse.h"
#include "Results/RoomMetrics.h"
#include "Solvers/AcousticRandom.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsClickResponseTest, "AcousticsLab.Core.Audio.ClickResponse",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsClickResponseTest::RunTest(const FString& Parameters)
{
	// Stereo at 48 kHz: 150 ms of pipeline latency (silence), the click, then from 4 ms on a noise tail decaying with T60 = 1.2 s.
	constexpr int32 SampleRate = 48000;
	constexpr int32 Channels = 2;
	constexpr double T60 = 1.2;
	constexpr double LatencySeconds = 0.150;
	const int32 Latency = FMath::RoundToInt32(LatencySeconds * SampleRate);
	const int32 TailStart = Latency + FMath::RoundToInt32(0.004 * SampleRate);
	const int32 NumFrames = Latency + 3 * SampleRate;
	TArray<float> Recording;
	Recording.SetNumZeroed(NumFrames * Channels);
	Recording[Latency * Channels + 0] = 0.8f;
	Recording[Latency * Channels + 1] = 0.6f;
	FAcousticRandom Random(7, 0);
	const double Decay = 3.0 * FMath::Loge(10.0) / T60;  // amplitude: 60 dB energy drop over T60
	for (int32 Frame = TailStart; Frame < NumFrames; ++Frame)
	{
		const double Envelope = 0.05 * FMath::Exp(-Decay * (Frame - TailStart) / SampleRate);
		for (int32 Channel = 0; Channel < Channels; ++Channel)
		{
			Recording[Frame * Channels + Channel] = static_cast<float>(Envelope * (2.0 * Random.NextDouble() - 1.0));
		}
	}

	FClickResponseSettings Settings;
	Settings.DirectDistance = 20.4;
	Settings.MaxTimeSeconds = 3.0;
	FReceiverResult Result;
	FString Error;
	int32 Onset = INDEX_NONE;
	if (!TestTrue(TEXT("synthetic recording analysed"), FClickResponse::Analyze(Recording, Channels, SampleRate, Settings, Result, Error, &Onset)))
	{
		AddError(Error);
		return false;
	}
	TestEqual(TEXT("onset found at the click (latency skipped)"), Onset, Latency);
	TestEqual(TEXT("direct arrival = d / c"), Result.DirectArrivalSeconds, 20.4 / Acoustics::SpeedOfSoundAir, 1e-12);
	TestEqual(TEXT("direct energy = the click (both channels)"), Result.DirectEnergy, 0.8 * 0.8 + 0.6 * 0.6, 1e-6);
	TestEqual(TEXT("first non-empty bin at d / c"), Result.Histogram.FirstNonEmptyBin(0), FMath::FloorToInt32(Result.DirectArrivalSeconds / Settings.BinSeconds));
	const FBandMetrics& Band = Result.Metrics.Band(0);
	TestTrue(TEXT("T30 valid"), Band.bT30Valid);
	TestTrue(TEXT("T20 valid"), Band.bT20Valid);
	TestEqual(TEXT("T30 within 5 % of the synthetic T60"), Band.T30, T60, 0.05 * T60);
	TestEqual(TEXT("T20 within 5 % of the synthetic T60"), Band.T20, T60, 0.05 * T60);
	TestTrue(TEXT("strong click: C50 > C80 - 6 dB and D50 in (0, 1)"), Band.C50 > Band.C80 - 6.0 && Band.D50 > 0.0 && Band.D50 < 1.0);

	// Silence and bad arguments are refused with a reason.
	TArray<float> Silence;
	Silence.SetNumZeroed(1000);
	FReceiverResult Unused;
	TestFalse(TEXT("silent recording refused"), FClickResponse::Analyze(Silence, 2, SampleRate, Settings, Unused, Error));
	TestTrue(TEXT("silence explained"), Error.Contains(TEXT("silent")));
	TestFalse(TEXT("zero channels refused"), FClickResponse::Analyze(Recording, 0, SampleRate, Settings, Unused, Error));
	return true;
}

#endif
