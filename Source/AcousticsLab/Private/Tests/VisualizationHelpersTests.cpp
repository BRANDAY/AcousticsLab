// M3 game-module helpers: viridis colormap, heatmap metric sampling, path colors and opacity (no world needed).
#include "Misc/AutomationTest.h"
#include "Results/RoomMetrics.h"
#include "Results/SimulationResult.h"
#include "Visualization/HeatmapComponent.h"
#include "Visualization/RayPathVisualizerComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAcousticsVisualizationHelpersTest, "AcousticsLab.Core.Visualization.Helpers",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAcousticsVisualizationHelpersTest::RunTest(const FString& Parameters)
{
	// Viridis: dark purple at 0, yellow at 1, luminance rising monotonically, every channel in [0, 1].
	{
		const FLinearColor Start = UHeatmapComponent::Viridis(0.0f);
		const FLinearColor End = UHeatmapComponent::Viridis(1.0f);
		TestTrue(TEXT("viridis(0) is dark purple"), Start.R < 0.1f && Start.G < 0.01f && Start.B > 0.05f && Start.B > Start.R);
		TestTrue(TEXT("viridis(1) is yellow"), End.R > 0.9f && End.G > 0.7f && End.B < 0.05f);
		float LastLuminance = -1.0f;
		bool bMonotonic = true;
		bool bInRange = true;
		for (int32 Step = 0; Step <= 100; ++Step)
		{
			const FLinearColor Color = UHeatmapComponent::Viridis(Step / 100.0f);
			bInRange &= Color.R >= 0.0f && Color.R <= 1.0f && Color.G >= 0.0f && Color.G <= 1.0f && Color.B >= 0.0f && Color.B <= 1.0f;
			const float Luminance = Color.GetLuminance();
			bMonotonic &= Luminance >= LastLuminance - 1e-4f;
			LastLuminance = Luminance;
		}
		TestTrue(TEXT("viridis channels stay in [0, 1]"), bInRange);
		TestTrue(TEXT("viridis luminance never decreases"), bMonotonic);
		TestTrue(TEXT("viridis clamps below 0"), UHeatmapComponent::Viridis(-1.0f).Equals(Start));
		TestTrue(TEXT("viridis clamps above 1"), UHeatmapComponent::Viridis(2.0f).Equals(End));
	}

	// Heatmap samples from a receiver result.
	{
		FReceiverResult Receiver;
		Receiver.Radius = 0.5;
		Receiver.DirectDistance = 2.0;
		Receiver.DirectEnergy = 0.25 / (4.0 * 4.0); // r^2 / (4 d^2)
		Receiver.Metrics.bValid = true;
		Receiver.Metrics.Bands[0].C80 = -3.5;
		Receiver.Metrics.Bands[0].T30 = 1.25;
		Receiver.Metrics.Bands[0].bT30Valid = true;
		Receiver.Metrics.Bands[0].EDT = 1.1;
		Receiver.Metrics.Bands[0].bEDTValid = false;

		const FHeatmapSample Sample = UHeatmapComponent::MakeSample(Receiver, 100.0);
		float Value = 0.0f;
		TestTrue(TEXT("direct SPL valid"), UHeatmapComponent::SampleValue(Sample, EHeatmapMetric::DirectSPL, Value));
		TestTrue(FString::Printf(TEXT("direct SPL at 2 m is 100 - 11 - 6 dB (got %.2f)"), Value), FMath::IsNearlyEqual(Value, 100.0f - 10.99f - 6.02f, 0.05f));
		TestTrue(TEXT("C80 valid"), UHeatmapComponent::SampleValue(Sample, EHeatmapMetric::C80, Value));
		TestTrue(TEXT("C80 value"), FMath::IsNearlyEqual(Value, -3.5f, 1e-5f));
		TestTrue(TEXT("T30 valid"), UHeatmapComponent::SampleValue(Sample, EHeatmapMetric::T30, Value));
		TestTrue(TEXT("T30 value"), FMath::IsNearlyEqual(Value, 1.25f, 1e-5f));
		TestFalse(TEXT("EDT invalid when the fit failed"), UHeatmapComponent::SampleValue(Sample, EHeatmapMetric::EDT, Value));

		Receiver.DirectEnergy = 0.0; // occluded
		Receiver.Metrics.Bands[0].C80 = RoomAcoustics::ClarityLimitDb;
		const FHeatmapSample Occluded = UHeatmapComponent::MakeSample(Receiver, 100.0);
		TestFalse(TEXT("occluded direct sound has no SPL"), UHeatmapComponent::SampleValue(Occluded, EHeatmapMetric::DirectSPL, Value));
		TestFalse(TEXT("one-sided clarity is not a value"), UHeatmapComponent::SampleValue(Occluded, EHeatmapMetric::C80, Value));
		TestEqual(TEXT("four metrics"), UHeatmapComponent::AllMetrics().Num(), 4);
		TestEqual(TEXT("T30 units"), UHeatmapComponent::MetricUnits(EHeatmapMetric::T30), FString(TEXT("s")));
	}

	// Path colors by order and opacity by energy.
	{
		TestTrue(TEXT("order 0 is white"), URayPathVisualizerComponent::ColorForOrder(0).Equals(FLinearColor::White));
		const FLinearColor First = URayPathVisualizerComponent::ColorForOrder(1);
		const FLinearColor Second = URayPathVisualizerComponent::ColorForOrder(2);
		const FLinearColor Third = URayPathVisualizerComponent::ColorForOrder(3);
		TestTrue(TEXT("order 1 is yellow"), First.R > 0.9f && First.G > 0.8f && First.B < 0.2f);
		TestTrue(TEXT("order 2 is orange"), Second.R > 0.9f && Second.G > 0.3f && Second.G < 0.7f && Second.B < 0.1f);
		TestTrue(TEXT("order 3 is red"), Third.R > 0.9f && Third.G < 0.2f && Third.B < 0.1f);
		TestTrue(TEXT("order 10 is red too"), URayPathVisualizerComponent::ColorForOrder(10).Equals(Third));

		TestTrue(TEXT("full energy is opaque"), FMath::IsNearlyEqual(URayPathVisualizerComponent::OpacityForEnergy(1.0, 40.0f, 0.1f), 1.0f, 1e-5f));
		TestTrue(TEXT("-20 dB is half way"), FMath::IsNearlyEqual(URayPathVisualizerComponent::OpacityForEnergy(0.01, 40.0f, 0.1f), 0.55f, 1e-4f));
		TestTrue(TEXT("-40 dB is the minimum"), FMath::IsNearlyEqual(URayPathVisualizerComponent::OpacityForEnergy(1e-4, 40.0f, 0.1f), 0.1f, 1e-5f));
		TestTrue(TEXT("below the range stays at the minimum"), FMath::IsNearlyEqual(URayPathVisualizerComponent::OpacityForEnergy(1e-9, 40.0f, 0.1f), 0.1f, 1e-5f));
		TestTrue(TEXT("zero energy is the minimum"), FMath::IsNearlyEqual(URayPathVisualizerComponent::OpacityForEnergy(0.0, 40.0f, 0.1f), 0.1f, 1e-5f));
	}

	// Path sample lookup.
	{
		FPathSample Path;
		FPathReceiverHit& HitB = Path.ReceiverHits.AddDefaulted_GetRef();
		HitB.ReceiverIndex = 1;
		HitB.Order = 2;
		FPathReceiverHit& HitA = Path.ReceiverHits.AddDefaulted_GetRef();
		HitA.ReceiverIndex = 0;
		HitA.Order = 5;
		TestTrue(TEXT("any receiver -> first hit"), Path.FindReceiverHit(INDEX_NONE) == &Path.ReceiverHits[0]);
		TestTrue(TEXT("receiver 0 -> its own hit"), Path.FindReceiverHit(0) == &Path.ReceiverHits[1]);
		TestTrue(TEXT("receiver 7 -> none"), Path.FindReceiverHit(7) == nullptr);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
