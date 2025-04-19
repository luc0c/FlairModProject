/* Copyright JsonAsAsset Contributors 2024-2025 */

#pragma once

#include "Importers/Constructor/Importer.h"

#include "PhysicsEngine/PhysicsAsset.h"

/* SkeletalBodySetup is a separate file on UE5.5+ */
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION > 4
#include "PhysicsEngine/SkeletalBodySetup.h"
#endif

class IPhysicsAssetImporter : public IImporter {
public:
	IPhysicsAssetImporter(const FString& FileName, const FString& FilePath, const TSharedPtr<FJsonObject>& JsonObject, UPackage* Package, UPackage* OutermostPkg, const TArray<TSharedPtr<FJsonValue>>& AllJsonObjects, UClass* AssetClass):
		IImporter(FileName, FilePath, JsonObject, Package, OutermostPkg, AllJsonObjects, AssetClass) {
	}
	
	virtual bool Import() override;

	static USkeletalBodySetup* CreateNewBody(UPhysicsAsset* PhysAsset, FName ExportName, FName BoneName);
	static UPhysicsConstraintTemplate* CreateNewConstraint(UPhysicsAsset* PhysAsset, FName ExportName);
};

REGISTER_IMPORTER(IPhysicsAssetImporter, {
	"PhysicsAsset"
}, "Physics Assets");