// Building blocks of the beam-tree build shared by FBeamTracer and FParallelBeamTracer (M7, SPEC §2.7). Private to
// AcousticsCore: the tree is built as one breadth-first subtree per root beam, then merged in a fixed order.
#pragma once

#include "CoreMinimal.h"
#include "Solvers/BeamTracer.h"
#include <atomic>

struct FAcousticScene;
struct FSolverProgress;

/**
 * Read-only state of one tree build, shared by every subtree build (the atomics are the only shared writes: the
 * memory guard and the log limiter, neither of which influences a normal result).
 */
struct FBeamBuildContext
{
	const FAcousticScene* Scene = nullptr;
	const FBeamTracerSettings* Settings = nullptr;
	FSolverProgress* Progress = nullptr;

	FVector3d Source = FVector3d::ZeroVector;
	int32 SourceMedium = 0;
	int32 NumRootBeams = 0;
	double EscapeLength = 10.0;
	bool bRefraction = false;

	/** Emission directions, 3 per root beam (FBeamTracer::EmissionDirections). */
	TArray<FVector3d> Directions;

	/** Surface (coplanar triangle group) of every triangle: reflection sequences are hashed by surface. */
	TArray<int32> TriangleSurface;

	/** Root beams that are traced (all of them, or those inside the cone of a directional source), ascending. */
	TArray<int32> EmittedRoots;

	/**
	 * Memory guard: the subtrees together may create at most this many nodes (2 x MaxBeamNodes). Every subtree also stops
	 * at MaxBeamNodes by itself, so the guard only fires when many subtrees are large; the build is then cut wherever the
	 * workers happen to be (logged as scheduling-dependent).
	 */
	int64 NodeCap = 0;
	mutable std::atomic<int64> NodeCount{ 0 };
	mutable std::atomic<bool> bOverflow{ false };

	/** Unresolved pieces are logged for the first few only, across all subtrees. */
	mutable std::atomic<int32> UnresolvedLogs{ 0 };
};

namespace AcousticsBeamBuild
{
	/** Emission directions, surfaces, emitted roots and limits of a build. */
	void InitContext(FBeamBuildContext& Context, const FAcousticScene& Scene, const FVector3d& Source, const FBeamTracerSettings& Settings, FSolverProgress* Progress);

	/**
	 * The subtree of root beam Context.EmittedRoots[Slot], breadth-first, into OutSubtree: node indices (Parent) are local
	 * to the subtree, the node budget (MaxBeamNodes) and the counters are the subtree's own. A pure function of the context
	 * and the slot, so it may run on any thread.
	 */
	void BuildRootSubtree(const FBeamBuildContext& Context, int32 Slot, FBeamTree& OutSubtree);

	/**
	 * The tree of all subtrees (indexed by slot) in breadth-first order: for every order, the subtrees in root order, each
	 * with its nodes of that order in their own order; parents are remapped. This is exactly the order of a sequential
	 * breadth-first build over all roots, so the merged tree does not depend on how the subtrees were scheduled. With more
	 * than MaxBeamNodes nodes the tree keeps the first MaxBeamNodes of that order and is marked truncated.
	 */
	TSharedPtr<FBeamTree> MergeSubtrees(const FBeamBuildContext& Context, TArray<FBeamTree>& Subtrees);

	/** One receiver of FBeamTracer::QueryReceivers (histogram, early reflections, containing nodes, direct sound, metrics). */
	void QueryReceiver(const FAcousticScene& Scene, const FBeamTree& Tree, const FSolverReceiver& Receiver, const FBeamTracerSettings& Settings, FReceiverResult& Out, TArray<FBeamContribution>& ContributionScratch, TArray<int32>& MediaScratch);
}
