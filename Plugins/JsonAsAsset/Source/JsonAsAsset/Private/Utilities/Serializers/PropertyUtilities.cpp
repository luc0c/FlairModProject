/* Copyright JsonAsAsset Contributors 2024-2025 */

#include "Utilities/Serializers/PropertyUtilities.h"

#include "GameplayTagContainer.h"
#include "Importers/Constructor/Importer.h"
#include "Utilities/Serializers/ObjectUtilities.h"
#include "UObject/TextProperty.h"

/* Struct Serializers */
#include "Utilities/Serializers/Structs/DateTimeSerializer.h"
#include "Utilities/Serializers/Structs/FallbackStructSerializer.h"
#include "Utilities/Serializers/Structs/TimespanSerializer.h"

DECLARE_LOG_CATEGORY_CLASS(LogPropertySerializer, Error, Log);
PRAGMA_DISABLE_OPTIMIZATION

UPropertySerializer::UPropertySerializer() {
	this->FallbackStructSerializer = MakeShared<FFallbackStructSerializer>(this);

	UScriptStruct* DateTimeStruct = FindObject<UScriptStruct>(nullptr, TEXT("/Script/CoreUObject.DateTime"));
	UScriptStruct* TimespanStruct = FindObject<UScriptStruct>(nullptr, TEXT("/Script/CoreUObject.Timespan"));
	check(DateTimeStruct);
	check(TimespanStruct);

	this->StructSerializers.Add(DateTimeStruct, MakeShared<FDateTimeSerializer>());
	this->StructSerializers.Add(TimespanStruct, MakeShared<FTimespanSerializer>());
}

void UPropertySerializer::DeserializePropertyValue(FProperty* Property, const TSharedRef<FJsonValue>& JsonValue, void* OutValue) {
	const FMapProperty* MapProperty = CastField<const FMapProperty>(Property);
	const FSetProperty* SetProperty = CastField<const FSetProperty>(Property);
	const FArrayProperty* ArrayProperty = CastField<const FArrayProperty>(Property);

	TSharedRef<FJsonValue> NewJsonValue = JsonValue;
	
	if (BlacklistedPropertyNames.Contains(Property->GetName())) {
		return;
	}

	if (MapProperty) {
		FProperty* KeyProperty = MapProperty->KeyProp;
		FProperty* ValueProperty = MapProperty->ValueProp;
		FScriptMapHelper MapHelper(MapProperty, OutValue);
		const TArray<TSharedPtr<FJsonValue>>& PairArray = NewJsonValue->AsArray();

		for (int32 i = 0; i < PairArray.Num(); i++) {
			const TSharedPtr<FJsonObject>& Pair = PairArray[i]->AsObject();
			const TSharedPtr<FJsonValue>& EntryKey = Pair->Values.FindChecked(TEXT("Key"));
			const TSharedPtr<FJsonValue>& EntryValue = Pair->Values.FindChecked(TEXT("Value"));
			const int32 Index = MapHelper.AddDefaultValue_Invalid_NeedsRehash();
			uint8* PairPtr = MapHelper.GetPairPtr(Index);

			/* Copy over imported key and value from temporary storage */
			DeserializePropertyValue(KeyProperty, EntryKey.ToSharedRef(), PairPtr);
			DeserializePropertyValue(ValueProperty, EntryValue.ToSharedRef(), PairPtr + MapHelper.MapLayout.ValueOffset);
		}
		MapHelper.Rehash();

	} else if (SetProperty) {
		FProperty* ElementProperty = SetProperty->ElementProp;
		FScriptSetHelper SetHelper(SetProperty, OutValue);
		const TArray<TSharedPtr<FJsonValue>>& SetArray = NewJsonValue->AsArray();
		SetHelper.EmptyElements();
		uint8* TempElementStorage = static_cast<uint8*>(FMemory::Malloc(ElementProperty->ElementSize));
		ElementProperty->InitializeValue(TempElementStorage);

		for (int32 i = 0; i < SetArray.Num(); i++) {
			const TSharedPtr<FJsonValue>& Element = SetArray[i];
			DeserializePropertyValue(ElementProperty, Element.ToSharedRef(), TempElementStorage);

			const int32 NewElementIndex = SetHelper.AddDefaultValue_Invalid_NeedsRehash();
			uint8* NewElementPtr = SetHelper.GetElementPtr(NewElementIndex);

			/* Copy over imported key from temporary storage */
			ElementProperty->CopyCompleteValue_InContainer(NewElementPtr, TempElementStorage);
		}
		SetHelper.Rehash();

		ElementProperty->DestroyValue(TempElementStorage);
		FMemory::Free(TempElementStorage);
	} else if (ArrayProperty) {
		FProperty* ElementProperty = ArrayProperty->Inner;
		FScriptArrayHelper ArrayHelper(ArrayProperty, OutValue);
		const TArray<TSharedPtr<FJsonValue>>& SetArray = NewJsonValue->AsArray();
		ArrayHelper.EmptyValues();

		for (int32 i = 0; i < SetArray.Num(); i++) {
			const TSharedPtr<FJsonValue>& Element = SetArray[i];
			const uint32 AddedIndex = ArrayHelper.AddValue();
			uint8* ValuePtr = ArrayHelper.GetRawPtr(AddedIndex);
			DeserializePropertyValue(ElementProperty, Element.ToSharedRef(), ValuePtr);
		}
	}
	else if (Property->IsA<FMulticastDelegateProperty>()) {
	} else if (Property->IsA<FDelegateProperty>()) {
	} else if (CastField<const FInterfaceProperty>(Property)) {
	}
	else if (FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property)) {
		TSharedPtr<FJsonObject> SoftJsonObjectProperty;
		FString PathString = "";
		
		switch (NewJsonValue->Type) {
			/* FModel, extract it from the object */
			case EJson::Object:
				SoftJsonObjectProperty = NewJsonValue->AsObject();
				PathString = SoftJsonObjectProperty->GetStringField(TEXT("AssetPathName"));
			break;

			/* Older game builds */
			default:
				PathString = NewJsonValue->AsString();
			break;
		}

		if (PathString != "") {
			FSoftObjectPtr* ObjectPtr = static_cast<FSoftObjectPtr*>(OutValue);
			*ObjectPtr = FSoftObjectPath(PathString);

			if (!ObjectPtr->LoadSynchronous()) {
				/* Try importing it using Local Fetch */
				FString PackagePath;
				FString AssetName;
				PathString.Split(".", &PackagePath, &AssetName);
				TObjectPtr<UObject> T;

				FString PropertyClassName = SoftObjectProperty->PropertyClass->GetName();
				
				IImporter::DownloadWrapper(T, PropertyClassName, AssetName, PackagePath);
			}
		}
	}
	else if (const FObjectPropertyBase* ObjectProperty = CastField<const FObjectPropertyBase>(Property)) {
		/* Need to serialize full UObject for object property */
		TObjectPtr<UObject> Object = nullptr;

		if (NewJsonValue->IsNull()) {
			ObjectProperty->SetObjectPropertyValue(OutValue, nullptr);
		}

		auto JsonValueAsObject = NewJsonValue->AsObject();
		bool bUseDefaultLoadObject = !JsonValueAsObject->GetStringField(TEXT("ObjectName")).Contains(":ParticleModule");

		if (bUseDefaultLoadObject) {
			/* Use IImporter to import the object */
			IImporter* Importer = new IImporter();

			Importer->ParentObject = ObjectSerializer->ParentAsset;
			Importer->LoadObject(&JsonValueAsObject, Object);

			if (Object == nullptr) {
				if (ObjectProperty && ObjectProperty->PropertyClass) {
					UStruct* Struct = ObjectProperty->PropertyClass;

					FFailedPropertyInfo PropertyInfo;
					PropertyInfo.ClassName = ObjectProperty->PropertyClass->GetName();
					PropertyInfo.SuperStructName = Struct->GetSuperStruct() ? Struct->GetSuperStruct()->GetName() : TEXT("None");
					PropertyInfo.ObjectPath = JsonValueAsObject->GetStringField(TEXT("ObjectPath"));
					
					if (!FailedProperties.Contains(PropertyInfo)) {
						FailedProperties.Add(PropertyInfo);
					}
				}
			}

			if (Object != nullptr && !Cast<UActorComponent>(Object.Get())) {
				ObjectProperty->SetObjectPropertyValue(OutValue, Object);
			}

			if (Object != nullptr) {
				/* Get the export */
				if (TSharedPtr<FJsonObject> Export = GetExport(JsonValueAsObject.Get(), ObjectSerializer->Exports)) {
					if (Export->HasField(TEXT("Properties"))) {
						TSharedPtr<FJsonObject> Properties = Export->GetObjectField(TEXT("Properties"));

						if (Export->HasField(TEXT("LODData"))) {
							Properties->SetArrayField(TEXT("LODData"), Export->GetArrayField(TEXT("LODData")));
						}
						
						ObjectSerializer->DeserializeObjectProperties(Properties, Object);
					}
				}
			}
		}

		FString ObjectName = JsonValueAsObject->GetStringField(TEXT("ObjectName"));
		FString ObjectPath = JsonValueAsObject->GetStringField(TEXT("ObjectPath"));

		if (ObjectName.Contains(".")) {
			ObjectName.Split(".", nullptr, &ObjectName);
			ObjectName.Split("'", &ObjectName, nullptr);
		}

		if (ObjectName.Contains(":")) {
			ObjectName.Split(":", nullptr, &ObjectName);
			ObjectName.Split("'", &ObjectName, nullptr);
		}

		if (FUObjectExport Export = ExportsContainer.Find(ObjectName); Export.Object != nullptr) {
			UObject* FoundObject = Export.Object;

			if (FoundObject) {
				ObjectProperty->SetObjectPropertyValue(OutValue, FoundObject);
			}
		}

		if (UObject* Parent = ObjectSerializer->ParentAsset) {
			FString Name = Parent->GetName();

			if (FUObjectExport Export = ExportsContainer.Find(ObjectName, Name); Export.Object != nullptr) {
				UObject* FoundObject = Export.Object;

				if (FoundObject) {
					ObjectProperty->SetObjectPropertyValue(OutValue, FoundObject);
				}
			}
		}
	}
	else if (const FStructProperty* StructProperty = CastField<const FStructProperty>(Property)) {
		if (StructProperty->Struct == FGameplayTag::StaticStruct()) {
			FGameplayTag* GameplayTagStr = static_cast<FGameplayTag*>(OutValue);
			FGameplayTag NewTag = FGameplayTag::RequestGameplayTag(FName(*NewJsonValue->AsObject()->GetStringField("TagName")), false);
			*GameplayTagStr = NewTag;
			return;
		}

		/* FGameplayTagContainer (handled from FModel data) */
		if (StructProperty->Struct == FGameplayTagContainer::StaticStruct()) {
			FGameplayTagContainer* GameplayTagContainerStr = static_cast<FGameplayTagContainer*>(OutValue);

			auto GameplayTags = JsonValue->AsArray();

			for (TSharedPtr<FJsonValue> GameplayTagValue : GameplayTags) {
				FString GameplayTagString = GameplayTagValue->AsString();
				FGameplayTag GameplayTag = FGameplayTag::RequestGameplayTag(FName(*GameplayTagString));
				
				GameplayTagContainerStr->AddTag(GameplayTag);
			}
			
			return;
		}

		if (StructProperty->Struct->GetFName() == "SoftObjectPath") {
			TSharedPtr<FJsonObject> SoftJsonObjectProperty;
			FString PathString = "";
		
			SoftJsonObjectProperty = NewJsonValue->AsObject();
			PathString = SoftJsonObjectProperty->GetStringField(TEXT("AssetPathName"));
			
			if (PathString != "") {
				FSoftObjectPtr* ObjectPtr = static_cast<FSoftObjectPtr*>(OutValue);
				*ObjectPtr = FSoftObjectPath(PathString);

				if (!ObjectPtr->LoadSynchronous()) {
					/* Try importing it using Local Fetch */
					FString PackagePath;
					FString AssetName;
					PathString.Split(".", &PackagePath, &AssetName);
					TObjectPtr<UObject> T;

					FString PropertyClassName = "DataAsset";
				
					IImporter::DownloadWrapper(T, PropertyClassName, AssetName, PackagePath);
				}
			}
		}
		
		/* JSON for FGuids are FStrings */
		FString OutString;
		
		if (JsonValue->TryGetString(OutString)) {
			FGuid GUID = FGuid(OutString); /* Create GUID from String */

			TSharedRef<FJsonObject> SharedObject = MakeShareable(new FJsonObject());
			SharedObject->SetNumberField(TEXT("A"), GUID.A); SharedObject->SetNumberField(TEXT("B"), GUID.B);
			SharedObject->SetNumberField(TEXT("C"), GUID.C); SharedObject->SetNumberField(TEXT("D"), GUID.D);

			const TSharedRef<FJsonValue> NewValue = MakeShareable(new FJsonValueObject(SharedObject));
			NewJsonValue = NewValue;
		}

		/* To serialize struct, we need its type and value pointer, because struct value doesn't contain type information */
		DeserializeStruct(StructProperty->Struct, NewJsonValue->AsObject().ToSharedRef(), OutValue);
	}
	else if (const FByteProperty* ByteProperty = CastField<const FByteProperty>(Property)) {
		/* If we have a string provided, make sure Enum is not null */
		if (JsonValue->Type == EJson::String) {
			FString EnumAsString = JsonValue->AsString();

			check(ByteProperty->Enum);
			int64 EnumerationValue = ByteProperty->Enum->GetValueByNameString(EnumAsString);

			ByteProperty->SetIntPropertyValue(OutValue, EnumerationValue);
		}
		else {
			/* Should be a number, set property value accordingly */
			const int64 NumberValue = static_cast<int64>(NewJsonValue->AsNumber());
			ByteProperty->SetIntPropertyValue(OutValue, NumberValue);
		}
		/* Primitives below, they are serialized as plain json values */
	}
	else if (const FNumericProperty* NumberProperty = CastField<const FNumericProperty>(Property)) {
		const double NumberValue = NewJsonValue->AsNumber();
		if (NumberProperty->IsFloatingPoint())
			NumberProperty->SetFloatingPointPropertyValue(OutValue, NumberValue);
		else NumberProperty->SetIntPropertyValue(OutValue, static_cast<int64>(NumberValue));
	}
	else if (const FBoolProperty* BoolProperty = CastField<const FBoolProperty>(Property)) {
		const bool bBooleanValue = NewJsonValue->AsBool();
		BoolProperty->SetPropertyValue(OutValue, bBooleanValue);
	}
	else if (Property->IsA<FStrProperty>()) {
		const FString StringValue = NewJsonValue->AsString();
		*static_cast<FString*>(OutValue) = StringValue;
	}
	else if (const FEnumProperty* EnumProperty = CastField<const FEnumProperty>(Property)) {
		FString EnumAsString = NewJsonValue->AsString();

		if (EnumAsString.Contains("::")) {
			EnumAsString.Split("::", nullptr, &EnumAsString);
		}
		
		/* Prefer readable enum names in result json to raw numbers */
		int64 EnumerationValue = EnumProperty->GetEnum()->GetValueByNameString(EnumAsString);

		if (EnumerationValue != INDEX_NONE) {
			EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(OutValue, EnumerationValue);
		}
	}
	else if (Property->IsA<FNameProperty>()) {
		/* Name is perfectly representable as string */
		const FString NameString = NewJsonValue->AsString();
		*static_cast<FName*>(OutValue) = *NameString;
	}
	else if (const FTextProperty* TextProperty = CastField<const FTextProperty>(Property)) {
		/* For FText, standard ExportTextItem is okay to use, because it's serialization is quite complex */
		const FString SerializedValue = NewJsonValue->AsString();
		
		if (!SerializedValue.IsEmpty()) {
			FTextStringHelper::ReadFromBuffer(*SerializedValue, *static_cast<FText*>(OutValue));
		} else {
			/* TODO: Somehow add other needed things like Namespace, Key, and LocalizedString */
			TSharedPtr<FJsonObject> Object = NewJsonValue->AsObject().ToSharedRef();

			/* Retrieve properties */
			FString TextNamespace = Object->GetStringField(TEXT("Namespace"));
			FString UniqueKey = Object->GetStringField(TEXT("Key"));
			FString SourceString = Object->GetStringField(TEXT("SourceString"));

			TextProperty->SetPropertyValue(OutValue, FInternationalization::ForUseOnlyByLocMacroAndGraphNodeTextLiterals_CreateText(*SourceString, *TextNamespace, *UniqueKey));
		}
	}
	else if (CastField<const FFieldPathProperty>(Property)) {
		FFieldPath FieldPath;
		FieldPath.Generate(*NewJsonValue->AsString());
		*static_cast<FFieldPath*>(OutValue) = FieldPath;
	}
	else {
		UE_LOG(LogPropertySerializer, Fatal, TEXT("Found unsupported property type when deserializing value: %s"), *Property->GetClass()->GetName());
	}
}

void UPropertySerializer::ClearCachedData() {
	FailedProperties.Empty();
}

void UPropertySerializer::DisablePropertySerialization(UStruct* Struct, const FName PropertyName) {
	FProperty* Property = Struct->FindPropertyByName(PropertyName);
	checkf(Property, TEXT("Cannot find Property %s in Struct %s"), *PropertyName.ToString(), *Struct->GetPathName());
	this->PinnedStructs.Add(Struct);
	this->BlacklistedProperties.Add(Property);
}

void UPropertySerializer::AddStructSerializer(UScriptStruct* Struct, const TSharedPtr<FStructSerializer>& Serializer) {
	this->PinnedStructs.Add(Struct);
	this->StructSerializers.Add(Struct, Serializer);
}

bool UPropertySerializer::ShouldDeserializeProperty(FProperty* Property) const {
	/* Skip deprecated properties */
	if (Property->HasAnyPropertyFlags(CPF_Deprecated)) {
		return false;
	}
	/* Skip blacklisted properties */
	if (BlacklistedProperties.Contains(Property)) {
		return false;
	}
	return true;
}

void UPropertySerializer::DeserializeStruct(UScriptStruct* Struct, const TSharedRef<FJsonObject>& Properties, void* OutValue) const {
	FStructSerializer* StructSerializer = GetStructSerializer(Struct);
	StructSerializer->Deserialize(Struct, OutValue, Properties);
}

FStructSerializer* UPropertySerializer::GetStructSerializer(const UScriptStruct* Struct) const {
	check(Struct);
	TSharedPtr<FStructSerializer> const* StructSerializer = StructSerializers.Find(Struct);
	return StructSerializer && ensure(StructSerializer->IsValid()) ? StructSerializer->Get() : FallbackStructSerializer.Get();
}

PRAGMA_ENABLE_OPTIMIZATION