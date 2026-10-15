// Automation tests of the impulse-response synthesis and the WAV writer (M4, SPEC §2.9).
#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Results/EnergyHistogram.h"
#include "Results/IRSynthesizer.h"
#include "Results/RoomMetrics.h"
#include "Results/SimulationResult.h"
#include "Results/WavFile.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** Exponential decay of T60 seconds, bin integrals of A exp(-k t), starting at t = 0; TotalEnergy spread over the bins. */
	void FillExponentialDecay(FEnergyHistogram& Histogram, double T60, double TotalEnergy)
	{
		const double K = 6.0 * FMath::Loge(10.0) / T60;
		double Sum = 0.0;
		TArray<double> Values;
		Values.SetNumZeroed(Histogram.NumBins);
		for (int32 Bin = 0; Bin < Histogram.NumBins; ++Bin)
		{
			const double T0 = Histogram.BinStartTime(Bin);
			const double T1 = T0 + Histogram.BinSeconds;
			Values[Bin] = (FMath::Exp(-K * T0) - FMath::Exp(-K * T1)) / K;
			Sum += Values[Bin];
		}
		for (int32 Bin = 0; Bin < Histogram.NumBins; ++Bin)
		{
			Histogram.At(Bin, 0) = Values[Bin] * TotalEnergy / Sum;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsIRSynthesisTest, "AcousticsLab.Core.Audio.IRSynthesis",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsIRSynthesisTest::RunTest(const FString& Parameters)
{
	constexpr double T60 = 1.2;
	constexpr double DirectTime = 0.0205; // bin 20, sample 984 exactly
	constexpr double DirectEnergy = 0.01;

	FEnergyHistogram Histogram(0.001, 3.0);
	FillExponentialDecay(Histogram, T60, 0.5);
	const int32 DirectBin = Histogram.BinIndex(DirectTime);
	TestEqual(TEXT("direct bin"), DirectBin, 20);
	// Bin 20 carries the direct sound only, so its single sample can be checked exactly.
	Histogram.At(DirectBin, 0) = DirectEnergy;

	FIRSynthesisSettings Settings;
	Settings.SampleRate = 48000;
	Settings.Seed = 7;
	const FIRImpulse Impulses[] = { FIRImpulse(DirectTime, DirectEnergy) };

	const FSynthesizedIR IR = FIRSynthesizer::Synthesize(Histogram, Impulses, Settings);
	TestTrue(TEXT("valid"), IR.IsValid());
	TestEqual(TEXT("sample rate"), IR.SampleRate, 48000);
	TestEqual(TEXT("one impulse"), IR.NumImpulses, 1);

	// Length: the first bin below -60 dB on the Schroeder curve of the histogram, close to T60.
	TArray<double> LevelDb;
	RoomAcoustics::SchroederCurveDb(Histogram, 0, LevelDb);
	int32 ExpectedBins = Histogram.NumBins;
	for (int32 Bin = 0; Bin < LevelDb.Num(); ++Bin)
	{
		if (LevelDb[Bin] < -60.0)
		{
			ExpectedBins = Bin;
			break;
		}
	}
	TestEqual(TEXT("bins used = first bin below -60 dB"), IR.NumBinsUsed, ExpectedBins);
	TestTrue(TEXT("cut by level"), IR.bTruncatedByLevel);
	TestEqual(TEXT("samples = bins * 48"), IR.Samples.Num(), ExpectedBins * 48);
	TestTrue(TEXT("length near T60"), FMath::Abs(IR.TruncationSeconds - T60) < 0.05);
	TestTrue(TEXT("duration matches"), FMath::IsNearlyEqual(IR.DurationSeconds(), IR.TruncationSeconds, 1e-9));

	// Peak -3 dBFS.
	TestTrue(TEXT("peak -3 dBFS"), FMath::IsNearlyEqual(IR.Peak(), FIRSynthesizer::DbFSToLinear(-3.0), 1e-6));
	TestTrue(TEXT("peak dB"), FMath::IsNearlyEqual(IR.PeakDbFS(), -3.0, 1e-4));
	TestTrue(TEXT("gain consistent"), FMath::IsNearlyEqual(IR.PeakBeforeNormalization * IR.NormalizationGain, FIRSynthesizer::DbFSToLinear(-3.0), 1e-9));

	// Energy envelope: per-bin energy / gain^2 equals the histogram bins (impulse + noise included).
	TArray<double> EnergyPerBin;
	FIRSynthesizer::EnergyPerBin(IR, EnergyPerBin);
	TestEqual(TEXT("energy entries"), EnergyPerBin.Num(), IR.NumBinsUsed);
	const double GainSq = IR.NormalizationGain * IR.NormalizationGain;
	double MaxRelativeError = 0.0;
	for (int32 Bin = 0; Bin < EnergyPerBin.Num(); ++Bin)
	{
		const double Expected = Histogram.At(Bin, 0);
		const double Actual = EnergyPerBin[Bin] / GainSq;
		MaxRelativeError = FMath::Max(MaxRelativeError, FMath::Abs(Actual - Expected) / Expected);
	}
	TestTrue(FString::Printf(TEXT("per-bin energy follows the histogram (max relative error %.3g)"), MaxRelativeError), MaxRelativeError < 1e-6);
	TestTrue(TEXT("total energy before normalization"), FMath::IsNearlyEqual(IR.EnergyBeforeNormalization, Histogram.SumRange(0, IR.NumBinsUsed, 0), 1e-9));

	// The direct sound is one sample of sqrt(E) * gain at round(t fs); the rest of its bin is silent.
	const int32 DirectSample = 984;
	TestTrue(TEXT("direct impulse amplitude"), FMath::IsNearlyEqual(static_cast<double>(IR.Samples[DirectSample]), FMath::Sqrt(DirectEnergy) * IR.NormalizationGain, 1e-6));
	for (int32 Index = DirectBin * 48; Index < (DirectBin + 1) * 48; ++Index)
	{
		if (Index != DirectSample)
		{
			TestEqual(TEXT("direct bin has no noise"), IR.Samples[Index], 0.0f);
		}
	}
	// The direct sample is the peak: 0.1 * gain versus noise samples of at most sqrt(E_bin) * gain with E_bin < 0.01 in every bin.
	TestTrue(TEXT("direct sample is the peak"), FMath::IsNearlyEqual(static_cast<double>(IR.Samples[DirectSample]), IR.Peak(), 1e-9));

	// Determinism.
	const FSynthesizedIR Again = FIRSynthesizer::Synthesize(Histogram, Impulses, Settings);
	TestEqual(TEXT("same length"), Again.Samples.Num(), IR.Samples.Num());
	TestTrue(TEXT("same seed -> identical samples"), Again.Samples.Num() == IR.Samples.Num() && FMemory::Memcmp(Again.Samples.GetData(), IR.Samples.GetData(), IR.Samples.Num() * sizeof(float)) == 0);

	FIRSynthesisSettings OtherSeed = Settings;
	OtherSeed.Seed = 8;
	const FSynthesizedIR Other = FIRSynthesizer::Synthesize(Histogram, Impulses, OtherSeed);
	int32 NumDifferent = 0;
	for (int32 Index = 0; Index < FMath::Min(Other.Samples.Num(), IR.Samples.Num()); ++Index)
	{
		NumDifferent += Other.Samples[Index] != IR.Samples[Index] ? 1 : 0;
	}
	TestTrue(TEXT("other seed -> different samples"), NumDifferent > IR.Samples.Num() / 2);
	TArray<double> OtherEnergy;
	FIRSynthesizer::EnergyPerBin(Other, OtherEnergy);
	double MaxSeedError = 0.0;
	for (int32 Bin = 0; Bin < FMath::Min(OtherEnergy.Num(), EnergyPerBin.Num()); ++Bin)
	{
		MaxSeedError = FMath::Max(MaxSeedError, FMath::Abs(OtherEnergy[Bin] / (Other.NormalizationGain * Other.NormalizationGain) - Histogram.At(Bin, 0)) / Histogram.At(Bin, 0));
	}
	TestTrue(TEXT("other seed -> same energy envelope"), MaxSeedError < 1e-6);

	// Length cap.
	FIRSynthesisSettings Capped = Settings;
	Capped.MaxSeconds = 0.5;
	const FSynthesizedIR Short = FIRSynthesizer::Synthesize(Histogram, Impulses, Capped);
	TestEqual(TEXT("capped bins"), Short.NumBinsUsed, 500);
	TestEqual(TEXT("capped samples"), Short.Samples.Num(), 24000);
	TestFalse(TEXT("capped: not by level"), Short.bTruncatedByLevel);

	// Receiver convenience: same result from the receiver's direct-sound fields.
	FReceiverResult Receiver;
	Receiver.Histogram = Histogram;
	Receiver.DirectArrivalSeconds = DirectTime;
	Receiver.DirectEnergy = DirectEnergy;
	const FSynthesizedIR FromReceiver = FIRSynthesizer::SynthesizeForReceiver(Receiver, Settings);
	TestTrue(TEXT("receiver path identical"), FromReceiver.Samples.Num() == IR.Samples.Num() && FMemory::Memcmp(FromReceiver.Samples.GetData(), IR.Samples.GetData(), IR.Samples.Num() * sizeof(float)) == 0);
	Receiver.bDirectOccluded = true;
	const FSynthesizedIR Occluded = FIRSynthesizer::SynthesizeForReceiver(Receiver, Settings);
	TestEqual(TEXT("occluded: no impulse"), Occluded.NumImpulses, 0);

	// Empty histogram -> empty response, no crash.
	FEnergyHistogram Empty(0.001, 1.0);
	const FSynthesizedIR Nothing = FIRSynthesizer::Synthesize(Empty, TConstArrayView<FIRImpulse>(), Settings);
	TestFalse(TEXT("empty histogram -> no samples"), Nothing.IsValid());
	TestEqual(TEXT("empty histogram -> 0 bins"), Nothing.NumBinsUsed, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsWavRoundTripTest, "AcousticsLab.Core.Audio.WavRoundTrip",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsWavRoundTripTest::RunTest(const FString& Parameters)
{
	const FString Folder = FPaths::ProjectIntermediateDir() / TEXT("Tests");
	const FString PathPCM = Folder / TEXT("wav_roundtrip_pcm16.wav");
	const FString PathFloat = Folder / TEXT("wav_roundtrip_float32.wav");
	const FString PathStereo = Folder / TEXT("wav_roundtrip_stereo.wav");

	constexpr int32 SampleRate = 48000;
	TArray<float> Signal;
	Signal.SetNumUninitialized(4800);
	for (int32 Index = 0; Index < Signal.Num(); ++Index)
	{
		Signal[Index] = 0.7f * FMath::Sin(2.0f * PI * 1000.0f * Index / SampleRate) * (1.0f - Index / 4800.0f);
	}
	Signal[10] = 1.0f;
	Signal[20] = -1.0f;
	Signal[30] = 1.5f; // clamped to full scale by the PCM writer

	// 16-bit PCM.
	FString Error;
	TestTrue(TEXT("write pcm16"), FWavFile::Write(PathPCM, Signal, SampleRate, 1, EWavSampleFormat::PCM16, &Error));
	TestTrue(TEXT("pcm16 exists"), IFileManager::Get().FileExists(*PathPCM));
	TestEqual(TEXT("pcm16 file size"), static_cast<int32>(IFileManager::Get().FileSize(*PathPCM)), 44 + Signal.Num() * 2);
	FWavData PCM;
	TestTrue(TEXT("read pcm16"), FWavFile::Read(PathPCM, PCM, &Error));
	TestEqual(TEXT("pcm16 tag"), static_cast<int32>(PCM.FormatTag), 1);
	TestEqual(TEXT("pcm16 channels"), PCM.NumChannels, 1);
	TestEqual(TEXT("pcm16 rate"), PCM.SampleRate, SampleRate);
	TestEqual(TEXT("pcm16 bits"), PCM.BitsPerSample, 16);
	TestEqual(TEXT("pcm16 frames"), PCM.NumFrames(), Signal.Num());
	TestTrue(TEXT("pcm16 duration"), FMath::IsNearlyEqual(PCM.DurationSeconds(), 0.1, 1e-9));
	float MaxError = 0.0f;
	for (int32 Index = 0; Index < FMath::Min(PCM.Samples.Num(), Signal.Num()); ++Index)
	{
		MaxError = FMath::Max(MaxError, FMath::Abs(PCM.Samples[Index] - FMath::Clamp(Signal[Index], -1.0f, 1.0f)));
	}
	// The writer scales by 32767 (full scale maps to the largest code) and the reader divides by 32768: up to 1.5 codes apart.
	TestTrue(FString::Printf(TEXT("pcm16 samples within 1.5 codes (max error %.3g)"), MaxError), MaxError <= 1.5f / 32768.0f + 1e-6f);
	TestTrue(TEXT("pcm16 clamp"), FMath::IsNearlyEqual(PCM.Samples[30], 32767.0f / 32768.0f, 1e-6f));

	// 32-bit float: exact.
	TestTrue(TEXT("write float32"), FWavFile::Write(PathFloat, Signal, SampleRate, 1, EWavSampleFormat::Float32, &Error));
	TestEqual(TEXT("float32 file size"), static_cast<int32>(IFileManager::Get().FileSize(*PathFloat)), 12 + (8 + 18) + (8 + 4) + 8 + Signal.Num() * 4);
	FWavData Float;
	TestTrue(TEXT("read float32"), FWavFile::Read(PathFloat, Float, &Error));
	TestEqual(TEXT("float32 tag"), static_cast<int32>(Float.FormatTag), 3);
	TestEqual(TEXT("float32 bits"), Float.BitsPerSample, 32);
	TestEqual(TEXT("float32 frames"), Float.NumFrames(), Signal.Num());
	TestTrue(TEXT("float32 samples exact"), Float.Samples.Num() == Signal.Num() && FMemory::Memcmp(Float.Samples.GetData(), Signal.GetData(), Signal.Num() * sizeof(float)) == 0);
	TestTrue(TEXT("float32 peak"), FMath::IsNearlyEqual(Float.Peak(), 1.5f, 1e-6f));

	// Stereo interleaving keeps the frame count and the channel order.
	TArray<float> Stereo;
	for (int32 Frame = 0; Frame < 100; ++Frame)
	{
		Stereo.Add(Frame / 100.0f);
		Stereo.Add(-Frame / 100.0f);
	}
	TestTrue(TEXT("write stereo"), FWavFile::Write(PathStereo, Stereo, 44100, 2, EWavSampleFormat::PCM16, &Error));
	FWavData StereoData;
	TestTrue(TEXT("read stereo"), FWavFile::Read(PathStereo, StereoData, &Error));
	TestEqual(TEXT("stereo channels"), StereoData.NumChannels, 2);
	TestEqual(TEXT("stereo frames"), StereoData.NumFrames(), 100);
	TestEqual(TEXT("stereo rate"), StereoData.SampleRate, 44100);
	TestTrue(TEXT("stereo order"), StereoData.Samples.Num() == 200 && FMath::IsNearlyEqual(StereoData.Samples[2 * 50], 0.5f, 1e-4f) && FMath::IsNearlyEqual(StereoData.Samples[2 * 50 + 1], -0.5f, 1e-4f));

	// Mismatched frame count and garbage input are rejected.
	TestFalse(TEXT("odd stereo rejected"), FWavFile::Write(PathStereo, TConstArrayView<float>(Stereo.GetData(), 3), 44100, 2, EWavSampleFormat::PCM16, &Error));
	FWavData Garbage;
	const uint8 NotWav[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };
	TestFalse(TEXT("garbage rejected"), FWavFile::Decode(NotWav, Garbage, &Error));

	IFileManager::Get().Delete(*PathPCM);
	IFileManager::Get().Delete(*PathFloat);
	IFileManager::Get().Delete(*PathStereo);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
