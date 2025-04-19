﻿/* Copyright JsonAsAsset Contributors 2024-2025 */

#pragma once

#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_BlendListByEnum.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Utilities/Serializers/ObjectUtilities.h"

inline FStructProperty* GetNodeStructProperty(const UAnimGraphNode_Base* Node) {
	if (!Node) return nullptr;

	for (TFieldIterator<FStructProperty> It(Node->GetClass()); It; ++It) {
		if (FStructProperty* StructProp = *It; StructProp->Struct && StructProp->Struct->IsChildOf(FAnimNode_Base::StaticStruct())) {
			return StructProp;
		}
	}

	return nullptr;
}

inline UEdGraphPin* GetFirstOutputPin(UAnimGraphNode_Base* Node) {
	if (!Node) return nullptr;

	for (UEdGraphPin* Pin : Node->Pins) {
		if (Pin && Pin->Direction == EGPD_Output) {
			return Pin;
		}
	}

	return nullptr;
}

inline UEdGraphPin* FindOutputPin(UEdGraphNode* Node) {
	for (UEdGraphPin* Pin : Node->Pins) {
		if (Pin->Direction == EGPD_Output) {
			return Pin;
		}
	}
	return nullptr;
}

inline UEdGraphPin* FindInputPin(UEdGraphNode* Node) {
	for (UEdGraphPin* Pin : Node->Pins) {
		if (Pin->Direction == EGPD_Input) {
			return Pin;
		}
	}
	return nullptr;
}

inline void ReplaceLinkID(const TSharedPtr<FJsonValue>& Data, const TArray<FString>& NodesKeys, const FString& PinName = TEXT("")) {
	if (!Data.IsValid()) return;
	
	if (Data->Type == EJson::Object) {
		const TSharedPtr<FJsonObject> Obj = Data->AsObject();
		for (auto& Pair : Obj->Values) {
			if (Pair.Key == TEXT("LinkID")) {
				const double d = Pair.Value->AsNumber();
				const int32 Index = static_cast<int32>(d);

				if (Index == -1) continue;
				
				if (Index < NodesKeys.Num()) {
					Pair.Value = MakeShared<FJsonValueString>(NodesKeys[Index]);
				}
			} else {
				ReplaceLinkID(Pair.Value, NodesKeys, Pair.Key);
			}
		}
	} else if (Data->Type == EJson::Array) {
		TArray<TSharedPtr<FJsonValue>> Array = Data->AsArray();
		
		for (int32 i = 0; i < Array.Num(); i++) {
			ReplaceLinkID(Array[i], NodesKeys, PinName);
		}
	}
}

inline void FilterAnimGraphNodeProperties(const TSharedPtr<FJsonObject>& JsonObject) {
	if (!JsonObject.IsValid()) return;
	
	TArray<FString> KeysToRemove;
	for (auto& Pair : JsonObject->Values) {
		if (!Pair.Key.Contains(TEXT("AnimGraphNode"))) {
			KeysToRemove.Add(Pair.Key);
		}
	}
	
	for (const FString& Key : KeysToRemove) {
		JsonObject->Values.Remove(Key);
	}
}

inline TArray<TPair<FString, FString>> FindLinkIDs(const TSharedPtr<FJsonValue>& Data, const FString& ParentProperty = TEXT("")) {
    TArray<TPair<FString, FString>> Results;
    if (!Data.IsValid()) return Results;
	
    if (Data->Type == EJson::Object) {
        const TSharedPtr<FJsonObject> Object = Data->AsObject();
    	
        for (auto& Pair : Object->Values) {
            if (Pair.Key == TEXT("LinkID")) {
                FString LinkStr = Pair.Value->AsString();
                FString Prop = ParentProperty.IsEmpty() ? Pair.Key : ParentProperty;
            	
                Results.Add(TPair<FString, FString>(Prop, LinkStr));
            	continue;
            }
        	
            TArray<TPair<FString, FString>> SubResults = FindLinkIDs(Pair.Value, Pair.Key);
            Results.Append(SubResults);
        }
    } else if (Data->Type == EJson::Array) {
        TArray<TSharedPtr<FJsonValue>> Arr = Data->AsArray();
        
        for (const TSharedPtr<FJsonValue>& Item : Arr) {
            TArray<TPair<FString, FString>> SubResults = FindLinkIDs(Item, ParentProperty);
        	
            Results.Append(SubResults);
        }
    }
	
    return Results;
}

inline void HarvestAndTagConnectedStateMachineNodes(const FString& StartKey, const FString& StateName, const FString& MachineName, TMap<FString, TSharedPtr<FJsonValue>>& Nodes) {
	if (!Nodes.Contains(StartKey)) return;

	const TSharedPtr<FJsonValue> NodeValue = Nodes.FindChecked(StartKey);
	
	if (NodeValue->Type == EJson::Object) {
		TSharedPtr<FJsonObject> NodeObject = NodeValue->AsObject();
		NodeObject->SetStringField(TEXT("State"), StateName);
		NodeObject->SetStringField(TEXT("Machine"), MachineName);
		
		Nodes[StartKey] = MakeShared<FJsonValueObject>(NodeObject);
	}
	
	TArray<TPair<FString, FString>> Links = FindLinkIDs(NodeValue, StartKey);
	
	for (const TPair<FString, FString>& LinkPair : Links) {
		FString NextKey = LinkPair.Value;

		/* AnimGraphNode_SaveCachedPose shouldn't be in a State Machine */
		if (NextKey.Contains("AnimGraphNode_SaveCachedPose")) {
			return;
		}
		
		HarvestAndTagConnectedStateMachineNodes(NextKey, StateName, MachineName, Nodes);
	}
}

inline void HandlePropertyBinding(FUObjectExport NodeExport, const TArray<TSharedPtr<FJsonValue>>& AllJsonObjects, UAnimGraphNode_Base* Node, IImporter* Importer, UAnimBlueprint* AnimBlueprint) {
	const TSharedPtr<FJsonObject> NodeProperties = NodeExport.JsonObject;
	
	/* Let the user know that this node has nodes plugged into it */
	if (NodeProperties->HasField("EvaluateGraphExposedInputs")) {
		const TSharedPtr<FJsonObject> EvaluateGraphExposedInputs = NodeProperties->GetObjectField("EvaluateGraphExposedInputs");

		bool bBoundFunction = EvaluateGraphExposedInputs->GetStringField("BoundFunction") != "None";
		
		if (EvaluateGraphExposedInputs->HasField("CopyRecords") || bBoundFunction) {
			const TArray<TSharedPtr<FJsonValue>> CopyRecords = EvaluateGraphExposedInputs->GetArrayField("CopyRecords");

			if (CopyRecords.Num() > 0) {
				for (const TSharedPtr<FJsonValue> CopyRecordAsValue : CopyRecords) {
					const TSharedPtr<FJsonObject> CopyRecordAsObject = CopyRecordAsValue->AsObject();

					if (!CopyRecordAsObject->HasField("DestProperty")) continue;
					if (CopyRecordAsObject->HasField("BoundFunction") && CopyRecordAsObject->GetStringField("BoundFunction") != TEXT("None")) {
						bBoundFunction = true;

						continue;
					}

					FString SourcePropertyName = CopyRecordAsObject->GetStringField("SourcePropertyName");

					/*
					 * Take the property's name from the object name:
					 *
					 * FloatProperty'AnimNode_SkeletalControlBase:Alpha' ->
					 * :Alpha' ->
					 * Alpha
					 */
					const TSharedPtr<FJsonObject> DestProperty = CopyRecordAsObject->GetObjectField("DestProperty");
					FString PinName = DestProperty->GetStringField(TEXT("ObjectName")); {
						PinName.Split(TEXT(":"), nullptr, &PinName);
						PinName = PinName.Replace(TEXT("'"), TEXT(""));
					}

					FString PinCategory = DestProperty->GetStringField(TEXT("ObjectName")); {
						PinCategory.Split(TEXT("'"), &PinCategory, nullptr);
						PinCategory.Split(TEXT("Property"), &PinCategory, nullptr);
						PinCategory = PinCategory.ToLower();
					}

					FName PinNameAsName(PinName);

					UClass* AnimClass = AnimBlueprint->GeneratedClass;

					/* Setup Property Binding */
					FAnimGraphNodePropertyBinding PropertyBinding;
					PropertyBinding.PropertyName = PinNameAsName;
					PropertyBinding.PathAsText = FText::FromString(SourcePropertyName);
					PropertyBinding.PinType.PinCategory = FName(PinCategory);
					PropertyBinding.bIsBound = true;
					PropertyBinding.PropertyPath.Append({ SourcePropertyName });

					TSharedPtr<FJsonObject> SourcePropertyObject = GetExportMatchingWith(SourcePropertyName, "Name", AllJsonObjects);
					if (PinCategory == "struct" && SourcePropertyObject.IsValid() && SourcePropertyObject->HasField("Struct")) {
						TSharedPtr<FJsonObject> StructObject = SourcePropertyObject->GetObjectField("Struct");

						TObjectPtr<UObject> LoadedObject;
						Importer->LoadObject<UObject>(&StructObject, LoadedObject);

						PropertyBinding.PinType.PinSubCategoryObject = LoadedObject.Get();
					} else {
						FProperty* Prop = AnimClass->FindPropertyByName(*SourcePropertyName);
						
						if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
						{
							UScriptStruct* ScriptStruct = StructProp->Struct;
							PropertyBinding.PinType.PinSubCategoryObject = ScriptStruct;
						}
					}

					if (CopyRecordAsObject->HasField("SourceSubPropertyName") && CopyRecordAsObject->GetStringField("SourceSubPropertyName") != "None") {
						FString SourceSubPropertyName = CopyRecordAsObject->GetStringField("SourceSubPropertyName");
						PropertyBinding.PathAsText = FText::FromString(SourcePropertyName + "." + SourceSubPropertyName);

						PropertyBinding.PropertyPath.Append({ SourceSubPropertyName });

						if (CopyRecordAsObject->GetObjectField("CachedSourceStructSubProperty")) {
							TSharedPtr<FJsonObject> StructObject = CopyRecordAsObject->GetObjectField("CachedSourceStructSubProperty");
							
							TObjectPtr<UObject> LoadedObject;
							Importer->LoadObject<UObject>(&StructObject, LoadedObject);

							auto StructProperty = LoadStructProperty(StructObject);

							if (StructProperty) {
								PropertyBinding.PinType.PinSubCategoryObject = StructProperty->Struct;
							}
						}
					}
					
					Node->PropertyBindings.Add(PinNameAsName, PropertyBinding);

					if (PinName == "ActiveEnumValue" && Node != nullptr) {
						if (UAnimGraphNode_BlendListByEnum* BlendListByEnum = Cast<UAnimGraphNode_BlendListByEnum>(Node)) {
							FProperty* Prop = AnimClass->FindPropertyByName(*SourcePropertyName);

							UEnum* EnumRef = nullptr;
							
							if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop)) {
								EnumRef = EnumProp->GetEnum();
							} else if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop)) {
								EnumRef = ByteProp->Enum;
							}

							if (EnumRef) {
								FProperty* BoundEnumProp = BlendListByEnum->GetClass()->FindPropertyByName(TEXT("BoundEnum"));
								
								if (BoundEnumProp) {
									if (FObjectProperty* ObjectProp = CastField<FObjectProperty>(BoundEnumProp)) {
										ObjectProp->SetObjectPropertyValue_InContainer(BlendListByEnum, EnumRef);
									}
								}
							}
						}
					}
				}
			}

			Node->NodeComment = NodeExport.Name.ToString();
			Node->bCommentBubbleVisible = bBoundFunction;
		}
	}
}