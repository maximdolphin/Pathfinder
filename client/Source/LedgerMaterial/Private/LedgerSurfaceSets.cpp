// Reading client/Config/surfaces.json, and loading the assets it names.

#include "LedgerSurfaceSets.h"

#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#include "LedgerLog.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectGlobals.h"

namespace LedgerSurface
{
	namespace
	{
		/// The manifest, parsed once. It is small, it does not change while the
		/// editor is open, and every surface asks for it.
		const TSharedPtr<FJsonObject>& Manifest()
		{
			static TSharedPtr<FJsonObject> Parsed;
			static bool bTried = false;
			if (bTried)
			{
				return Parsed;
			}
			bTried = true;

			// Outside Content on purpose: Content holds the imported .uasset,
			// and the manifest describes where those came from. In Config
			// rather than beside the source images because the packaged game
			// reads it too, and Config is what stages.
			const FString Path = FPaths::ConvertRelativePathToFull(
				FPaths::ProjectConfigDir() / TEXT("surfaces.json"));

			FString Body;
			if (!FFileHelper::LoadFileToString(Body, *Path))
			{
				UE_LOG(LogLedger, Error, TEXT("no surface manifest at %s"), *Path);
				return Parsed;
			}

			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
			if (!FJsonSerializer::Deserialize(Reader, Parsed))
			{
				UE_LOG(LogLedger, Error, TEXT("surface manifest at %s is not valid JSON"), *Path);
				Parsed.Reset();
			}
			return Parsed;
		}

		const TSharedPtr<FJsonObject>* FindSet(const FString& Name)
		{
			const TSharedPtr<FJsonObject>& Root = Manifest();
			if (!Root.IsValid())
			{
				return nullptr;
			}

			const TArray<TSharedPtr<FJsonValue>>* Sets = nullptr;
			if (!Root->TryGetArrayField(TEXT("sets"), Sets))
			{
				return nullptr;
			}

			for (const TSharedPtr<FJsonValue>& Value : *Sets)
			{
				const TSharedPtr<FJsonObject>& Entry = Value->AsObject();
				if (Entry.IsValid() && Entry->GetStringField(TEXT("name")) == Name)
				{
					// Returned by pointer into the cached document, which
					// outlives every caller.
					static TSharedPtr<FJsonObject> Held;
					Held = Entry;
					return &Held;
				}
			}
			return nullptr;
		}

		/// The sRGB transfer curve, inverted. FLinearColor::FromSRGBColor takes
		/// an 8-bit FColor and would quantise a measured average back to a
		/// byte, which is the one thing this number exists to avoid.
		float SRGBToLinear(float Value)
		{
			return Value <= 0.04045f
				? Value / 12.92f
				: FMath::Pow((Value + 0.055f) / 1.055f, 2.4f);
		}

		UTexture2D* Load(const FString& SetName, const TCHAR* Asset)
		{
			// The importer names the package after the file, so the path is a
			// consequence of the manifest rather than a second thing to keep in
			// step with it.
			const FString Path = FString::Printf(
				TEXT("/Game/Surfaces/%s/%s.%s"), *SetName, Asset, Asset);
			UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *Path);
			if (Texture == nullptr)
			{
				UE_LOG(LogLedger, Error,
					TEXT("surface %s: no texture at %s. Run tools/make_import_settings.py "
					     "and the ImportAssets commandlet."), *SetName, *Path);
			}
			return Texture;
		}
	}

	FSurfaceSet LoadSurfaceSet(const FString& Name)
	{
		FSurfaceSet Set;
		Set.Name = Name;

		const TSharedPtr<FJsonObject>* Entry = FindSet(Name);
		if (Entry == nullptr || !Entry->IsValid())
		{
			UE_LOG(LogLedger, Error, TEXT("surface %s is not in the manifest"), *Name);
			return Set;
		}

		double Metres = 0.0;
		if ((*Entry)->TryGetNumberField(TEXT("tiling_metres"), Metres) && Metres > 0.0)
		{
			Set.TilingMetres = Metres;
		}

		const TArray<TSharedPtr<FJsonValue>>* Mean = nullptr;
		if ((*Entry)->TryGetArrayField(TEXT("mean_albedo"), Mean) && Mean->Num() >= 3)
		{
			// Converted to linear, because that is the space the shader divides
			// in. The mean is measured over the stored PNG, which is sRGB; the
			// texture sampler hands the shader linear values. Dividing a linear
			// albedo by an sRGB mean is not a small error: a mid-grey scan
			// reads 0.51 in sRGB and 0.22 in linear, so the quotient averages
			// 0.43 instead of 1, and the terrain does it twice — once for
			// detail and once for the macro breakup. The ground came out at
			// about one and a half percent reflectance, which looks exactly
			// like the sun has gone out.
			Set.MeanAlbedo = FLinearColor(
				SRGBToLinear(static_cast<float>((*Mean)[0]->AsNumber())),
				SRGBToLinear(static_cast<float>((*Mean)[1]->AsNumber())),
				SRGBToLinear(static_cast<float>((*Mean)[2]->AsNumber())));
		}

		Set.Albedo = Load(Name, TEXT("albedo"));
		Set.Normal = Load(Name, TEXT("normal"));
		Set.Packed = Load(Name, TEXT("packed_ao_rough_height"));
		return Set;
	}
}
