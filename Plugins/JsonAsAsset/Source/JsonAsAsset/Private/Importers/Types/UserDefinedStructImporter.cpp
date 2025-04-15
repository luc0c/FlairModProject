/* Copyright JsonAsAsset Contributors 2024-2025 */

#include "Importers/Types/UserDefinedStructImporter.h"

#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "Kismet2/StructureEditorUtils.h"

static const FRegexPattern PropertyNameRegexPattern(TEXT(R"((.*)_(\d+)_([0-9A-Z]+))"));

static const TMap<FString, const FName> PropertyCategoryMap = {
    {TEXT("BoolProperty"), TEXT("bool")},
    {TEXT("ByteProperty"), TEXT("byte")},
    {TEXT("IntProperty"), TEXT("int")},
    {TEXT("Int64Property"), TEXT("int64")},
    {TEXT("FloatProperty"), TEXT("real")},
    {TEXT("DoubleProperty"), TEXT("real")},
    {TEXT("StrProperty"), TEXT("string")},
    {TEXT("TextProperty"), TEXT("text")},
    {TEXT("NameProperty"), TEXT("name")},
    {TEXT("ClassProperty"), TEXT("class")},
    {TEXT("SoftClassProperty"), TEXT("softclass")},
    {TEXT("ObjectProperty"), TEXT("object")},
    {TEXT("SoftObjectProperty"), TEXT("softobject")},
    {TEXT("EnumProperty"), TEXT("byte")},
    {TEXT("StructProperty"), TEXT("struct")},
};

static const TMap<FString, EPinContainerType> ContainerTypeMap = {
    {TEXT("ArrayProperty"), EPinContainerType::Array},
    {TEXT("MapProperty"), EPinContainerType::Map},
    {TEXT("SetProperty"), EPinContainerType::Set},
};

bool IUserDefinedStructImporter::Import() {
    UUserDefinedStruct* UserDefinedStruct = FStructureEditorUtils::CreateUserDefinedStruct(Package, *FileName, RF_Standalone | RF_Public | RF_Transactional);

    DefaultProperties = AssetData->GetObjectField("DefaultProperties");
    GetObjectSerializer()->DeserializeObjectProperties(KeepPropertiesShared(AssetData,
    {
        "Guid",
        "StructFlags"
    }), UserDefinedStruct);

    /* Struct Metadata [Editor Only Data] */
    CookedStructMetaData = GetExport("StructCookedMetaData", AllJsonObjects, true);
    
    if (CookedStructMetaData.IsValid() && CookedStructMetaData->HasField(TEXT("StructMetaData"))) {
        TArray<TSharedPtr<FJsonValue>> ObjectMetaData = CookedStructMetaData->GetObjectField(TEXT("StructMetaData"))->GetObjectField(TEXT("ObjectMetaData"))->GetArrayField(TEXT("ObjectMetaData"));

        for (const TSharedPtr<FJsonValue> ObjectMetadataValue : ObjectMetaData) {
            TSharedPtr<FJsonObject> ObjectMetadataObject = ObjectMetadataValue->AsObject();

            FString MetadataKey = ObjectMetadataObject->GetStringField(TEXT("Key"));
            FString MetadataValue = ObjectMetadataObject->GetStringField(TEXT("Value"));

            UserDefinedStruct->SetMetaData(FName(*MetadataKey), *MetadataValue);

            /* Tooltip is a part of EditorData */
            if (MetadataKey == TEXT("Tooltip")) {
                FStructureEditorUtils::ChangeTooltip(UserDefinedStruct, MetadataValue);
            }
        }
    }
    
    /* Remove default variable */
    FStructureEditorUtils::GetVarDesc(UserDefinedStruct).Pop();

    const TArray<TSharedPtr<FJsonValue>> ChildProperties = AssetData->GetArrayField(TEXT("ChildProperties"));
    
    for (const TSharedPtr<FJsonValue> Property : ChildProperties) {
        const TSharedPtr<FJsonObject> PropertyObject = Property->AsObject();
        
        ImportPropertyIntoStruct(UserDefinedStruct, PropertyObject);
    }

    /* Handle edit changes, and add it to the content browser */
    return OnAssetCreation(UserDefinedStruct);
}

void IUserDefinedStructImporter::ImportPropertyIntoStruct(UUserDefinedStruct* UserDefinedStruct, const TSharedPtr<FJsonObject> &PropertyJsonObject) {
    const FString Name = PropertyJsonObject->GetStringField(TEXT("Name"));
    const FString Type = PropertyJsonObject->GetStringField(TEXT("Type"));

    FString FieldDisplayName = Name;
    FGuid FieldGuid;

    FRegexMatcher RegexMatcher(PropertyNameRegexPattern, Name);
    
    if (RegexMatcher.FindNext()) {
        /* Import properties keeping GUID if present */
        FieldDisplayName = RegexMatcher.GetCaptureGroup(1);
        FieldGuid = FGuid(RegexMatcher.GetCaptureGroup(3));
    } else {
        CastChecked<UUserDefinedStructEditorData>(UserDefinedStruct->EditorData)->GenerateUniqueNameIdForMemberVariable();
        FieldGuid = FGuid::NewGuid();
    }

    FStructVariableDescription Variable; {
        Variable.VarName = *Name;
        Variable.FriendlyName = FieldDisplayName;
        Variable.VarGuid = FieldGuid;

        Variable.SetPinType(ResolvePropertyPinType(PropertyJsonObject));
    }

    FStructureEditorUtils::GetVarDesc(UserDefinedStruct).Add(Variable);
    FStructureEditorUtils::OnStructureChanged(UserDefinedStruct, FStructureEditorUtils::EStructureEditorChangeInfo::AddedVariable);

    const TSharedPtr<FJsonValue>& PropertyJsonValue = DefaultProperties->Values.FindChecked(Name);

    FProperty* Property = FindFProperty<FProperty>(UserDefinedStruct, *Name);

    if (Property == nullptr) {
        return;
    }

    /* DefaultProperties ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
    FStructOnScope StructScope(UserDefinedStruct);
    uint8* InstanceMemory = StructScope.GetStructMemory();

    /* Get Property Value and deserialize the values */
    void* PropertyValue = Property->ContainerPtrToValuePtr<void>(InstanceMemory);
    PropertySerializer->DeserializePropertyValue(Property, PropertyJsonValue.ToSharedRef(), PropertyValue);

    /* Get the default value as a string */
    FString DefaultValue;
#if ENGINE_UE5
    Property->ExportTextItem_Direct(DefaultValue, PropertyValue, nullptr, UserDefinedStruct, 0);
#else
    Property->ExportText_Direct(DefaultValue, PropertyValue, nullptr, UserDefinedStruct, 0);
#endif

    /* Update the variable */
    FStructureEditorUtils::ChangeVariableDefaultValue(UserDefinedStruct, Variable.VarGuid, DefaultValue);

    /* Editor Only Data */
    if (CookedStructMetaData.IsValid() && CookedStructMetaData->HasField(TEXT("StructMetaData"))) {
        TArray<TSharedPtr<FJsonValue>> PropertiesMetaData = CookedStructMetaData->GetObjectField(TEXT("StructMetaData"))->GetArrayField(TEXT("PropertiesMetaData"));
        
        for (TSharedPtr<FJsonValue> Value : PropertiesMetaData) {
            const TSharedPtr<FJsonObject> PropertiesMetadataJsonObject = Value->AsObject();

            /* Find a matching key */
            if (PropertiesMetadataJsonObject->GetStringField(TEXT("Key")) == Name) {
                TArray<TSharedPtr<FJsonValue>> FieldMetaData = PropertiesMetadataJsonObject->GetObjectField("Value")->GetArrayField("FieldMetaData");

                for (TSharedPtr<FJsonValue> FieldValue : FieldMetaData) {
                    const TSharedPtr<FJsonObject> FieldObject = FieldValue->AsObject();

                    FString MetadataKey = FieldObject->GetStringField(TEXT("Key"));
                    FString MetadataValue = FieldObject->GetStringField(TEXT("Value"));

                    Property->SetMetaData(FName(*MetadataKey), *MetadataValue);

                    if (MetadataKey == TEXT("Tooltip")) {
                        FStructureEditorUtils::ChangeVariableTooltip(UserDefinedStruct, Variable.VarGuid, MetadataValue);
                    }

                    if (MetadataKey == TEXT("DisplayName")) {
                        Variable.FriendlyName = MetadataValue;
                    }
                }
            }
        }
    }
}

FEdGraphPinType IUserDefinedStructImporter::ResolvePropertyPinType(const TSharedPtr<FJsonObject> &PropertyJsonObject) {
    const FString Type = PropertyJsonObject->GetStringField(TEXT("Type"));

    /* Special handling for containers */
    const EPinContainerType *ContainerType = ContainerTypeMap.Find(Type);
    
    if (ContainerType) {
        if (*ContainerType == EPinContainerType::Map) {
            TSharedPtr<FJsonObject> KeyPropObject = PropertyJsonObject->GetObjectField(TEXT("KeyProp"));
            
            FEdGraphPinType ResolvedType = ResolvePropertyPinType(KeyPropObject);
            ResolvedType.ContainerType = *ContainerType;

            TSharedPtr<FJsonObject> ValuePropObject = PropertyJsonObject->GetObjectField(TEXT("ValueProp"));
            FEdGraphPinType ResolvedTerminalType = ResolvePropertyPinType(ValuePropObject);
            
            ResolvedType.PinValueType.TerminalCategory = ResolvedTerminalType.PinCategory;
            ResolvedType.PinValueType.TerminalSubCategory = ResolvedTerminalType.PinSubCategory;
            ResolvedType.PinValueType.TerminalSubCategoryObject = ResolvedTerminalType.PinSubCategoryObject;

            return ResolvedType;
        }

        if (*ContainerType == EPinContainerType::Set) {
            TSharedPtr<FJsonObject> ElementPropObject = PropertyJsonObject->GetObjectField(TEXT("ElementProp"));
            FEdGraphPinType ResolvedType = ResolvePropertyPinType(ElementPropObject);
            
            ResolvedType.ContainerType = *ContainerType;
            
            return ResolvedType;
        }

        if (*ContainerType == EPinContainerType::Array) {
            TSharedPtr<FJsonObject> InnerTypeObject = PropertyJsonObject->GetObjectField(TEXT("Inner"));
            FEdGraphPinType ResolvedType = ResolvePropertyPinType(InnerTypeObject);
            
            ResolvedType.ContainerType = *ContainerType;
            
            return ResolvedType;
        }
    }

    FEdGraphPinType ResolvedType = FEdGraphPinType(NAME_None, NAME_None, nullptr, EPinContainerType::None,false, FEdGraphTerminalType());

    /* Find main type from our PropertyCategoryMap */
    const FName* TypeCategory = PropertyCategoryMap.Find(Type);
    
    if (TypeCategory) {
        ResolvedType.PinCategory = *TypeCategory;
    } else {
        UE_LOG(LogJson, Warning, TEXT("Type '%s' not found in PropertyCategoryMap, defaulting to 'Byte'"), *Type);
        ResolvedType.PinCategory = TEXT("byte");
    }

    /* Special handling for some types */
    if (Type == "DoubleProperty") {
        ResolvedType.PinSubCategory = TEXT("double");
    } else if (Type == "FloatProperty") {
        ResolvedType.PinSubCategory = TEXT("float");
    } else if (Type == "EnumProperty" || Type == "ByteProperty") {
        ResolvedType.PinSubCategoryObject = LoadObjectFromJsonReference(PropertyJsonObject, TEXT("Enum"));
    } else if (Type == "StructProperty") {
        ResolvedType.PinSubCategoryObject = LoadObjectFromJsonReference(PropertyJsonObject, TEXT("Struct"));
    } else if (Type == "ClassProperty" || Type == "SoftClassProperty") {
        ResolvedType.PinSubCategoryObject = LoadObjectFromJsonReference(PropertyJsonObject, TEXT("MetaClass"));
    } else if (Type == "ObjectProperty" || Type == "SoftObjectProperty") {
        ResolvedType.PinSubCategoryObject = LoadObjectFromJsonReference(PropertyJsonObject, TEXT("PropertyClass"));
    }

    return ResolvedType;
}

UObject* IUserDefinedStructImporter::LoadObjectFromJsonReference(const TSharedPtr<FJsonObject> &ParentJsonObject, const FString &ReferenceKey) {
    const TSharedPtr<FJsonObject> ReferenceObject = ParentJsonObject->GetObjectField(ReferenceKey);
    
    if (!ReferenceObject) {
        UE_LOG(LogJson, Error, TEXT("Failed to load Object from property %s: property not found"), *ReferenceKey);
        return nullptr;
    }

    TObjectPtr<UObject> LoadedObject;
    LoadObject<UObject>(&ReferenceObject, LoadedObject);
    return LoadedObject;
}
