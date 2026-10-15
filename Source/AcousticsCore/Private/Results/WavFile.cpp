#include "Results/WavFile.h"

#include "AcousticsCore.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	constexpr uint16 WaveFormatPCM = 1;
	constexpr uint16 WaveFormatIEEEFloat = 3;
	constexpr uint16 WaveFormatExtensible = 0xFFFE;

	void PutU16(TArray<uint8>& Out, uint16 Value)
	{
		Out.Add(static_cast<uint8>(Value & 0xFF));
		Out.Add(static_cast<uint8>((Value >> 8) & 0xFF));
	}

	void PutU32(TArray<uint8>& Out, uint32 Value)
	{
		PutU16(Out, static_cast<uint16>(Value & 0xFFFF));
		PutU16(Out, static_cast<uint16>((Value >> 16) & 0xFFFF));
	}

	void PutTag(TArray<uint8>& Out, const char* Tag)
	{
		Out.Append(reinterpret_cast<const uint8*>(Tag), 4);
	}

	uint16 GetU16(const uint8* Bytes)
	{
		return static_cast<uint16>(Bytes[0] | (Bytes[1] << 8));
	}

	uint32 GetU32(const uint8* Bytes)
	{
		return static_cast<uint32>(Bytes[0]) | (static_cast<uint32>(Bytes[1]) << 8) | (static_cast<uint32>(Bytes[2]) << 16) | (static_cast<uint32>(Bytes[3]) << 24);
	}

	bool TagIs(const uint8* Bytes, const char* Tag)
	{
		return FMemory::Memcmp(Bytes, Tag, 4) == 0;
	}

	bool Fail(FString* OutError, const FString& Message)
	{
		if (OutError)
		{
			*OutError = Message;
		}
		return false;
	}
}

float FWavData::Peak() const
{
	float Max = 0.0f;
	for (const float Sample : Samples)
	{
		Max = FMath::Max(Max, FMath::Abs(Sample));
	}
	return Max;
}

void FWavFile::Encode(TArray<uint8>& OutBytes, TConstArrayView<float> Samples, int32 SampleRate, int32 NumChannels, EWavSampleFormat Format)
{
	const int32 SampleBytes = BytesPerSample(Format);
	const uint16 BlockAlign = static_cast<uint16>(NumChannels * SampleBytes);
	const uint32 DataSize = static_cast<uint32>(Samples.Num()) * SampleBytes;
	const bool bFloat = Format == EWavSampleFormat::Float32;
	const uint32 FmtSize = bFloat ? 18 : 16;
	const uint32 FactSize = bFloat ? 4 : 0;
	const uint32 RiffSize = 4 + (8 + FmtSize) + (bFloat ? 8 + FactSize : 0) + (8 + DataSize) + (DataSize & 1);

	OutBytes.Reset(static_cast<int32>(8 + RiffSize));
	PutTag(OutBytes, "RIFF");
	PutU32(OutBytes, RiffSize);
	PutTag(OutBytes, "WAVE");

	PutTag(OutBytes, "fmt ");
	PutU32(OutBytes, FmtSize);
	PutU16(OutBytes, FormatTagOf(Format));
	PutU16(OutBytes, static_cast<uint16>(NumChannels));
	PutU32(OutBytes, static_cast<uint32>(SampleRate));
	PutU32(OutBytes, static_cast<uint32>(SampleRate) * BlockAlign);
	PutU16(OutBytes, BlockAlign);
	PutU16(OutBytes, static_cast<uint16>(SampleBytes * 8));
	if (bFloat)
	{
		PutU16(OutBytes, 0); // cbSize
		PutTag(OutBytes, "fact");
		PutU32(OutBytes, FactSize);
		PutU32(OutBytes, NumChannels > 0 ? static_cast<uint32>(Samples.Num() / NumChannels) : 0u);
	}

	PutTag(OutBytes, "data");
	PutU32(OutBytes, DataSize);
	if (bFloat)
	{
		const int32 Offset = OutBytes.AddUninitialized(static_cast<int32>(DataSize));
		FMemory::Memcpy(OutBytes.GetData() + Offset, Samples.GetData(), DataSize);
	}
	else
	{
		for (const float Sample : Samples)
		{
			const float Clamped = FMath::Clamp(Sample, -1.0f, 1.0f);
			const int32 Code = FMath::Clamp(FMath::RoundToInt32(Clamped * 32767.0f), -32768, 32767);
			PutU16(OutBytes, static_cast<uint16>(static_cast<int16>(Code)));
		}
	}
	if (DataSize & 1)
	{
		OutBytes.Add(0);
	}
}

bool FWavFile::Write(const FString& Path, TConstArrayView<float> Samples, int32 SampleRate, int32 NumChannels, EWavSampleFormat Format, FString* OutError)
{
	if (SampleRate <= 0 || NumChannels <= 0)
	{
		return Fail(OutError, FString::Printf(TEXT("invalid WAV layout: %d Hz, %d channels"), SampleRate, NumChannels));
	}
	if (Samples.Num() % NumChannels != 0)
	{
		return Fail(OutError, FString::Printf(TEXT("%d samples are not a whole number of %d-channel frames"), Samples.Num(), NumChannels));
	}
	TArray<uint8> Bytes;
	Encode(Bytes, Samples, SampleRate, NumChannels, Format);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	if (!FFileHelper::SaveArrayToFile(Bytes, *Path))
	{
		return Fail(OutError, FString::Printf(TEXT("could not write %s"), *Path));
	}
	return true;
}

bool FWavFile::Read(const FString& Path, FWavData& OutData, FString* OutError)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path))
	{
		return Fail(OutError, FString::Printf(TEXT("could not read %s"), *Path));
	}
	return Decode(Bytes, OutData, OutError);
}

bool FWavFile::Decode(TConstArrayView<uint8> Bytes, FWavData& OutData, FString* OutError)
{
	OutData = FWavData();
	if (Bytes.Num() < 12 || !TagIs(Bytes.GetData(), "RIFF") || !TagIs(Bytes.GetData() + 8, "WAVE"))
	{
		return Fail(OutError, TEXT("not a RIFF/WAVE file"));
	}

	bool bHaveFormat = false;
	const uint8* Data = nullptr;
	uint32 DataSize = 0;
	int32 Offset = 12;
	while (Offset + 8 <= Bytes.Num())
	{
		const uint8* Chunk = Bytes.GetData() + Offset;
		const uint32 ChunkSize = GetU32(Chunk + 4);
		const int32 Payload = Offset + 8;
		const int64 PayloadEnd = static_cast<int64>(Payload) + ChunkSize;
		if (TagIs(Chunk, "fmt "))
		{
			if (ChunkSize < 16 || PayloadEnd > Bytes.Num())
			{
				return Fail(OutError, TEXT("truncated fmt chunk"));
			}
			const uint8* Fmt = Bytes.GetData() + Payload;
			OutData.FormatTag = GetU16(Fmt);
			OutData.NumChannels = GetU16(Fmt + 2);
			OutData.SampleRate = static_cast<int32>(GetU32(Fmt + 4));
			OutData.BitsPerSample = GetU16(Fmt + 14);
			if (OutData.FormatTag == WaveFormatExtensible)
			{
				// cbSize (2), valid bits (2), channel mask (4), sub-format GUID whose first two bytes are the format tag.
				if (ChunkSize < 26)
				{
					return Fail(OutError, TEXT("truncated extensible fmt chunk"));
				}
				OutData.FormatTag = GetU16(Fmt + 24);
			}
			bHaveFormat = true;
		}
		else if (TagIs(Chunk, "data"))
		{
			Data = Bytes.GetData() + Payload;
			DataSize = static_cast<uint32>(FMath::Min<int64>(ChunkSize, Bytes.Num() - Payload));
		}
		Offset = static_cast<int32>(FMath::Min<int64>(PayloadEnd + (ChunkSize & 1), Bytes.Num()));
		if (bHaveFormat && Data)
		{
			break;
		}
	}
	if (!bHaveFormat)
	{
		return Fail(OutError, TEXT("no fmt chunk"));
	}
	if (!Data)
	{
		return Fail(OutError, TEXT("no data chunk"));
	}
	if (OutData.NumChannels <= 0 || OutData.SampleRate <= 0)
	{
		return Fail(OutError, TEXT("invalid channel count or sample rate"));
	}

	const int32 BytesPer = OutData.BitsPerSample / 8;
	if (BytesPer <= 0)
	{
		return Fail(OutError, FString::Printf(TEXT("unsupported bit depth %d"), OutData.BitsPerSample));
	}
	const int32 NumSamples = static_cast<int32>(DataSize / BytesPer);
	OutData.Samples.SetNumUninitialized(NumSamples);

	if (OutData.FormatTag == WaveFormatIEEEFloat)
	{
		if (BytesPer == 4)
		{
			FMemory::Memcpy(OutData.Samples.GetData(), Data, static_cast<SIZE_T>(NumSamples) * 4);
		}
		else if (BytesPer == 8)
		{
			for (int32 Index = 0; Index < NumSamples; ++Index)
			{
				double Value = 0.0;
				FMemory::Memcpy(&Value, Data + static_cast<SIZE_T>(Index) * 8, 8);
				OutData.Samples[Index] = static_cast<float>(Value);
			}
		}
		else
		{
			return Fail(OutError, FString::Printf(TEXT("unsupported float width %d"), OutData.BitsPerSample));
		}
		return true;
	}
	if (OutData.FormatTag != WaveFormatPCM)
	{
		return Fail(OutError, FString::Printf(TEXT("unsupported format tag %u"), OutData.FormatTag));
	}
	for (int32 Index = 0; Index < NumSamples; ++Index)
	{
		const uint8* Sample = Data + static_cast<SIZE_T>(Index) * BytesPer;
		float Value = 0.0f;
		switch (BytesPer)
		{
		case 1: Value = (static_cast<int32>(Sample[0]) - 128) / 128.0f; break;
		case 2: Value = static_cast<int16>(GetU16(Sample)) / 32768.0f; break;
		case 3: Value = static_cast<int32>((static_cast<uint32>(Sample[0]) << 8) | (static_cast<uint32>(Sample[1]) << 16) | (static_cast<uint32>(Sample[2]) << 24)) / 2147483648.0f; break;
		case 4: Value = static_cast<int32>(GetU32(Sample)) / 2147483648.0f; break;
		default: return Fail(OutError, FString::Printf(TEXT("unsupported PCM width %d"), OutData.BitsPerSample));
		}
		OutData.Samples[Index] = Value;
	}
	return true;
}
