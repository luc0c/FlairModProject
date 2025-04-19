﻿/* Copyright JsonAsAsset Contributors 2024-2025 */

#include "Importers/Types/Blueprint/AnimationBlueprintImporter.h"

#include "AnimationStateMachineGraph.h"
#include "AnimationStateMachineSchema.h"
#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_StateResult.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "Animation/AnimBlueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Importers/Types/Blueprint/Utilities/AnimationBlueprintUtilities.h"
#include "Importers/Types/Blueprint/Utilities/AnimNodeLayoutUtillties.h"
#include "Importers/Types/Blueprint/Utilities/StateMachineUtilities.h"

#if ENGINE_UE5
#include "UObject/UnrealTypePrivate.h"
#endif

extern bool GShowAnimationBlueprintImporterWarning = true;

bool IAnimationBlueprintImporter::Import() {
	if (GShowAnimationBlueprintImporterWarning) {
		SpawnPrompt("Preface Warning", "None of this is final, this is completely a work in progress with flaws. None of it is perfect. If you find a issue, fix it.\n\nTo remove this warning, go to AnimationBlueprintImporter.cpp and set GShowAnimationBlueprintImporterWarning to false.");
		GShowAnimationBlueprintImporterWarning = false;
	}
	
	AnimBlueprint = GetSelectedAsset<UAnimBlueprint>();
	if (!AnimBlueprint) return false;

	const TSharedPtr<FJsonObject> RootAnimNodeDefaults = GetExportStartingWith("Default__", "Name", AllJsonObjects);
	if (!RootAnimNodeDefaults.IsValid()) return false;
	
	RootAnimNodeProperties = RootAnimNodeDefaults->GetObjectField("Properties");
	if (!RootAnimNodeProperties.IsValid()) return false;

	/* Newer Unreal Engine versions use CopyRecords and SerializedSparseClassData */
	if (RootAnimNodeDefaults->HasField("SerializedSparseClassData")) {
		SerializedSparseClassData = RootAnimNodeDefaults->GetObjectField("SerializedSparseClassData");
	}

	/* Array of sync group names cached to use at later points of importing */
	if (AssetData->HasField("SyncGroupNames")) {
		for (const TSharedPtr<FJsonValue> SyncGroupNameValue : AssetData->GetArrayField("SyncGroupNames")) {
			SyncGroupNames.Add(SyncGroupNameValue->AsString());
		}
	}

	/* Filter AnimNodeProperties ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
	FilterAnimGraphNodeProperties(RootAnimNodeProperties);
	ProcessEvaluateGraphExposedInputs(RootAnimNodeProperties);

	/* Parse LinkIDs to proper Node IDs ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
	RootAnimNodeProperties->Values.GetKeys(NodesKeys);
	
	ReversedNodesKeys = NodesKeys;
	Algo::Reverse(ReversedNodesKeys);

	for (const FString& Key : NodesKeys) {
		TSharedPtr<FJsonValue> NodeValue = RootAnimNodeProperties->Values.FindChecked(Key);
		if (!NodeValue.IsValid()) continue;
		
		ReplaceLinkID(NodeValue, NodesKeys);
		RootAnimNodeProperties->Values[Key] = NodeValue;
	}

	/* Sets "State" and "Machine" for each state result */
	if (AssetData->HasField(TEXT("BakedStateMachines"))) {
		BakedStateMachines = AssetData->GetArrayField(TEXT("BakedStateMachines"));
    
		for (const TSharedPtr<FJsonValue>& MachineValue : BakedStateMachines) {
			const TSharedPtr<FJsonObject> MachineObject = MachineValue->AsObject();
			const TArray<TSharedPtr<FJsonValue>> States = MachineObject->GetArrayField(TEXT("States"));
			const FString MachineName = MachineObject->GetStringField(TEXT("MachineName"));
        
			/* Loop through each state */
			for (const TSharedPtr<FJsonValue>& StateValue : States) {
				const TSharedPtr<FJsonObject> StateObject = StateValue->AsObject();
				const int32 StateRootNodeIndex = StateObject->GetIntegerField(TEXT("StateRootNodeIndex"));
            
				if (StateRootNodeIndex == -1 || !ReversedNodesKeys.IsValidIndex(StateRootNodeIndex)) {
					continue;
				}
            
				const FString StartKey = ReversedNodesKeys[StateRootNodeIndex];
				HarvestAndTagConnectedStateMachineNodes(StartKey, StateObject->GetStringField(TEXT("StateName")), MachineName, RootAnimNodeProperties->Values);
			}
		}
	}

	/* Separate main graph nodes (without "State" and "Machine") into RootGraphAnimProperties ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
	const TSharedPtr<FJsonObject> RootGraphAnimProperties = MakeShared<FJsonObject>(); {
		for (const FString& Key : NodesKeys) {
			const TSharedPtr<FJsonValue> NodeValue = RootAnimNodeProperties->Values.FindChecked(Key);
		
			if (NodeValue->Type == EJson::Object) {
				const TSharedPtr<FJsonObject> NodeObject = NodeValue->AsObject();
			
				if (!NodeObject->HasField(TEXT("State")) && !NodeObject->HasField(TEXT("Machine"))) {
					RootGraphAnimProperties->SetObjectField(Key, NodeObject);
				}
			}
		}
	}

	UEdGraph* AnimGraph = FindAnimGraph(AnimBlueprint);
	
	if (AnimGraph) {
		AnimGraph->SubGraphs.Empty();
	}

	CreateGraph(RootGraphAnimProperties, AnimGraph, RootAnimNodeContainer);
	
	return true;
}

void IAnimationBlueprintImporter::CreateGraph(const TSharedPtr<FJsonObject>& AnimNodeProperties, UEdGraph* AnimGraph, FUObjectExportContainer& Container) {
	/* Remove all pre-existing nodes */
	if (AnimGraph) {
		for (UEdGraphNode* Node : AnimGraph->Nodes) {
			if (Node) {
				Node->BreakAllNodeLinks();
				Node->ConditionalBeginDestroy();
			}
		}
        
		AnimGraph->Nodes.Empty();
		AnimGraph->SubGraphs.Empty();
	}
	
	CreateAnimGraphNodes(AnimGraph, AnimNodeProperties, Container);
	AddNodesToGraph(AnimGraph, Container);

	HandleNodeDeserialization(Container);
	ConnectAnimGraphNodes(Container, AnimGraph);
	AutoLayoutAnimGraphNodes(Container.Exports);

	for (const FUObjectExport ExportNode : Container.Exports) {
		const TSharedPtr<FJsonObject> ExportJsonObject = ExportNode.JsonObject;
		
		if (UAnimGraphNode_StateMachine* StateMachine = Cast<UAnimGraphNode_StateMachine>(ExportNode.Object)) {
			UAnimationStateMachineGraph* EditorStateMachineGraph = CastChecked<UAnimationStateMachineGraph>(FBlueprintEditorUtils::CreateNewGraph(StateMachine, NAME_None, UAnimationStateMachineGraph::StaticClass(), UAnimationStateMachineSchema::StaticClass()));
			EditorStateMachineGraph->OwnerAnimGraphNode = StateMachine;

			const TSharedPtr<FJsonObject> StateMachineObject = BakedStateMachines[ExportJsonObject->GetIntegerField("StateMachineIndexInClass")]->AsObject();
					
			FString MachineName = StateMachineObject->GetStringField(TEXT("MachineName"));
			EditorStateMachineGraph->Rename(*MachineName);

			const UEdGraphSchema* Schema = EditorStateMachineGraph->GetSchema();
			Schema->CreateDefaultNodesForGraph(*EditorStateMachineGraph);

			UEdGraph* ParentGraph = StateMachine->GetGraph();
	
			if(ParentGraph->SubGraphs.Find(EditorStateMachineGraph) == INDEX_NONE) {
				ParentGraph->Modify();
				ParentGraph->SubGraphs.Add(EditorStateMachineGraph);
			}

			StateMachine->EditorStateMachineGraph = EditorStateMachineGraph;
			CreateStateMachineGraph(EditorStateMachineGraph, StateMachineObject, GetObjectSerializer(), RootAnimNodeContainer, ReversedNodesKeys, this, AnimBlueprint);

			/* Add nodes to graph */
			if (!StateMachineObject->HasField(TEXT("States"))) continue;

			TArray<TSharedPtr<FJsonValue>> States = StateMachineObject->GetArrayField(TEXT("States"));

			for (const TSharedPtr<FJsonValue>& StateValue : States) {
				const TSharedPtr<FJsonObject> StateObject = StateValue->AsObject();
				FString StateName = StateObject->GetStringField(TEXT("StateName"));

				UAnimationStateGraph* Graph = nullptr;

				for (UEdGraph* SubGraph : EditorStateMachineGraph->SubGraphs) {
					if (SubGraph->GetName() == StateName) {
						Graph = Cast<UAnimationStateGraph>(SubGraph);
					}
				}

				TSharedPtr<FJsonObject> StateMachineAnimNodeProperties = MakeShared<FJsonObject>();

				for (const auto& Pair : RootAnimNodeProperties->Values) {
					const  FString Key = Pair.Key;
					const TSharedPtr<FJsonObject> Value = Pair.Value->AsObject();

					if (!Value.IsValid()) continue;

					if (Value->HasField(TEXT("State")) && Value->HasField(TEXT("Machine"))) {
						const FString NodeStateName = Value->GetStringField(TEXT("State"));
						const FString NodeMachineName = Value->GetStringField(TEXT("Machine"));

						if (StateName == NodeStateName && NodeMachineName == MachineName) {
							StateMachineAnimNodeProperties->SetObjectField(Key, Value);
						}
					}
				}

				if (Graph) {
					FUObjectExportContainer StateMachineContainer;
					CreateGraph(StateMachineAnimNodeProperties, Graph, StateMachineContainer);

					if (Graph->MyResultNode) {
						Graph->MyResultNode->BreakAllNodeLinks();
						Graph->RemoveNode(Graph->MyResultNode);
						Graph->MyResultNode->ConditionalBeginDestroy();
						Graph->MyResultNode = nullptr;
					}

					for (const FUObjectExport& StateMachineExport : StateMachineContainer.Exports) {
						if (UAnimGraphNode_StateResult* StateResult = Cast<UAnimGraphNode_StateResult>(StateMachineExport.Object)) {
							Graph->MyResultNode = StateResult;
						}
					}
				}
			}
		}
	}
}

void inline LinkPoseInputPin(const FString& PinName, UAnimGraphNode_Base* Node, UAnimGraphNode_Base* TargetNode, UEdGraph* AnimGraph) {
	UEdGraphPin* InputPin = Node->FindPin(PinName, EGPD_Input);
	UEdGraphPin* OutputPin = GetFirstOutputPin(TargetNode);
	
	if (InputPin && OutputPin) {
		InputPin->MakeLinkTo(OutputPin);
		InputPin->DefaultValue.Reset();
		
		Node->Modify();
		TargetNode->Modify();
		AnimGraph->Modify();
	}
}

void IAnimationBlueprintImporter::UpdateBlendListByEnumVisibleEntries(FUObjectExport NodeExport, FUObjectExportContainer& Container, UEdGraph* AnimGraph) {
	TSharedPtr<FJsonObject> NodeJsonObject = NodeExport.JsonObject;
	UAnimGraphNode_BlendListByEnum* BlendListByEnum = Cast<UAnimGraphNode_BlendListByEnum>(NodeExport.Object);
	
    if (!BlendListByEnum || !NodeJsonObject) {
        return;
    }

	/* Get the BoundEnum set before in the BlendListByEnum node */
    const UEnum* BoundEnum = BlendListByEnum->GetEnum();
    if (!BoundEnum) {
        return;
    }

	TArray<TSharedPtr<FJsonValue>> EnumToPoseIndexArray = NodeJsonObject->GetArrayField(TEXT("EnumToPoseIndex"));

	/* Create VisibleEnumEntries array using the enum */
	TArray<FName> VisibleEnumEntries;

    for (int32 i = 0; i < EnumToPoseIndexArray.Num(); i++) {
        const int32 Value = static_cast<int32>(EnumToPoseIndexArray[i]->AsNumber());
        
        if (Value != 0) {
            if (BoundEnum->NumEnums() > i) {
                FString EnumEntryName = BoundEnum->GetNameByIndex(i).ToString();
                if (EnumEntryName.Contains("::")) {
                    EnumEntryName.Split("::", nullptr, &EnumEntryName);
                }

                VisibleEnumEntries.Add(FName(*EnumEntryName));
            }
        }
    }
	
    if (const FArrayProperty* VisEnumArrayProp = FindFProperty<FArrayProperty>(BlendListByEnum->GetClass(), TEXT("VisibleEnumEntries"))) {
        const void* ArrayPtr = VisEnumArrayProp->ContainerPtrToValuePtr<void>(BlendListByEnum);
        FScriptArrayHelper ArrayHelper(VisEnumArrayProp, ArrayPtr);
    	
        ArrayHelper.Resize(0);
    	
        const FNameProperty* NameProp = CastField<FNameProperty>(VisEnumArrayProp->Inner);
        if (!NameProp) {
            return;
        }
    	
        for (const FName& Entry : VisibleEnumEntries) {
            const int32 NewIdx = ArrayHelper.AddValue();
            void* ElemPtr = ArrayHelper.GetRawPtr(NewIdx);
        	
            NameProp->SetPropertyValue(ElemPtr, Entry);
        }
    }

	BlendListByEnum->ReconstructNode();

	TArray<TSharedPtr<FJsonValue>> BlendPoseArray = NodeJsonObject->GetArrayField(TEXT("BlendPose"));

	int BlendPoseIndex = 0;
	if (BlendPoseArray.IsValidIndex(0)) {
		FString LinkID = BlendPoseArray[0]->AsObject()->GetStringField(TEXT("LinkID"));
		const FString IndexedPinName = FString::Printf(TEXT("BlendPose_%d"), 0);

		FUObjectExport TargetNodeExport = Container.Find(LinkID);
		UAnimGraphNode_Base* TargetNode = Cast<UAnimGraphNode_Base>(TargetNodeExport.Object);

		LinkPoseInputPin(IndexedPinName, BlendListByEnum, TargetNode, AnimGraph);

		BlendPoseIndex++;
	}

	/* Map poses corresponding to the bound enum */
	for (int32 EnumIndex = 0; EnumIndex < BoundEnum->NumEnums() - 1; ++EnumIndex) {
		if (EnumIndex < EnumToPoseIndexArray.Num()) {
			const int PoseIndex = EnumToPoseIndexArray[EnumIndex]->AsNumber();

			if (PoseIndex == 0) continue;

			if (PoseIndex < BlendPoseArray.Num()) {
				FString LinkID = BlendPoseArray[PoseIndex]->AsObject()->GetStringField(TEXT("LinkID"));
                const FString IndexedPinName = FString::Printf(TEXT("BlendPose_%d"), BlendPoseIndex);

				FUObjectExport TargetNodeExport = Container.Find(LinkID);
				UAnimGraphNode_Base* TargetNode = Cast<UAnimGraphNode_Base>(TargetNodeExport.Object);

				LinkPoseInputPin(IndexedPinName, BlendListByEnum, TargetNode, AnimGraph);
			}
		}

		BlendPoseIndex++;
	}
}

void IAnimationBlueprintImporter::CreateAnimGraphNodes(UEdGraph* AnimGraph, const TSharedPtr<FJsonObject>& AnimNodeProperties, FUObjectExportContainer& OutContainer) {
	for (const auto& Pair : AnimNodeProperties->Values) {
		FString Key = Pair.Key;

		TSharedPtr<FJsonObject> Value = Pair.Value->AsObject();

		/* Find the NodeType and GUID from the key */
		FString NodeType, NodeStringGUID; {
			Key.Split(TEXT("_"), &NodeType, &NodeStringGUID, ESearchCase::IgnoreCase, ESearchDir::FromEnd);

			/* Handle case for format: "AnimGraphNode[0]" */
			if (Key.Contains("[")) {
				FString CleanKey = Key.Left(Key.Find("["));
				
				TArray<FString> Parts; {
					CleanKey.ParseIntoArray(Parts, TEXT("_"));
				}
				
				NodeType = Parts.Num() >= 2 ? Parts[0] + TEXT("_") + Parts[1] : CleanKey;
				NodeStringGUID.Empty();
			}
		}

		if (NodeType == "AnimGraphNode") {
			NodeType = Key;
		}

		/* Redirections */
		if (NodeType == "AnimGraphNode_SubInput") {
			NodeType = "AnimGraphNode_LinkedInputPose";
		}

		/* Only add json object data, transition result is handled different */
		if (NodeType == "AnimGraphNode_TransitionResult") {
			OutContainer.Exports.Add(
				FUObjectExport(
					FName(*Key),
					FName(*NodeType),
					FName(AnimGraph->GetName()),
					Value,
					nullptr,
					nullptr
				)
			);

			continue;
		}

		/* Parse the NodeGuid, if not parsed properly, generate a new one */
		FGuid NodeGuid; {
			FGuid::Parse(NodeStringGUID, NodeGuid);

			if (!NodeGuid.IsValid()) NodeGuid = FGuid();
		}

		const UClass* Class = FindObject<UClass>(ANY_PACKAGE, *NodeType);
		if (!Class) continue;

		UAnimGraphNode_Base* Node = NewObject<UAnimGraphNode_Base>(AnimGraph, Class, NAME_None, RF_Transactional);
		Node->NodeGuid = NodeGuid;

		/* Add new node */
		OutContainer.Exports.Add(
			FUObjectExport(
				FName(*Key),
				FName(*NodeType),
				FName(AnimGraph->GetName()),
				Value,
				Node,
				AnimGraph
			)
		);
	}
}

void IAnimationBlueprintImporter::AddNodesToGraph(UEdGraph* AnimGraph, FUObjectExportContainer& Container) {
    for (const FUObjectExport& Export : Container.Exports) {
        if (!IsValid(Export.Object) || !Export.JsonObject.IsValid())
            continue;

        UAnimGraphNode_Base* Node = Cast<UAnimGraphNode_Base>(Export.Object);

        Node->Rename(nullptr, AnimGraph);
        AnimGraph->Nodes.Add(Node);
        Node->Modify();
    }
}

void IAnimationBlueprintImporter::HandleNodeDeserialization(FUObjectExportContainer& Container) {
	GetObjectSerializer()->GetPropertySerializer()->BlacklistedPropertyNames.Add(TEXT("LinkID"));

	for (FUObjectExport NodeExport : Container.Exports) {
		if (NodeExport.Object == nullptr) continue;

		UAnimGraphNode_Base* Node = Cast<UAnimGraphNode_Base>(NodeExport.Object);
		TSharedPtr<FJsonObject> NodeProperties = NodeExport.JsonObject;

		/* Post-processing modifications ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
		if (NodeProperties->HasField(TEXT("GroupRole"))) {
			const int GroupIndexInteger = NodeProperties->GetIntegerField(TEXT("GroupIndex"));

			/* -1 is no group role */
			if (GroupIndexInteger != -1) {
				TSharedPtr<FJsonObject> SyncGroup = MakeShared<FJsonObject>();
				FString SyncGroupName = SyncGroupNames[GroupIndexInteger];
			
				SyncGroup->SetStringField("GroupName", SyncGroupName);
				SyncGroup->SetStringField("GroupRole", NodeProperties->GetStringField(TEXT("GroupRole")));

				NodeProperties->SetObjectField(TEXT("SyncGroup"), SyncGroup);
			}
		}
		
		GetObjectSerializer()->DeserializeObjectProperties(NodeProperties, Node);

		/* Specific needs for certain nodes ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
		if (UAnimGraphNode_SaveCachedPose* SaveCachedPose = Cast<UAnimGraphNode_SaveCachedPose>(Node)) {
			SaveCachedPose->CacheName = NodeProperties->GetStringField(TEXT("CachePoseName"));
		}

		if (UAnimGraphNode_UseCachedPose* UseCachedPose = Cast<UAnimGraphNode_UseCachedPose>(Node)) {
			if (NodeProperties->HasField(TEXT("LinkToCachingNode"))) {
				const TSharedPtr<FJsonObject> LinkToCachingNode = NodeProperties->GetObjectField("LinkToCachingNode");
				
				if (LinkToCachingNode->HasField("LinkID")) {
					const FString LinkID = LinkToCachingNode->GetStringField("LinkID");

					/* Specifically use RootAnimNodeContainer, because cached poses won't move with state machines */
					FUObjectExport SaveCachedPoseExport = RootAnimNodeContainer.Find(LinkID);
					if (!SaveCachedPoseExport.IsValid()) continue;

					UAnimGraphNode_SaveCachedPose* SaveCachedPose = Cast<UAnimGraphNode_SaveCachedPose>(SaveCachedPoseExport.Object);
					if (!SaveCachedPose) continue;
					
					UseCachedPose->SaveCachedPoseNode = SaveCachedPose;
					UseCachedPose->Modify();
					SaveCachedPose->Modify();
				}
			}
		}

		HandlePropertyBinding(NodeExport, AllJsonObjects, Node, this, AnimBlueprint);

		const UJsonAsAssetSettings* Settings = GetDefault<UJsonAsAssetSettings>();
		if (Settings->AssetSettings.AnimationBlueprintImportSettings.bShowAllNodeKeysAsComment) {
			Node->NodeComment = NodeExport.Name.ToString();
			Node->bCommentBubbleVisible = true;
		}
		
		Node->AllocateDefaultPins();
		Node->Modify();
	}
}

void IAnimationBlueprintImporter::ConnectAnimGraphNodes(FUObjectExportContainer& Container, UEdGraph* AnimGraph) {
    for (const FUObjectExport Export : Container.Exports) {
        UAnimGraphNode_Base* Node = Cast<UAnimGraphNode_Base>(Export.Object);
        const TSharedPtr<FJsonObject> Json = Export.JsonObject;

        if (UAnimGraphNode_BlendListByEnum* BlendNode = Cast<UAnimGraphNode_BlendListByEnum>(Node)) {
            UpdateBlendListByEnumVisibleEntries(Export, Container, AnimGraph);
        	continue;
        }
    	
        for (const auto& Pair : Json->Values) {
            const FString& Key = Pair.Key;
            const TSharedPtr<FJsonValue>& Value = Pair.Value;
            
            if (Value->Type == EJson::Array) {
                const TArray<TSharedPtr<FJsonValue>>& JsonArray = Value->AsArray();
                
                for (int32 Index = 0; Index < JsonArray.Num(); ++Index) {
                    const TSharedPtr<FJsonValue>& Elem = JsonArray[Index];
                    
                    if (!Elem.IsValid() || !Elem->AsObject().IsValid()) {
                        continue;
                    }
                    
                    const TSharedPtr<FJsonObject>& Obj = Elem->AsObject();
                    if (!Obj->HasField("LinkID")) {
                        continue;
                    }
                    
                    const FString LinkID = Obj->GetStringField("LinkID");
                    UAnimGraphNode_Base* TargetNode = Cast<UAnimGraphNode_Base>(Container.Find(LinkID).Object);
                    
                    if (!TargetNode) {
                        continue;
                    }
                    
                    const FStructProperty* NodeProp = GetNodeStructProperty(Node);
                    if (!NodeProp) {
                        continue;
                    }
                    
                    for (TFieldIterator<FProperty> It(NodeProp->Struct); It; ++It) {
                        FProperty* Property = *It;
                        
                        if (Property->GetName() != Pair.Key) {
                            continue;
                        }
                        
                        if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property)) {
                            const FStructProperty* InnerStruct = CastField<FStructProperty>(ArrayProp->Inner);
                            
                            if (!InnerStruct || !InnerStruct->Struct->IsChildOf(FPoseLinkBase::StaticStruct())) {
                                continue;
                            }
                            
                            const FString IndexedPinName = FString::Printf(TEXT("%s_%d"), *Pair.Key, Index);
                            LinkPoseInputPin(IndexedPinName, Node, TargetNode, AnimGraph);
                        }
                    }
                }
            }
            
            if (Value->Type == EJson::Object && Value->AsObject()->HasTypedField<EJson::String>("LinkID")) {
                const FString LinkID = Value->AsObject()->GetStringField("LinkID");
                UAnimGraphNode_Base* TargetNode = Cast<UAnimGraphNode_Base>(Container.Find(LinkID).Object);
                
                if (!TargetNode) {
                    continue;
                }
                
                const FStructProperty* NodeProp = GetNodeStructProperty(Node);

                if (!NodeProp) {
                    continue;
                }
                
                for (TFieldIterator<FProperty> It(NodeProp->Struct); It; ++It) {
                    const FProperty* Property = *It;
                    
                    if (Property->GetName() != Pair.Key) {
                        continue;
                    }
                    
                    LinkPoseInputPin(Key, Node, TargetNode, AnimGraph);
                }
            }
        }
    }
}

/* In newer versions of Unreal Engine, EvaluateGraphExposedInputs was moved to the main AnimBlueprintGeneratedClass class */
/* Here, we move them into the node data to use more easily */
void IAnimationBlueprintImporter::ProcessEvaluateGraphExposedInputs(const TSharedPtr<FJsonObject>& AnimNodeProperties) const {
	if (!AssetData->HasField("EvaluateGraphExposedInputs")) return;
	TArray<TSharedPtr<FJsonValue>> EvaluateInputs = AssetData->GetArrayField("EvaluateGraphExposedInputs");
	
	for (const TSharedPtr<FJsonValue> Value : EvaluateInputs) {
		TSharedPtr<FJsonObject> InputObj = Value->AsObject();
		
		FString NodeName = InputObj->GetObjectField("ValueHandlerNodeProperty")->GetStringField("ObjectName"); {
			NodeName.Split(":", nullptr, &NodeName);
			NodeName = NodeName.Replace(TEXT("'"), TEXT(""));	
		}
		
		AnimNodeProperties->GetObjectField(NodeName)->SetObjectField("EvaluateGraphExposedInputs", InputObj);
	}
}

UEdGraph* IAnimationBlueprintImporter::FindAnimGraph(UAnimBlueprint* AnimBlueprint) {
	for (UEdGraph* Graph : AnimBlueprint->FunctionGraphs) {
		if (Graph && Graph->GetName() == TEXT("AnimGraph")) {
			return Graph;
		}
	}
	
	return nullptr;
}