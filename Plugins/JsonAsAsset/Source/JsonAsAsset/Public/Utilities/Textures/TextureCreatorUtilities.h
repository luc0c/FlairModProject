﻿/* Copyright JsonAsAsset Contributors 2024-2025 */

#pragma once

#include "Utilities/Serializers/PropertyUtilities.h"
#include "Dom/JsonObject.h"

struct FTextureCreatorUtilities {
public:
	FTextureCreatorUtilities(const FString& FileName, const FString& FilePath, UPackage* Package, 
			  UPackage* OutermostPkg)
		: FileName(FileName), FilePath(FilePath), Package(Package), OutermostPkg(OutermostPkg)
	{
		PropertySerializer = NewObject<UPropertySerializer>();
		GObjectSerializer = NewObject<UObjectSerializer>();

		GObjectSerializer->SetPropertySerializer(PropertySerializer);
	}

	bool CreateTexture2D(UTexture*& OutTexture2D, TArray<uint8>& Data, const TSharedPtr<FJsonObject>& Properties) const;
	bool CreateTextureCube(UTexture*& OutTextureCube, const TArray<uint8>& Data, const TSharedPtr<FJsonObject>& Properties) const;
	bool CreateVolumeTexture(UTexture*& OutVolumeTexture, TArray<uint8>& Data, const TSharedPtr<FJsonObject>& Properties) const;
	bool CreateRenderTarget2D(UTexture*& OutRenderTarget2D, const TSharedPtr<FJsonObject>& Properties) const;

	/* Deserialization functions */
	bool DeserializeTexture2D(UTexture2D* InTexture2D, const TSharedPtr<FJsonObject>& Properties) const;
	bool DeserializeTexture(UTexture* Texture, const TSharedPtr<FJsonObject>& Properties) const;

private:
	static void GetDecompressedTextureData(uint8* Data, uint8*& OutData, const int SizeX, const int SizeY, const int SizeZ, const int TotalSize, const EPixelFormat Format);

protected:
	FString FileName;
	FString FilePath;
	UPackage* Package;
	UPackage* OutermostPkg;
	UPropertySerializer* PropertySerializer;
	UObjectSerializer* GObjectSerializer;

public:
	FORCEINLINE UObjectSerializer* GetObjectSerializer() const { return GObjectSerializer; }
};
