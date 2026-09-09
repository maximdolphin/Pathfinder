#include "LedgerTerrainDelta.h"

#include "Dom/JsonObject.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	/// The bucket a direction falls in.
	///
	/// **`DirectionToFace`, deliberately, and not the real inverse.** A bucket
	/// only has to be *consistent* between the insert and the lookup; it does
	/// not have to be a place. `SphereToFace` is the honest answer and it is a
	/// forty-step fixed point, and putting it here doubled the cost of every
	/// height sample on the planet -- 89.5 ms to 192.4 for 200,000 samples --
	/// which is the exact opposite of "the delta costs nothing where nothing
	/// was modified". The same mistake had been made in the stitch path an hour
	/// earlier (docs/comparisons/projection/).
	///
	/// The warp distorts the bucket's shape by up to 0.066 of a face. That does
	/// not matter: it is smooth, so points near each other stay in buckets near
	/// each other, and the insert already covers an edit's extent by probing
	/// nine points around it rather than by trusting the lattice's geometry.
	int32 BucketOf(const FVector3d& UnitSphere)
	{
		ELedgerCubeFace Face = ELedgerCubeFace::PositiveX;
		double U = 0.0;
		double V = 0.0;
		LedgerTerrain::DirectionToFace(UnitSphere, Face, U, V);

		const int32 CellU = FMath::Clamp(
			FMath::FloorToInt32(U * FLedgerTerrainDelta::BucketResolution),
			0, FLedgerTerrainDelta::BucketResolution - 1);
		const int32 CellV = FMath::Clamp(
			FMath::FloorToInt32(V * FLedgerTerrainDelta::BucketResolution),
			0, FLedgerTerrainDelta::BucketResolution - 1);
		return static_cast<int32>(Face) * FLedgerTerrainDelta::BucketResolution
			* FLedgerTerrainDelta::BucketResolution
			+ CellV * FLedgerTerrainDelta::BucketResolution + CellU;
	}
}

void FLedgerTerrainDelta::Index()
{
	ByBucket.Reset();
	for (int32 Which = 0; Which < Edits.Num(); ++Which)
	{
		const FLedgerTerrainEdit& Edit = Edits[Which];

		// Every bucket the edit can reach, found by walking a small grid across
		// its own extent rather than by inverting the lattice. An edit is at
		// most a few hundred metres and a bucket is 156 km, so this almost
		// always adds one entry -- and when it does not, it adds the two or
		// four the edit genuinely straddles.
		const double Reach = Edit.RadiusMetres + Edit.FalloffMetres;
		FVector3d East = FVector3d::CrossProduct(FVector3d(0.0, 0.0, 1.0), Edit.Centre);
		if (East.IsNearlyZero())
		{
			East = FVector3d::CrossProduct(FVector3d(1.0, 0.0, 0.0), Edit.Centre);
		}
		East.Normalize();
		const FVector3d North = FVector3d::CrossProduct(Edit.Centre, East).GetSafeNormal();

		// Earth-sized radius in metres, which is what the arcs below are
		// against. The delta does not know the planet's radius and does not
		// need to: it only has to be generous.
		constexpr double GenerousRadiusMetres = 6371000.0;
		for (int32 Corner = -1; Corner <= 1; ++Corner)
		{
			for (int32 Other = -1; Other <= 1; ++Other)
			{
				const FVector3d Point = (Edit.Centre
					+ East * (Corner * Reach / GenerousRadiusMetres)
					+ North * (Other * Reach / GenerousRadiusMetres)).GetSafeNormal();
				ByBucket.FindOrAdd(BucketOf(Point)).AddUnique(Which);
			}
		}
	}
}

TSharedRef<const FLedgerTerrainDelta> FLedgerTerrainDelta::With(
	const FLedgerTerrainEdit& Edit) const
{
	TSharedRef<FLedgerTerrainDelta> Next = MakeShared<FLedgerTerrainDelta>();
	Next->Edits = Edits;
	Next->Edits.Add(Edit);
	Next->Index();
	return Next;
}

double FLedgerTerrainDelta::Apply(
	const FVector3d& UnitSphere, double BaseAltitudeMetres) const
{
	if (ByBucket.Num() == 0)
	{
		return BaseAltitudeMetres;
	}

	const TArray<int32>* Bucket = ByBucket.Find(BucketOf(UnitSphere));
	if (Bucket == nullptr)
	{
		return BaseAltitudeMetres;
	}

	double Altitude = BaseAltitudeMetres;
	for (const int32 Index : *Bucket)
	{
		const FLedgerTerrainEdit& Edit = Edits[Index];

		// A dot product first, and an arccosine only for the handful of samples
		// that are actually on the pad. An arccosine per edit per height sample
		// is not free and almost every sample is outside every edit.
		constexpr double GenerousRadiusMetres = 6371000.0;
		const double Reach = Edit.RadiusMetres + Edit.FalloffMetres;
		const double Cosine = FVector3d::DotProduct(UnitSphere, Edit.Centre);
		if (Cosine < FMath::Cos(Reach / GenerousRadiusMetres))
		{
			continue;
		}

		const double Metres =
			FMath::Acos(FMath::Clamp(Cosine, -1.0, 1.0)) * GenerousRadiusMetres;

		// Smoothstep out from the edge of the flat part, so the pad meets the
		// hillside instead of standing on a wall.
		const double Blend = Edit.FalloffMetres > 0.0
			? 1.0 - FMath::SmoothStep(Edit.RadiusMetres,
				Edit.RadiusMetres + Edit.FalloffMetres, Metres)
			: (Metres < Edit.RadiusMetres ? 1.0 : 0.0);

		Altitude = FMath::Lerp(Altitude, Edit.TargetAltitudeMetres, Blend);
	}
	return Altitude;
}

FString FLedgerTerrainDelta::ToJson() const
{
	FString Out;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	Writer->WriteObjectStart();
	Writer->WriteArrayStart(TEXT("edits"));
	for (const FLedgerTerrainEdit& Edit : Edits)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("x"), Edit.Centre.X);
		Writer->WriteValue(TEXT("y"), Edit.Centre.Y);
		Writer->WriteValue(TEXT("z"), Edit.Centre.Z);
		Writer->WriteValue(TEXT("radiusMetres"), Edit.RadiusMetres);
		Writer->WriteValue(TEXT("falloffMetres"), Edit.FalloffMetres);
		Writer->WriteValue(TEXT("targetAltitudeMetres"), Edit.TargetAltitudeMetres);
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();
	Writer->WriteObjectEnd();
	Writer->Close();
	return Out;
}

TSharedRef<const FLedgerTerrainDelta> FLedgerTerrainDelta::FromJson(const FString& Json)
{
	TSharedRef<FLedgerTerrainDelta> Delta = MakeShared<FLedgerTerrainDelta>();

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		// An unreadable delta is an empty one. The ground comes back as
		// generated, which is wrong but is ground; refusing to load would be a
		// planet with no terrain at all.
		return Delta;
	}

	const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
	if (!Root->TryGetArrayField(TEXT("edits"), Array))
	{
		return Delta;
	}

	for (const TSharedPtr<FJsonValue>& Value : *Array)
	{
		const TSharedPtr<FJsonObject>& Object = Value->AsObject();
		if (!Object.IsValid())
		{
			continue;
		}
		FLedgerTerrainEdit Edit;
		Edit.Centre = FVector3d(
			Object->GetNumberField(TEXT("x")),
			Object->GetNumberField(TEXT("y")),
			Object->GetNumberField(TEXT("z"))).GetSafeNormal();
		Edit.RadiusMetres = Object->GetNumberField(TEXT("radiusMetres"));
		Edit.FalloffMetres = Object->GetNumberField(TEXT("falloffMetres"));
		Edit.TargetAltitudeMetres = Object->GetNumberField(TEXT("targetAltitudeMetres"));
		Delta->Edits.Add(Edit);
	}

	Delta->Index();
	return Delta;
}

FString FLedgerTerrainDelta::DefaultPath()
{
	return FPaths::ConvertRelativePathToFull(
		FPaths::ProjectSavedDir() / TEXT("TerrainDelta.json"));
}
