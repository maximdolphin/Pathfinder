#include "LedgerPatchDisk.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "LedgerLog.h"
#include "LedgerPlanet.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"

#include <atomic>

namespace
{
	std::atomic<int32> GHits{0};
	std::atomic<int32> GMisses{0};
	std::atomic<int32> GWrites{0};

	/// FNV-1a over bytes, which is what a content address wants: fast, no
	/// dependencies, and no pretence at being cryptographic.
	void Mix(uint64& Hash, const void* Data, int32 Bytes)
	{
		const uint8* Byte = static_cast<const uint8*>(Data);
		for (int32 Index = 0; Index < Bytes; ++Index)
		{
			Hash ^= Byte[Index];
			Hash *= 1099511628211ull;
		}
	}

	template <typename T>
	void MixValue(uint64& Hash, const T& Value)
	{
		Mix(Hash, &Value, sizeof(T));
	}

	bool IsEnabled()
	{
		// On by default, off for a measurement that wants to see generation.
		static const bool bDisabled =
			FParse::Param(FCommandLine::Get(), TEXT("nopatchdisk"));
		return !bDisabled;
	}
}

namespace LedgerPatchDisk
{
	FString PathFor(uint64 Key)
	{
		// Two levels of prefix. A single directory with tens of thousands of
		// files in it is slow to open on every filesystem this will ever run
		// on, and the fix costs two bytes of the name.
		return Directory()
			/ FString::Printf(TEXT("%02x"), static_cast<uint32>(Key >> 56) & 0xFFu)
			/ FString::Printf(TEXT("%02x"), static_cast<uint32>(Key >> 48) & 0xFFu)
			/ FString::Printf(TEXT("%016llx.patch"), Key);
	}

	FString Directory()
	{
		return FPaths::ConvertRelativePathToFull(
			FPaths::ProjectSavedDir() / TEXT("PatchCache"));
	}

	uint64 ContentKey(const FLedgerPatchJob& Job)
	{
		uint64 Hash = 1469598103934665603ull;
		MixValue(Hash, FormatVersion);

		// Where and how big.
		MixValue(Hash, Job.Face);
		MixValue(Hash, Job.U);
		MixValue(Hash, Job.V);
		MixValue(Hash, Job.Extent);
		MixValue(Hash, Job.Side);

		// The stitch flags collapse vertices along an edge, so they change the
		// elevations themselves and not only the indices.
		MixValue(Hash, Job.bStitchLeft);
		MixValue(Hash, Job.bStitchRight);
		MixValue(Hash, Job.bStitchBottom);
		MixValue(Hash, Job.bStitchTop);

		// The planet.
		MixValue(Hash, Job.Params.Seed);
		MixValue(Hash, Job.Params.Radius);
		MixValue(Hash, Job.Params.MaxElevation);
		MixValue(Hash, Job.Params.SeaLevel);
		MixValue(Hash, Job.SeasonPhase);
		// The tilt, because T073 made it the amplitude of the seasonal term:
		// the same phase on a differently tilted planet is a different climate,
		// and therefore different vertex colours and different scatter.
		MixValue(Hash, Job.Params.AxialTiltRadians);
		// Air or no air, because it decides every moisture value on the body.
		MixValue(Hash, Job.Params.bHasAtmosphere ? 1u : 0u);

		// Ground somebody has changed. Every edit, because an edit anywhere can
		// reach this patch and a delta with one more edit in it is a different
		// planet.
		if (Job.Params.Delta.IsValid())
		{
			for (const FLedgerTerrainEdit& Edit : Job.Params.Delta->All())
			{
				MixValue(Hash, Edit.Centre);
				MixValue(Hash, Edit.RadiusMetres);
				MixValue(Hash, Edit.FalloffMetres);
				MixValue(Hash, Edit.TargetAltitudeMetres);
			}
		}

		// The biomes, because they decide the vertex colours and the scatter.
		if (Job.Biomes.IsValid())
		{
			for (const FLedgerBiome& Biome : *Job.Biomes)
			{
				MixValue(Hash, Biome.TemperatureC);
				MixValue(Hash, Biome.TemperatureToleranceC);
				MixValue(Hash, Biome.Moisture);
				MixValue(Hash, Biome.MoistureTolerance);
				MixValue(Hash, Biome.MaxSlopeDegrees);
				MixValue(Hash, Biome.ScatterDensity);
				Mix(Hash, *Biome.SurfaceSet, Biome.SurfaceSet.Len() * sizeof(TCHAR));
			}
		}

		// Whether this patch was asked for with collision, because that decides
		// whether it scattered anything.
		MixValue(Hash, Job.bWithCollision);
		return Hash;
	}

	bool Load(FLedgerPatchJob& Job)
	{
		if (!IsEnabled())
		{
			return false;
		}

		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *PathFor(ContentKey(Job)), FILEREAD_Silent))
		{
			++GMisses;
			return false;
		}

		FMemoryReader Reader(Bytes);
		int32 Elevations = 0;
		Reader << Elevations;
		const int32 Side = FMath::Max(3, Job.Side | 1);
		if (Elevations != Side * Side)
		{
			// A collision in the key, or a file from a build whose layout
			// changed without the version being bumped. Either way it is not
			// this patch, and generating is always correct.
			++GMisses;
			return false;
		}

		Job.Elevations.SetNumUninitialized(Elevations);
		Reader.Serialize(Job.Elevations.GetData(), Elevations * sizeof(double));

		int32 Colours = 0;
		Reader << Colours;
		Job.Colors.SetNumUninitialized(Colours);
		if (Colours > 0)
		{
			Reader.Serialize(Job.Colors.GetData(), Colours * sizeof(FColor));
		}

		Reader.Serialize(&Job.Palette, sizeof(FLedgerBiomePalette));

		int32 Scattered = 0;
		Reader << Scattered;
		Job.Scatter.SetNumUninitialized(Scattered);
		if (Scattered > 0)
		{
			Reader.Serialize(Job.Scatter.GetData(),
				Scattered * sizeof(FLedgerScatterInstance));
		}

		++GHits;
		return !Reader.IsError();
	}

	void Store(const FLedgerPatchJob& Job)
	{
		if (!IsEnabled() || Job.Elevations.Num() == 0)
		{
			return;
		}

		FBufferArchive Bytes;
		int32 Elevations = Job.Elevations.Num();
		Bytes << Elevations;
		Bytes.Serialize(const_cast<double*>(Job.Elevations.GetData()),
			Elevations * sizeof(double));

		int32 Colours = Job.Colors.Num();
		Bytes << Colours;
		if (Colours > 0)
		{
			Bytes.Serialize(const_cast<FColor*>(Job.Colors.GetData()),
				Colours * sizeof(FColor));
		}

		Bytes.Serialize(const_cast<FLedgerBiomePalette*>(&Job.Palette),
			sizeof(FLedgerBiomePalette));

		int32 Scattered = Job.Scatter.Num();
		Bytes << Scattered;
		if (Scattered > 0)
		{
			Bytes.Serialize(const_cast<FLedgerScatterInstance*>(Job.Scatter.GetData()),
				Scattered * sizeof(FLedgerScatterInstance));
		}

		// Silently, because a machine whose Saved directory is read-only should
		// be slow rather than broken, and because this runs on a worker thread
		// where a log line per patch would be its own problem.
		if (FFileHelper::SaveArrayToFile(Bytes, *PathFor(ContentKey(Job))))
		{
			// Every four thousand writes, which at ~50 KB an entry is about
			// 200 MB of growth between checks -- often enough that the cache
			// cannot run away, rare enough that walking a hundred thousand
			// files is not on the hot path. Store runs on a worker thread and
			// so does this.
			if ((++GWrites % 4000) == 0)
			{
				Evict();
			}
		}
	}

	void Evict()
	{
		if (!IsEnabled())
		{
			return;
		}

		// Every file, with its size and when it was last touched. On a full
		// cache this is a hundred thousand stat calls, which is why it runs on
		// a schedule rather than per write -- see the counter in Store.
		struct FEntry
		{
			FString Path;
			int64 Bytes = 0;
			FDateTime Touched;
		};

		TArray<FEntry> Entries;
		uint64 Total = 0;

		class FVisitor : public IPlatformFile::FDirectoryStatVisitor
		{
		public:
			FVisitor(TArray<FEntry>& InEntries, uint64& InTotal)
				: Entries(InEntries), Total(InTotal) {}

			virtual bool Visit(const TCHAR* Path, const FFileStatData& Stat) override
			{
				if (!Stat.bIsDirectory && Stat.FileSize > 0)
				{
					Entries.Add({ Path, Stat.FileSize,
						Stat.AccessTime > FDateTime::MinValue()
							? Stat.AccessTime : Stat.ModificationTime });
					Total += static_cast<uint64>(Stat.FileSize);
				}
				return true;
			}

			TArray<FEntry>& Entries;
			uint64& Total;
		};

		FVisitor Visitor(Entries, Total);
		FPlatformFileManager::Get().GetPlatformFile()
			.IterateDirectoryStatRecursively(*Directory(), Visitor);

		if (Total <= BudgetBytes)
		{
			return;
		}

		// Oldest first, and stop as soon as the total is under budget rather
		// than clearing to some low-water mark: this runs often enough that
		// trimming the excess is enough, and deleting more than necessary
		// throws away ground somebody is about to fly over again.
		Entries.Sort([](const FEntry& A, const FEntry& B)
			{ return A.Touched < B.Touched; });

		int32 Removed = 0;
		for (const FEntry& Entry : Entries)
		{
			if (Total <= BudgetBytes)
			{
				break;
			}
			if (IFileManager::Get().Delete(*Entry.Path, false, false, true))
			{
				Total -= static_cast<uint64>(Entry.Bytes);
				++Removed;
			}
		}

		UE_LOG(LogLedger, Log,
			TEXT("patch cache: evicted %d entries, now %.2f GB of a %.2f GB budget"),
			Removed, Total / 1073741824.0, BudgetBytes / 1073741824.0);
	}

	void Stats(int32& OutHits, int32& OutMisses, int32& OutWrites)
	{
		OutHits = GHits.load();
		OutMisses = GMisses.load();
		OutWrites = GWrites.load();
	}
}
