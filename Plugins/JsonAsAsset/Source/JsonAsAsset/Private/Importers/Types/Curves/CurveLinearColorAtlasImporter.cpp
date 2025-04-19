﻿/* Copyright JsonAsAsset Contributors 2024-2025 */

#include "Importers/Types/Curves/CurveLinearColorAtlasImporter.h"
#include "Curves/CurveLinearColorAtlas.h"
#include "Curves/CurveLinearColor.h"

bool ICurveLinearColorAtlasImporter::Import() {
	float Width = 256;
	float Height = 256;

	UCurveLinearColorAtlas* Object = NewObject<UCurveLinearColorAtlas>(Package, UCurveLinearColorAtlas::StaticClass(), *FileName, RF_Public | RF_Standalone);
	Object->Source.Init(Width, Height, 1, 1, TSF_RGBA16F);
	const int32 TextureDataSize = Object->Source.CalcMipSize(0);
	Object->SrcData.AddUninitialized(TextureDataSize);
	uint32* TextureData = reinterpret_cast<uint32*>(Object->Source.LockMip(0));
	FFloat16Color InitColor(FLinearColor::White);
	for (uint32 y = 0; y < Object->TextureSize; y++) {
		for (uint32 x = 0; x < Object->TextureSize; x++) {
			Object->SrcData[x * Object->TextureSize + y] = InitColor;
		}
	}
	FMemory::Memcpy(TextureData, Object->SrcData.GetData(), TextureDataSize);
	Object->Source.UnlockMip(0);

	Object->UpdateResource();

	bool bHasAnyDirtyTextures = false;
	if (AssetData->TryGetBoolField(TEXT("bHasAnyDirtyTextures"), bHasAnyDirtyTextures)) {
		Object->bHasAnyDirtyTextures = bHasAnyDirtyTextures;
	}

	bool bIsDirty = false;
	if (AssetData->TryGetBoolField(TEXT("bIsDirty"), bIsDirty)) {
		Object->bIsDirty = bIsDirty;
	}

	bool bShowDebugColorsForNullGradients = false;
	if (AssetData->TryGetBoolField(TEXT("bShowDebugColorsForNullGradients"), bShowDebugColorsForNullGradients)) {
		Object->bShowDebugColorsForNullGradients = bShowDebugColorsForNullGradients;
	}

	bool bSquareResolution = false;

#if !UE4_27_ONLY_BELOW
	if (AssetData->TryGetBoolField(TEXT("bSquareResolution"), bSquareResolution)) {
		Object->bSquareResolution = bSquareResolution;
	}
#endif

#if ENGINE_MAJOR_VERSION == 5
	float TextureSize = 0.0f;
	float TextureHeight = 0.0f;
#else
	double TextureSize = 0.0f;
	double TextureHeight = 0.0f;
#endif
	
	if (AssetData->TryGetNumberField(TEXT("TextureSize"), TextureSize)) {
		Object->TextureSize = TextureSize;
	}

#if !UE4_27_ONLY_BELOW
	if (AssetData->TryGetNumberField(TEXT("TextureHeight"), TextureHeight)) {
		Object->TextureHeight = TextureHeight;
	}
#endif

	FProperty* TextureSizeProperty = FindFProperty<FProperty>(Object->GetClass(), "TextureSize");
	FPropertyChangedEvent TextureSizePropertyPropertyChangedEvent(TextureSizeProperty, EPropertyChangeType::ValueSet);
	Object->PostEditChangeProperty(TextureSizePropertyPropertyChangedEvent);

	/* Add gradient curves */
	FProperty* GradientCurvesProperty = FindFProperty<FProperty>(Object->GetClass(), "GradientCurves");
	FPropertyChangedEvent PropertyChangedEvent(GradientCurvesProperty, EPropertyChangeType::ArrayAdd);

	const TArray<TSharedPtr<FJsonValue>> GradientCurves = AssetData->GetArrayField(TEXT("GradientCurves"));
	TArray<TObjectPtr<UCurveLinearColor>> CurvesLocal;

#if ENGINE_UE5
	CurvesLocal = LoadObject(GradientCurves, CurvesLocal);
	Object->GradientCurves = CurvesLocal;
	Object->PostEditChangeProperty(PropertyChangedEvent);
#else
	CurvesLocal = LoadObject(GradientCurves, CurvesLocal);

	/* Convert TObjectPtr<UCurveLinearColor> to UCurveLinearColor* and assign to Object->GradientCurves */
	TArray<UCurveLinearColor*> RawCurves;
	for (const TObjectPtr<UCurveLinearColor>& Curve : CurvesLocal) {
		RawCurves.Add(Curve.Get());
	}

	Object->GradientCurves = RawCurves;
#endif

	/* Handle edit changes, and add it to the content browser */
	return OnAssetCreation(Object);
}