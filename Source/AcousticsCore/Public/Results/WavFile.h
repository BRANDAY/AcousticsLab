// Minimal RIFF/WAVE writer and reader for impulse responses and test signals (SPEC §2.9 export). Core only.
#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"

enum class EWavSampleFormat : uint8
{
	/** 16-bit signed integer PCM (WAVE_FORMAT_PCM). */
	PCM16,
	/** 32-bit IEEE float (WAVE_FORMAT_IEEE_FLOAT). */
	Float32
};

/** What a WAV file contained, samples converted to float in [-1, 1], interleaved. */
struct ACOUSTICSCORE_API FWavData
{
	/** 1 = PCM, 3 = IEEE float (an extensible header reports its sub-format). */
	uint16 FormatTag = 0;
	int32 NumChannels = 0;
	int32 SampleRate = 0;
	int32 BitsPerSample = 0;
	TArray<float> Samples;

	int32 NumFrames() const { return NumChannels > 0 ? Samples.Num() / NumChannels : 0; }
	double DurationSeconds() const { return SampleRate > 0 ? static_cast<double>(NumFrames()) / SampleRate : 0.0; }
	float Peak() const;
};

class ACOUSTICSCORE_API FWavFile
{
public:
	/** Writes a canonical RIFF/WAVE file (fmt + data, plus a fact chunk for float data); creates the directory. */
	static bool Write(const FString& Path, TConstArrayView<float> Samples, int32 SampleRate, int32 NumChannels, EWavSampleFormat Format, FString* OutError = nullptr);

	/** The bytes Write would store. Floats are clamped to [-1, 1] for PCM16 and rounded to the nearest code. */
	static void Encode(TArray<uint8>& OutBytes, TConstArrayView<float> Samples, int32 SampleRate, int32 NumChannels, EWavSampleFormat Format);

	/** Parses PCM 8 / 16 / 24 / 32 bit and float 32 / 64 bit files, plain or extensible headers. */
	static bool Read(const FString& Path, FWavData& OutData, FString* OutError = nullptr);
	static bool Decode(TConstArrayView<uint8> Bytes, FWavData& OutData, FString* OutError = nullptr);

	static int32 BytesPerSample(EWavSampleFormat Format) { return Format == EWavSampleFormat::PCM16 ? 2 : 4; }
	static uint16 FormatTagOf(EWavSampleFormat Format) { return Format == EWavSampleFormat::PCM16 ? 1 : 3; }
};
