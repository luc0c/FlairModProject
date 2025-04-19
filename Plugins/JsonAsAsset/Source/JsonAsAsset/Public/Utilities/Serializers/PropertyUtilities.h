/* Copyright JsonAsAsset Contributors 2024-2025 */

#pragma once

#include "ObjectUtilities.h"
#include "Containers/ObjectExport.h"
#include "Dom/JsonObject.h"
#include "Structs/StructSerializer.h"
#include "UObject/Object.h"
#include "UObject/UnrealType.h"
#include "PropertyUtilities.generated.h"

class UObjectSerializer;

USTRUCT()
struct FFailedPropertyInfo
{
	GENERATED_BODY()
public:
	FString ClassName;
	FString ObjectPath;
	FString SuperStructName;

	bool operator==(const FFailedPropertyInfo& Other) const
	{
		return ClassName == Other.ClassName &&
			   SuperStructName == Other.SuperStructName &&
			   ObjectPath == Other.ObjectPath;
	}
};

UCLASS()
class JSONASASSET_API UPropertySerializer : public UObject
{
	GENERATED_BODY()

	friend class UObjectSerializer;

	UPROPERTY()
	UObjectSerializer* ObjectSerializer;

	UPROPERTY()
	TArray<UStruct*> PinnedStructs;
	TArray<FProperty*> BlacklistedProperties;

	TSharedPtr<FStructSerializer> FallbackStructSerializer;
	TMap<UScriptStruct*, TSharedPtr<FStructSerializer>> StructSerializers;
public:
	UPropertySerializer();

	FUObjectExportContainer ExportsContainer;

	TArray<FString> BlacklistedPropertyNames;
	
	TArray<FFailedPropertyInfo> FailedProperties;
	void ClearCachedData();

	/** Disables property serialization entirely */
	void DisablePropertySerialization(UStruct* Struct, FName PropertyName);
	void AddStructSerializer(UScriptStruct* Struct, const TSharedPtr<FStructSerializer>& Serializer);

	/** Checks whenever we should serialize property in question at all */
	bool ShouldDeserializeProperty(FProperty* Property) const;

	void DeserializePropertyValue(FProperty* Property, const TSharedRef<FJsonValue>& Value, void* OutValue);
	void DeserializeStruct(UScriptStruct* Struct, const TSharedRef<FJsonObject>& Value, void* OutValue) const;

private:
	FStructSerializer* GetStructSerializer(const UScriptStruct* Struct) const;
};

/* Use to handle differentiating formats produced by CUE4Parse */
inline bool PassthroughPropertyHandler(FProperty* Property, const FString& PropertyName, void* PropertyValue, const TSharedPtr<FJsonObject>& Properties, UPropertySerializer* PropertySerializer) {
	/* Handles static arrays in the format of: PropertyName[Index] */
	if (Property->ArrayDim != 1) {
		TArray<TSharedPtr<FJsonValue>> ArrayElements;

		/* Finds array elements with the format: PropertyName[Index] and sets them properly into an array */
		for (const auto& Pair : Properties->Values) {
			const FString& Key = Pair.Key;
			const TSharedPtr<FJsonValue> Value = Pair.Value;

			/* If it doesn't start with the same property name */
			if (!Key.StartsWith(PropertyName)) continue;

			/* By default, it should be 0 */
			int32 CurrentArrayIndex = 0;

			/* If it is formatted like PropertyName[Index] */
			if (Key.Contains("[") && Key.Contains("]")) {
				int32 OpenBracketPos, CloseBracketPos;

				/* Find the index in PropertyName[Index] (integer) */
				if (Key.FindChar('[', OpenBracketPos) && Key.FindChar(']', CloseBracketPos) && CloseBracketPos > OpenBracketPos) {
					FString IndexStr = Key.Mid(OpenBracketPos + 1, CloseBracketPos - OpenBracketPos - 1);
					
					CurrentArrayIndex = FCString::Atoi(*IndexStr);
				}
			}

			/* Fail-save? */
			if (CurrentArrayIndex >= ArrayElements.Num()) {
				ArrayElements.SetNum(CurrentArrayIndex + 1);
			}
			
			ArrayElements[CurrentArrayIndex] = Value;
		}

		/* Array elements is filled up, now we set them in the property value */
		for (int32 ArrayIndex = 0; ArrayIndex < ArrayElements.Num(); ArrayIndex++) {
			uint8* ArrayPropertyValue = static_cast<uint8*>(PropertyValue) + Property->ElementSize * ArrayIndex;

			if (!ArrayElements.IsValidIndex(ArrayIndex)) continue;

			TSharedPtr<FJsonValue> ArrayJsonElement = ArrayElements[ArrayIndex];

			/* Check to see if it's null */
			if (ArrayJsonElement == nullptr || ArrayJsonElement->IsNull()) continue;
			
			const TSharedRef<FJsonValue> ArrayJsonValue = ArrayJsonElement.ToSharedRef();

			PropertySerializer->DeserializePropertyValue(Property, ArrayJsonValue, ArrayPropertyValue);
		}

		return true;
	}

	return false;
}