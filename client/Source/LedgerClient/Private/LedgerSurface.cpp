#include "LedgerSurface.h"

#include "Engine/Texture2D.h"
#include "LedgerSimSubsystem.h"
#include "LedgerTerrainMath.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"

#if WITH_EDITOR
#include "MaterialDomain.h"
#include "Materials/MaterialExpressionAbs.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionCosine.h"
#include "Materials/MaterialExpressionDistance.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionDotProduct.h"
#include "Materials/MaterialExpressionFrac.h"
#include "Materials/MaterialExpressionFresnel.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionNormalize.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionPixelDepth.h"
#include "Materials/MaterialExpressionPower.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionSine.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#endif

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
		return LedgerTerrain::FractalNoise(Position * 1.7, Seed, 5) * 0.5 + 0.5;
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

#if WITH_EDITOR
	namespace
	{
		/// Small builder so the graph below reads as a graph rather than as three
		/// hundred lines of `NewObject` and field assignment.
		struct FGraph
		{
			UMaterial* Material = nullptr;

			template <typename T>
			T* Make()
			{
				T* Expression = NewObject<T>(Material);
				Material->GetExpressionCollection().AddExpression(Expression);
				return Expression;
			}

			UMaterialExpression* Constant(float Value)
			{
				UMaterialExpressionConstant* Node = Make<UMaterialExpressionConstant>();
				Node->R = Value;
				return Node;
			}

			UMaterialExpression* Mask(UMaterialExpression* Input, bool R, bool G, bool B)
			{
				UMaterialExpressionComponentMask* Node = Make<UMaterialExpressionComponentMask>();
				Node->Input.Expression = Input;
				Node->R = R;
				Node->G = G;
				Node->B = B;
				Node->A = false;
				return Node;
			}

			UMaterialExpression* Multiply(UMaterialExpression* A, UMaterialExpression* B)
			{
				UMaterialExpressionMultiply* Node = Make<UMaterialExpressionMultiply>();
				Node->A.Expression = A;
				Node->B.Expression = B;
				return Node;
			}

			UMaterialExpression* Add(UMaterialExpression* A, UMaterialExpression* B)
			{
				UMaterialExpressionAdd* Node = Make<UMaterialExpressionAdd>();
				Node->A.Expression = A;
				Node->B.Expression = B;
				return Node;
			}

			UMaterialExpression* Divide(UMaterialExpression* A, UMaterialExpression* B)
			{
				UMaterialExpressionDivide* Node = Make<UMaterialExpressionDivide>();
				Node->A.Expression = A;
				Node->B.Expression = B;
				return Node;
			}

			UMaterialExpression* Subtract(UMaterialExpression* A, UMaterialExpression* B)
			{
				UMaterialExpressionSubtract* Node = Make<UMaterialExpressionSubtract>();
				Node->A.Expression = A;
				Node->B.Expression = B;
				return Node;
			}

			UMaterialExpression* Scale(UMaterialExpression* A, float Factor)
			{
				return Multiply(A, Constant(Factor));
			}

			UMaterialExpression* Saturate(UMaterialExpression* Input)
			{
				UMaterialExpressionSaturate* Node = Make<UMaterialExpressionSaturate>();
				Node->Input.Expression = Input;
				return Node;
			}

			UMaterialExpression* OneMinus(UMaterialExpression* Input)
			{
				UMaterialExpressionOneMinus* Node = Make<UMaterialExpressionOneMinus>();
				Node->Input.Expression = Input;
				return Node;
			}

			UMaterialExpression* Lerp(UMaterialExpression* A, UMaterialExpression* B, UMaterialExpression* Alpha)
			{
				UMaterialExpressionLinearInterpolate* Node = Make<UMaterialExpressionLinearInterpolate>();
				Node->A.Expression = A;
				Node->B.Expression = B;
				Node->Alpha.Expression = Alpha;
				return Node;
			}

			UMaterialExpression* Sample(UTexture2D* Texture, UMaterialExpression* Coordinates, EMaterialSamplerType Type)
			{
				UMaterialExpressionTextureSample* Node = Make<UMaterialExpressionTextureSample>();
				Node->Texture = Texture;
				Node->SamplerType = Type;
				Node->Coordinates.Expression = Coordinates;
				return Node;
			}

			/// World-aligned projection along all three axes, blended by the
			/// surface normal. No UVs, no seams, no pinching at the poles.
			UMaterialExpression* Triplanar(
				UTexture2D* Texture,
				UMaterialExpression* ScaledPosition,
				UMaterialExpression* WeightX,
				UMaterialExpression* WeightY,
				UMaterialExpression* WeightZ,
				EMaterialSamplerType Type)
			{
				UMaterialExpression* PlaneYZ = Mask(ScaledPosition, false, true, true);
				UMaterialExpression* PlaneXZ = Mask(ScaledPosition, true, false, true);
				UMaterialExpression* PlaneXY = Mask(ScaledPosition, true, true, false);

				UMaterialExpression* SampleX = Multiply(Sample(Texture, PlaneYZ, Type), WeightX);
				UMaterialExpression* SampleY = Multiply(Sample(Texture, PlaneXZ, Type), WeightY);
				UMaterialExpression* SampleZ = Multiply(Sample(Texture, PlaneXY, Type), WeightZ);

				return Add(Add(SampleX, SampleY), SampleZ);
			}
		};
	}

	UMaterialInterface* CreateTerrainMaterial(UObject* Outer, uint32 Seed)
	{
		UMaterial* Material = NewObject<UMaterial>(Outer, NAME_None, RF_Transient);
		if (Material == nullptr)
		{
			return nullptr;
		}

		UTexture2D* Albedo = CreateDetailAlbedo(Material, 256, Seed ^ 0xD1D1u);
		UTexture2D* NormalMap = CreateDetailNormal(Material, 256, Seed ^ 0xD1D1u, 3.5);
		if (Albedo == nullptr || NormalMap == nullptr)
		{
			return nullptr;
		}

		Material->MaterialDomain = MD_Surface;
		Material->SetShadingModel(MSM_DefaultLit);
		Material->TwoSided = false;

		FGraph Graph;
		Graph.Material = Material;

		// Camera-relative, not absolute: an absolute position on a 6.37e8 cm
		// planet spends all of its float precision on the exponent, and the
		// projection degenerates into banding. Camera-relative keeps full
		// precision exactly where the detail is visible.
		UMaterialExpressionWorldPosition* WorldPosition = Graph.Make<UMaterialExpressionWorldPosition>();
		WorldPosition->WorldPositionShaderOffset = WPT_CameraRelativeNoOffsets;

		// Blend weights: |N| normalised so the three projections sum to one.
		UMaterialExpressionVertexNormalWS* Normal = Graph.Make<UMaterialExpressionVertexNormalWS>();
		UMaterialExpressionAbs* AbsoluteNormal = Graph.Make<UMaterialExpressionAbs>();
		AbsoluteNormal->Input.Expression = Normal;

		UMaterialExpressionConstant3Vector* Ones = Graph.Make<UMaterialExpressionConstant3Vector>();
		Ones->Constant = FLinearColor(1.0f, 1.0f, 1.0f);
		UMaterialExpressionDotProduct* WeightSum = Graph.Make<UMaterialExpressionDotProduct>();
		WeightSum->A.Expression = AbsoluteNormal;
		WeightSum->B.Expression = Ones;

		UMaterialExpression* Weights = Graph.Divide(AbsoluteNormal, WeightSum);
		UMaterialExpression* WeightX = Graph.Mask(Weights, true, false, false);
		UMaterialExpression* WeightY = Graph.Mask(Weights, false, true, false);
		UMaterialExpression* WeightZ = Graph.Mask(Weights, false, false, true);

		// Two scales. The near one carries the grain you see standing on it; the
		// macro one breaks up the tiling that a single scale makes obvious from
		// the air. 400 cm and 90 m.
		UMaterialExpression* NearPosition = Graph.Multiply(WorldPosition, Graph.Constant(1.0f / 400.0f));
		UMaterialExpression* MacroPosition = Graph.Multiply(WorldPosition, Graph.Constant(1.0f / 9000.0f));

		UMaterialExpression* NearDetail = Graph.Triplanar(
			Albedo, NearPosition, WeightX, WeightY, WeightZ, SAMPLERTYPE_LinearColor);
		UMaterialExpression* MacroDetail = Graph.Triplanar(
			Albedo, MacroPosition, WeightX, WeightY, WeightZ, SAMPLERTYPE_LinearColor);

		// Distance fade. Mips stop the near detail from aliasing, but by the time
		// a 4 m pattern is a few pixels across it is only noise on the histogram
		// — fading it to neutral before then is both cheaper and cleaner.
		UMaterialExpressionPixelDepth* Depth = Graph.Make<UMaterialExpressionPixelDepth>();
		UMaterialExpressionSaturate* Fade = Graph.Make<UMaterialExpressionSaturate>();
		Fade->Input.Expression = Graph.Divide(Depth, Graph.Constant(60000.0f)); // 600 m

		UMaterialExpression* NearFaded = Graph.Lerp(NearDetail, Graph.Constant(1.0f), Fade);

		UMaterialExpressionVertexColor* VertexColour = Graph.Make<UMaterialExpressionVertexColor>();
		UMaterialExpression* BaseColour = Graph.Multiply(Graph.Multiply(VertexColour, NearFaded), MacroDetail);

		// Normal, faded to flat over the same distance. A normal map that
		// survives past its mip range is the other half of the shimmer.
		UMaterialExpression* TriplanarNormal = Graph.Triplanar(
			NormalMap, NearPosition, WeightX, WeightY, WeightZ, SAMPLERTYPE_Normal);
		UMaterialExpressionConstant3Vector* FlatNormal = Graph.Make<UMaterialExpressionConstant3Vector>();
		FlatNormal->Constant = FLinearColor(0.0f, 0.0f, 1.0f);
		UMaterialExpression* FadedNormal = Graph.Lerp(TriplanarNormal, FlatNormal, Fade);

		UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
		if (EditorData == nullptr)
		{
			return nullptr;
		}
		EditorData->BaseColor.Expression = BaseColour;
		EditorData->Normal.Expression = FadedNormal;
		EditorData->Roughness.Expression = Graph.Constant(0.93f);
		EditorData->Specular.Expression = Graph.Constant(0.05f);

		Material->PostEditChange();
		return Material;
	}

	UMaterialInterface* CreateWaterMaterial(UObject* Outer)
	{
		UMaterial* Material = NewObject<UMaterial>(Outer, NAME_None, RF_Transient);
		if (Material == nullptr)
		{
			return nullptr;
		}

		Material->MaterialDomain = MD_Surface;
		Material->SetShadingModel(MSM_DefaultLit);
		// The wave normal is derived from world-space gradients. There is no
		// tangent basis anywhere in that derivation and none should be applied
		// to the result.
		Material->bTangentSpaceNormal = false;

		FGraph Graph;
		Graph.Material = Material;

		// Depth comes in on vertex alpha: shallow water over a sandbar is a
		// different colour from open ocean, and that gradient at the shoreline is
		// most of what tells you it is a surface with something under it.
		UMaterialExpressionVertexColor* VertexColour = Graph.Make<UMaterialExpressionVertexColor>();

		UMaterialExpressionConstant3Vector* Shallow = Graph.Make<UMaterialExpressionConstant3Vector>();
		Shallow->Constant = FLinearColor(0.075f, 0.220f, 0.245f);
		UMaterialExpressionConstant3Vector* Deep = Graph.Make<UMaterialExpressionConstant3Vector>();
		Deep->Constant = FLinearColor(0.012f, 0.048f, 0.098f);

		// Depth arrives on vertex alpha, and alpha is a **separate output pin**
		// on the vertex-colour node — pin 0 is float3 RGB with no alpha in it.
		//
		// Masking pin 0 for alpha is what broke this material: it compiled to
		// "not enough components in float3 for component mask 0001", and UE's
		// response to a material that will not compile is one warning line and a
		// silent swap to the default. The sea therefore rendered as grey
		// WorldGridMaterial, and every subsequent change to the water shader
		// appeared to do nothing whatsoever. Worth knowing: a material that does
		// not react to any edit is usually not being used.
		constexpr int32 VertexAlphaPin = 4;

		UMaterialExpressionLinearInterpolate* Body = Graph.Make<UMaterialExpressionLinearInterpolate>();
		Body->A.Expression = Shallow;
		Body->B.Expression = Deep;
		Body->Alpha.Expression = VertexColour;
		Body->Alpha.OutputIndex = VertexAlphaPin;


		// Fresnel: water is nearly a mirror at grazing angles and nearly clear
		// looking straight down. Without it the sea is the same colour to the
		// horizon and reads as painted.
		UMaterialExpressionFresnel* Fresnel = Graph.Make<UMaterialExpressionFresnel>();
		Fresnel->Exponent = 4.0f;
		Fresnel->BaseReflectFraction = 0.02f;

		// At a grazing angle water shows the sky, which near the horizon is bright.
		// The first pass used a dark grey here and the sea came back nearly black
		// from any low viewpoint, which is every viewpoint that matters.
		UMaterialExpressionConstant3Vector* Grazing = Graph.Make<UMaterialExpressionConstant3Vector>();
		Grazing->Constant = FLinearColor(0.42f, 0.52f, 0.62f);
		UMaterialExpression* BaseColour = Graph.Lerp(Body, Grazing, Fresnel);

		// ------------------------------------------------------------- waves
		//
		// Two crossed plane-wave trains, each running along a world axis. Two is
		// enough to stop the sea reading as a single corrugation and is well
		// short of a spectrum; a real ocean wants a handful more, in a texture.
		//
		// Phase is `Frac(x / wavelength)`, not `x / wavelength`. The planet is
		// 6371 km across, so a world coordinate out here is around 6.4e8 and a
		// float has no fractional part left at that magnitude — the sine would
		// return staircase garbage. Frac on an absolute world position is the
		// one operation large-world coordinates keep exact, which is why it
		// exists. It is also why this cannot use the camera-relative position
		// the terrain material uses: relative coordinates move with the camera,
		// and the wave field would swim along with it.
		UMaterialExpressionWorldPosition* WorldPos = Graph.Make<UMaterialExpressionWorldPosition>();
		WorldPos->WorldPositionShaderOffset = WPT_Default;

		// Deep-water phase speed is sqrt(g L / 2pi), so the long swell outruns
		// the chop. Getting this wrong is immediately legible: waves of
		// different lengths travelling at the same speed hold their relative
		// positions, and the sea reads as one scrolling texture, which is what
		// it would then be.
		constexpr float Gravity = 981.0f;              // cm/s^2
		constexpr float LongWavelength = 7000.0f;      // 70 m
		constexpr float LongAmplitude = 55.0f;
		constexpr float ShortWavelength = 3100.0f;     // 31 m
		constexpr float ShortAmplitude = 22.0f;

		const float LongPeriod = LongWavelength / FMath::Sqrt(Gravity * LongWavelength / (2.0f * PI));
		const float ShortPeriod = ShortWavelength / FMath::Sqrt(Gravity * ShortWavelength / (2.0f * PI));

		UMaterialExpressionTime* Time = Graph.Make<UMaterialExpressionTime>();

		// Phase in cycles: the Sine and Cosine nodes have a period of 1, so 1.0
		// here is one whole wave.
		auto MakePhase = [&Graph, WorldPos, Time](bool bAlongX, float Wavelength, float Period) -> UMaterialExpression*
		{
			UMaterialExpressionFrac* Wrapped = Graph.Make<UMaterialExpressionFrac>();
			Wrapped->Input.Expression = Graph.Divide(
				Graph.Mask(WorldPos, bAlongX, !bAlongX, false), Graph.Constant(Wavelength));
			return Graph.Subtract(Wrapped, Graph.Scale(Time, 1.0f / Period));
		};

		UMaterialExpression* LongPhase = MakePhase(true, LongWavelength, LongPeriod);
		UMaterialExpression* ShortPhase = MakePhase(false, ShortWavelength, ShortPeriod);

		auto Sine = [&Graph](UMaterialExpression* Phase) -> UMaterialExpression*
		{
			UMaterialExpressionSine* Node = Graph.Make<UMaterialExpressionSine>();
			Node->Input.Expression = Phase;
			return Node;
		};
		auto Cosine = [&Graph](UMaterialExpression* Phase) -> UMaterialExpression*
		{
			UMaterialExpressionCosine* Node = Graph.Make<UMaterialExpressionCosine>();
			Node->Input.Expression = Phase;
			return Node;
		};

		UMaterialExpression* LongSine = Sine(LongPhase);
		UMaterialExpression* ShortSine = Sine(ShortPhase);

		// Vertex red carries proximity to the shoreline: 1 at the waterline,
		// 0 once the sea floor is 3 m down. Amplitude runs the other way, so
		// the swell flattens into the beach instead of cutting through it.
		UMaterialExpression* Shore = Graph.Mask(VertexColour, true, false, false);
		UMaterialExpression* OpenWater = Graph.OneMinus(Shore);

		// Displacement has to stop before the mesh stops resolving it: a couple
		// of kilometres out a 31 m wave is narrower than the LOD triangles, and
		// sampling it there turns the sea into noise. Fading the geometry and
		// the normal on the same curve keeps the two agreeing all the way out.
		UMaterialExpressionDistance* ToCamera = Graph.Make<UMaterialExpressionDistance>();
		ToCamera->A.Expression = Graph.Make<UMaterialExpressionCameraPositionWS>();
		ToCamera->B.Expression = WorldPos;
		UMaterialExpression* DistanceFade = Graph.OneMinus(Graph.Saturate(
			Graph.Divide(
				Graph.Subtract(ToCamera, Graph.Constant(40000.0f)),
				Graph.Constant(260000.0f))));

		UMaterialExpression* WaveFade = Graph.Multiply(OpenWater, DistanceFade);

		UMaterialExpressionVertexNormalWS* Up = Graph.Make<UMaterialExpressionVertexNormalWS>();

		UMaterialExpression* Height = Graph.Multiply(
			Graph.Add(
				Graph.Scale(LongSine, LongAmplitude),
				Graph.Scale(ShortSine, ShortAmplitude)),
			WaveFade);

		// The normal of a height field displaced along N is N - grad(h), with
		// the radial part of the gradient removed. Each train varies along one
		// world axis only, so the gradient is just the two cosines laid into x
		// and y.
		//
		// The slopes are exaggerated: A*2pi/L comes to about 0.05, which is a
		// correct and completely invisible amount of tilt. Lighting is what
		// sells a wave; the geometry only gives it a silhouette.
		constexpr float NormalStrength = 1.8f;
		UMaterialExpression* SlopeX = Graph.Scale(Cosine(LongPhase),
			NormalStrength * LongAmplitude * 2.0f * PI / LongWavelength);
		UMaterialExpression* SlopeY = Graph.Scale(Cosine(ShortPhase),
			NormalStrength * ShortAmplitude * 2.0f * PI / ShortWavelength);

		UMaterialExpressionAppendVector* SlopeXY = Graph.Make<UMaterialExpressionAppendVector>();
		SlopeXY->A.Expression = SlopeX;
		SlopeXY->B.Expression = SlopeY;
		UMaterialExpressionAppendVector* Gradient = Graph.Make<UMaterialExpressionAppendVector>();
		Gradient->A.Expression = SlopeXY;
		Gradient->B.Expression = Graph.Constant(0.0f);

		UMaterialExpressionDotProduct* Radial = Graph.Make<UMaterialExpressionDotProduct>();
		Radial->A.Expression = Gradient;
		Radial->B.Expression = Up;
		UMaterialExpression* Tangential = Graph.Subtract(Gradient, Graph.Multiply(Up, Radial));

		UMaterialExpressionNormalize* WaveNormal = Graph.Make<UMaterialExpressionNormalize>();
		WaveNormal->VectorInput.Expression =
			Graph.Subtract(Up, Graph.Multiply(Tangential, WaveFade));

		// -------------------------------------------------------------- foam
		//
		// A band hugging the waterline, surging with the long wave so the
		// shoreline advances and retreats instead of sitting still. Keyed off
		// the same shore channel, on a curve rather than a threshold: a hard cut
		// would have to know whether vertex colour arrives sRGB-encoded, and a
		// curve does not care.
		UMaterialExpressionPower* FoamBand = Graph.Make<UMaterialExpressionPower>();
		FoamBand->Base.Expression = Shore;
		FoamBand->Exponent.Expression = Graph.Constant(6.0f);

		UMaterialExpression* Surge = Graph.Saturate(
			Graph.Add(Graph.Constant(0.5f), Graph.Scale(LongSine, 0.85f)));
		UMaterialExpression* Foam = Graph.Saturate(
			Graph.Scale(Graph.Multiply(FoamBand, Surge), 1.3f));

		UMaterialExpressionConstant3Vector* FoamColour = Graph.Make<UMaterialExpressionConstant3Vector>();
		FoamColour->Constant = FLinearColor(0.82f, 0.87f, 0.90f);
		BaseColour = Graph.Lerp(BaseColour, FoamColour, Foam);

		// Roughness matters more than it looks.
		//
		// At 0.012 the surface is a mirror, and a mirror renders black unless
		// something is in front of it: screen-space reflection can only return
		// what is already on screen, and looking out to sea that is mostly sky
		// it cannot sample. Around 0.09 the sea picks up the sky light's ambient
		// specular instead, which is what gives it colour at all, and still sits
		// under the SSR roughness cutoff so real reflections work where they can.
		UMaterialExpressionLinearInterpolate* Roughness = Graph.Make<UMaterialExpressionLinearInterpolate>();
		Roughness->A.Expression = Graph.Constant(0.11f);
		Roughness->B.Expression = Graph.Constant(0.07f);
		Roughness->Alpha.Expression = VertexColour;
		Roughness->Alpha.OutputIndex = VertexAlphaPin;

		// Foam is aerated water, and it is the one part of the sea that is not
		// shiny. Left smooth it comes out as a white mirror.
		UMaterialExpression* SurfaceRoughness = Graph.Lerp(Roughness, Graph.Constant(0.62f), Foam);

		UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
		if (EditorData == nullptr)
		{
			return nullptr;
		}
		EditorData->BaseColor.Expression = BaseColour;
		EditorData->Roughness.Expression = SurfaceRoughness;
		EditorData->Specular.Expression = Graph.Constant(0.85f);
		EditorData->Metallic.Expression = Graph.Constant(0.0f);
		EditorData->Normal.Expression = WaveNormal;
		EditorData->WorldPositionOffset.Expression = Graph.Multiply(Up, Height);

		Material->PostEditChange();
		return Material;
	}

	UMaterialInterface* CreateFlatMaterial(UObject* Outer, const FLinearColor& Colour, float Roughness)
	{
		UMaterial* Material = NewObject<UMaterial>(Outer, NAME_None, RF_Transient);
		if (Material == nullptr)
		{
			return nullptr;
		}

		Material->MaterialDomain = MD_Surface;
		Material->SetShadingModel(MSM_DefaultLit);

		FGraph Graph;
		Graph.Material = Material;

		UMaterialExpressionConstant3Vector* Tint = Graph.Make<UMaterialExpressionConstant3Vector>();
		Tint->Constant = Colour;

		// Multiplied by vertex colour, not replacing it. The buildings and trees
		// carry all their variation per-vertex, so passing white here yields
		// exactly those colours and passing a tint shades the whole set.
		UMaterialExpressionVertexColor* VertexColour = Graph.Make<UMaterialExpressionVertexColor>();
		UMaterialExpression* Base = Graph.Multiply(Tint, VertexColour);

		UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
		if (EditorData == nullptr)
		{
			return nullptr;
		}
		EditorData->BaseColor.Expression = Base;
		EditorData->Roughness.Expression = Graph.Constant(Roughness);
		EditorData->Specular.Expression = Graph.Constant(0.4f);

		Material->PostEditChange();
		return Material;
	}
#else
	UMaterialInterface* CreateTerrainMaterial(UObject*, uint32)
	{
		// A cooked build cannot compile a shader at runtime; this needs a saved
		// asset before there is a packaged game.
		return nullptr;
	}

	UMaterialInterface* CreateFlatMaterial(UObject*, const FLinearColor&, float)
	{
		return nullptr;
	}

	UMaterialInterface* CreateWaterMaterial(UObject*)
	{
		return nullptr;
	}
#endif
}
