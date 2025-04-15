﻿/* Copyright JsonAsAsset Contributors 2024-2025 */

#pragma once

#include "AnimGraphNode_Base.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

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