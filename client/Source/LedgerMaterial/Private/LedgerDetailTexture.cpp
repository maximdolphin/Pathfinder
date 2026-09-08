// Tiling detail textures, generated with a full mip chain.
//
// The mip chain is the entire point. `CreateTransient` produces a single
// level, and a single-level texture aliases the moment its features fall
// below a pixel — which on a planet is most of the screen. Every attempt to
// tune a Noise node instead traded one artefact for another.
//
// These are placeholders for authored surface sets (M02 T046-T048); what
// survives that change is the mip policy, not the noise.

#include "LedgerSurface.h"

#include "Engine/Texture2D.h"
#include "LedgerNoise.h"

#if WITH_EDITOR

namespace
{
	/// Tiling height field the detail textures are built from. Wraps exactly at
	/// `Size`, so the texture tiles without a visible seam.
	double TilingHeight(int32 X, int32 Y, int32 Size, uint32 Seed)
	{
		// Sample noise on a torus: two circles, so the field is periodic in both
		// axes by construction rather than by mirroring or blending.
		const double U = static_cast<double>(X) / static_cast<double>(Size);
		const double V = static_cast<double>(Y) / static_cast<double>(Size);
		const double TwoPi = 2.0 * PI;
		constexpr double R1 = 1.0;
		constexpr double R2 = 2.4;

		const FVector3d OnTorus(
			R1 * FMath::Cos(U * TwoPi),
			R1 * FMath::Sin(U * TwoPi),
			R2 * FMath::Cos(V * TwoPi));
		const FVector3d Second(
			0.0,
			0.0,
			R2 * FMath::Sin(V * TwoPi));

		const FVector3d Position = OnTorus + Second;
		return LedgerNoise::Fractal(Position * 1.7, Seed, 5) * 0.5 + 0.5;
	}

	/// Box-filters a level down by half. The whole point of the exercise: a mip
	/// chain is what lets the GPU stop showing detail before it aliases.
	void DownsampleBGRA(const TArray<uint8>& Source, int32 SourceSize, TArray<uint8>& Out)
	{
		const int32 TargetSize = FMath::Max(1, SourceSize / 2);
		Out.SetNumUninitialized(TargetSize * TargetSize * 4);

		for (int32 Y = 0; Y < TargetSize; ++Y)
		{
			for (int32 X = 0; X < TargetSize; ++X)
			{
				for (int32 Channel = 0; Channel < 4; ++Channel)
				{
					const int32 X0 = FMath::Min(X * 2, SourceSize - 1);
					const int32 X1 = FMath::Min(X * 2 + 1, SourceSize - 1);
					const int32 Y0 = FMath::Min(Y * 2, SourceSize - 1);
					const int32 Y1 = FMath::Min(Y * 2 + 1, SourceSize - 1);
					const int32 Sum =
						Source[(Y0 * SourceSize + X0) * 4 + Channel] +
						Source[(Y0 * SourceSize + X1) * 4 + Channel] +
						Source[(Y1 * SourceSize + X0) * 4 + Channel] +
						Source[(Y1 * SourceSize + X1) * 4 + Channel];
					Out[(Y * TargetSize + X) * 4 + Channel] = static_cast<uint8>(Sum / 4);
				}
			}
		}
	}

	/// Builds a `UTexture2D` from level-0 BGRA bytes, generating every mip.
	UTexture2D* BuildMippedTexture(UObject* Outer, int32 Size, TArray<uint8>& Level0, bool bSRGB, bool bNormalMap)
	{
		UTexture2D* Texture = NewObject<UTexture2D>(Outer, NAME_None, RF_Transient);
		if (Texture == nullptr)
		{
			return nullptr;
		}

		FTexturePlatformData* Data = new FTexturePlatformData();
		Data->SizeX = Size;
		Data->SizeY = Size;
		Data->PixelFormat = PF_B8G8R8A8;
		Texture->SetPlatformData(Data);

		TArray<uint8> Current = MoveTemp(Level0);
		int32 CurrentSize = Size;

		while (true)
		{
			FTexture2DMipMap* Mip = new FTexture2DMipMap();
			Mip->SizeX = static_cast<uint16>(CurrentSize);
			Mip->SizeY = static_cast<uint16>(CurrentSize);
			Mip->SizeZ = 1;
			Mip->BulkData.Lock(LOCK_READ_WRITE);
			void* Destination = Mip->BulkData.Realloc(Current.Num());
			FMemory::Memcpy(Destination, Current.GetData(), Current.Num());
			Mip->BulkData.Unlock();
			Data->Mips.Add(Mip);

			if (CurrentSize == 1)
			{
				break;
			}

			TArray<uint8> Next;
			DownsampleBGRA(Current, CurrentSize, Next);
			Current = MoveTemp(Next);
			CurrentSize = FMath::Max(1, CurrentSize / 2);
		}

		Texture->SRGB = bSRGB;
		Texture->Filter = TF_Trilinear;
		Texture->AddressX = TA_Wrap;
		Texture->AddressY = TA_Wrap;
		Texture->CompressionSettings = bNormalMap ? TC_Normalmap : TC_Default;
		Texture->NeverStream = true;
		Texture->UpdateResource();
		return Texture;
	}
}

namespace LedgerSurface
{
	UTexture2D* CreateDetailAlbedo(UObject* Outer, int32 Size, uint32 Seed)
	{
		TArray<uint8> Pixels;
		Pixels.SetNumUninitialized(Size * Size * 4);

		for (int32 Y = 0; Y < Size; ++Y)
		{
			for (int32 X = 0; X < Size; ++X)
			{
				const double Height = TilingHeight(X, Y, Size, Seed);
				// Low contrast on purpose: this multiplies the vertex colour, and
				// anything stronger reads as dirt rather than as surface.
				const double Shade = 0.80 + Height * 0.40;
				const uint8 Value = static_cast<uint8>(FMath::Clamp(Shade, 0.0, 1.0) * 255.0);
				const int32 Index = (Y * Size + X) * 4;
				Pixels[Index + 0] = Value; // B
				Pixels[Index + 1] = Value; // G
				Pixels[Index + 2] = Value; // R
				Pixels[Index + 3] = 255;
			}
		}

		return BuildMippedTexture(Outer, Size, Pixels, /*bSRGB*/ false, /*bNormalMap*/ false);
	}

	UTexture2D* CreateDetailNormal(UObject* Outer, int32 Size, uint32 Seed, double Strength)
	{
		// Height first, so the derivative wraps with the texture.
		TArray<double> Height;
		Height.SetNumUninitialized(Size * Size);
		for (int32 Y = 0; Y < Size; ++Y)
		{
			for (int32 X = 0; X < Size; ++X)
			{
				Height[Y * Size + X] = TilingHeight(X, Y, Size, Seed);
			}
		}

		TArray<uint8> Pixels;
		Pixels.SetNumUninitialized(Size * Size * 4);

		for (int32 Y = 0; Y < Size; ++Y)
		{
			for (int32 X = 0; X < Size; ++X)
			{
				// Wrapped central differences, so the normal map tiles too.
				const int32 Left = (X - 1 + Size) % Size;
				const int32 Right = (X + 1) % Size;
				const int32 Down = (Y - 1 + Size) % Size;
				const int32 Up = (Y + 1) % Size;

				const double Dx = (Height[Y * Size + Right] - Height[Y * Size + Left]) * Strength;
				const double Dy = (Height[Up * Size + X] - Height[Down * Size + X]) * Strength;

				FVector3d Normal = FVector3d(-Dx, -Dy, 1.0).GetSafeNormal();
				const int32 Index = (Y * Size + X) * 4;
				Pixels[Index + 0] = static_cast<uint8>((Normal.Z * 0.5 + 0.5) * 255.0); // B
				Pixels[Index + 1] = static_cast<uint8>((Normal.Y * 0.5 + 0.5) * 255.0); // G
				Pixels[Index + 2] = static_cast<uint8>((Normal.X * 0.5 + 0.5) * 255.0); // R
				Pixels[Index + 3] = 255;
			}
		}

		return BuildMippedTexture(Outer, Size, Pixels, /*bSRGB*/ false, /*bNormalMap*/ true);
	}
}

#endif
