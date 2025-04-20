/* Copyright JsonAsAsset Contributors 2024-2025 */

#include "Importers/Types/Physics/PhysicsAssetImporter.h"
#include "Utilities/EngineUtilities.h"

#include "PhysicsEngine/PhysicsConstraintTemplate.h"

bool IPhysicsAssetImporter::Import() {
	/* CollisionDisableTable is required to port physics assets */
	if (!AssetData->HasField(TEXT("CollisionDisableTable"))) {
		SpawnPrompt("Missing CollisionDisableTable", "The provided physics asset json file is missing the 'CollisionDisableTable' property. This property is required.\n\nPlease use the FModel available on JsonAsAsset's Discord Server to correctly import the physics asset.");

		return false;
	}

	UPhysicsAsset* PhysicsAsset = NewObject<UPhysicsAsset>(Package, UPhysicsAsset::StaticClass(), *FileName, RF_Public | RF_Standalone);

	TMap<FName, FExportData> Exports = CreateExports();
	
	/* SkeletalBodySetups ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
	ProcessJsonArrayField(AssetData, TEXT("SkeletalBodySetups"), [&](const TSharedPtr<FJsonObject>& ObjectField) {
		const FName ExportName = GetExportNameOfSubobject(ObjectField->GetStringField(TEXT("ObjectName")));
		const FJsonObject* ExportJson = Exports.Find(ExportName)->Json;

		const TSharedPtr<FJsonObject> ExportProperties = ExportJson->GetObjectField(TEXT("Properties"));
		const FName BoneName = FName(*ExportProperties->GetStringField(TEXT("BoneName")));
		
		USkeletalBodySetup* BodySetup = CreateNewBody(PhysicsAsset, ExportName, BoneName);

		GetObjectSerializer()->DeserializeObjectProperties(ExportProperties, BodySetup);
	});

	/* For caching. IMPORTANT! DO NOT REMOVE! */
	PhysicsAsset->UpdateBodySetupIndexMap();
	PhysicsAsset->UpdateBoundsBodiesArray();

	/* CollisionDisableTable ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
	TArray<TSharedPtr<FJsonValue>> CollisionDisableTable = AssetData->GetArrayField(TEXT("CollisionDisableTable"));

	for (const TSharedPtr<FJsonValue> TableJSONElement : CollisionDisableTable) {
		const TSharedPtr<FJsonObject> TableObjectElement = TableJSONElement->AsObject();

		bool MapValue = TableObjectElement->GetBoolField(TEXT("Value"));
		TArray<TSharedPtr<FJsonValue>> Indices = TableObjectElement->GetObjectField(TEXT("Key"))->GetArrayField(TEXT("Indices"));

		const int32 BodyIndexA = Indices[0]->AsNumber();
		const int32 BodyIndexB = Indices[1]->AsNumber();

		/* Add to the CollisionDisableTable */
		PhysicsAsset->CollisionDisableTable.Add(FRigidBodyIndexPair(BodyIndexA, BodyIndexB), MapValue);
	}

	/* ConstraintSetup ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
	ProcessJsonArrayField(AssetData, TEXT("ConstraintSetup"), [&](const TSharedPtr<FJsonObject>& ObjectField) {
		const FName ExportName = GetExportNameOfSubobject(ObjectField->GetStringField(TEXT("ObjectName")));
		const FJsonObject* ExportJson = Exports.Find(ExportName)->Json;

		TSharedPtr<FJsonObject> ExportProperties = ExportJson->GetObjectField(TEXT("Properties"));
		UPhysicsConstraintTemplate* PhysicsConstraintTemplate = CreateNewConstraint(PhysicsAsset, ExportName);
		
		GetObjectSerializer()->DeserializeObjectProperties(ExportProperties, PhysicsConstraintTemplate);

		/* For caching. IMPORTANT! DO NOT REMOVE! */
		PhysicsConstraintTemplate->UpdateProfileInstance();
	});

	/* Simple data at end ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
	GetObjectSerializer()->DeserializeObjectProperties(RemovePropertiesShared(AssetData,
	{
		"SkeletalBodySetups",
		"ConstraintSetup",
		"BoundsBodies",
		"ThumbnailInfo",
		"CollisionDisableTable"
	}), PhysicsAsset);

	/* If the user selected a skeletal mesh in the browser, set it in the physics asset */
	const USkeletalMesh* SkeletalMesh = GetSelectedAsset<USkeletalMesh>(true);
	
	if (SkeletalMesh) {
		PhysicsAsset->PreviewSkeletalMesh = SkeletalMesh;
		PhysicsAsset->PostEditChange();
	}
	
	/* Finalize ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
	PhysicsAsset->Modify();
	PhysicsAsset->MarkPackageDirty();
	PhysicsAsset->UpdateBoundsBodiesArray();
	
	return OnAssetCreation(PhysicsAsset);
}

USkeletalBodySetup* IPhysicsAssetImporter::CreateNewBody(UPhysicsAsset* PhysAsset, const FName ExportName, const FName BoneName) {
	USkeletalBodySetup* NewBodySetup = NewObject<USkeletalBodySetup>(PhysAsset, ExportName, RF_Transactional);
	NewBodySetup->BoneName = BoneName;

	PhysAsset->SkeletalBodySetups.Add(NewBodySetup);

	return NewBodySetup;
}

UPhysicsConstraintTemplate* IPhysicsAssetImporter::CreateNewConstraint(UPhysicsAsset* PhysAsset, const FName ExportName) {
	UPhysicsConstraintTemplate* NewConstraintSetup = NewObject<UPhysicsConstraintTemplate>(PhysAsset, ExportName, RF_Transactional);
	PhysAsset->ConstraintSetup.Add(NewConstraintSetup);

	return NewConstraintSetup;
}