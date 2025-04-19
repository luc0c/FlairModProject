/* Copyright JsonAsAsset Contributors 2024-2025 */

#include "Importers/Types/Tables/StringTableImporter.h"

#include "Internationalization/StringTable.h"
#include "Internationalization/StringTableCore.h"

bool IStringTableImporter::Import() {
	/* Create StringTable from Package */
	UStringTable* StringTable = NewObject<UStringTable>(Package, UStringTable::StaticClass(), *FileName, RF_Public | RF_Standalone);

	if (AssetData->HasField(TEXT("StringTable"))) {
		const TSharedPtr<FJsonObject> StringTableData = AssetData->GetObjectField(TEXT("StringTable"));
		const FStringTableRef MutableStringTable = StringTable->GetMutableStringTable();

		/* Set Table Namespace */
		MutableStringTable->SetNamespace(StringTableData->GetStringField(TEXT("TableNamespace")));

		/* Set "SourceStrings" from KeysToEntries */
		const TSharedPtr<FJsonObject> KeysToEntries = StringTableData->GetObjectField(TEXT("KeysToEntries"));
	
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : KeysToEntries->Values) {
			FString Key = Pair.Key;
			FString SourceString = Pair.Value->AsString();

			MutableStringTable->SetSourceString(Key, SourceString);
		}

		/* Set Metadata from KeysToMetaData */
		const TSharedPtr<FJsonObject> KeysToMetaData = StringTableData->GetObjectField(TEXT("KeysToMetaData"));

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : KeysToMetaData->Values) {
			const FString TableKey = Pair.Key;
			const TSharedPtr<FJsonObject> MetadataObject = Pair.Value->AsObject();

			for (const TPair<FString, TSharedPtr<FJsonValue>>& MetadataPair : MetadataObject->Values) {
				const FName TextKey = *MetadataPair.Key;
				FString MetadataValue = MetadataPair.Value->AsString();
			
				MutableStringTable->SetMetaData(TableKey, TextKey, MetadataValue);
			}
		}
	}

	/* Handle edit changes, and add it to the content browser */
	return OnAssetCreation(StringTable);
}
