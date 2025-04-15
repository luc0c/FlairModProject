﻿/* Copyright JsonAsAsset Contributors 2024-2025 */

#pragma once

#include "AnimationBlueprintUtilities.h"
#include "AnimationStateGraph.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationStateMachineSchema.h"
#include "AnimationTransitionGraph.h"
#include "AnimGraphNode_TransitionResult.h"
#include "AnimStateConduitNode.h"
#include "AnimStateEntryNode.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "EdGraphUtilities.h"
#include "Animation/AnimNode_TransitionResult.h"
#include "Utilities/Serializers/ObjectUtilities.h"
#include "Utilities/Serializers/PropertyUtilities.h"

inline void AutoLayoutStateMachineGraph(UAnimationStateMachineGraph* StateMachineGraph)  {
    if (!StateMachineGraph) { 
        return; 
    }

    UAnimStateNodeBase* InitialState = nullptr; {
        if (StateMachineGraph->EntryNode) {
            const UEdGraphPin* EntryPin = StateMachineGraph->EntryNode->FindPin(TEXT("Entry"));
        	
            if (EntryPin && EntryPin->LinkedTo.Num() > 0) { 
                InitialState = Cast<UAnimStateNodeBase>(EntryPin->LinkedTo[0]->GetOwningNode()); 
            }
        }
    }
    
    if (!InitialState) {
        return;
    }
    
    TMap<UAnimStateNodeBase*, int32> StateLevels;
    TQueue<UAnimStateNodeBase*> NodesToProcess;
    
    StateLevels.Add(InitialState, 0);
    NodesToProcess.Enqueue(InitialState);
    
    TMap<UAnimStateNodeBase*, TArray<UAnimStateNodeBase*>> OutgoingTransitions;
	
    for (UEdGraphNode* Node : StateMachineGraph->Nodes) {
        const UAnimStateTransitionNode* TransitionNode = Cast<UAnimStateTransitionNode>(Node);
        if (!TransitionNode) { 
            continue; 
        }
        
        const UEdGraphPin* InPin = TransitionNode->GetInputPin();
        const UEdGraphPin* OutPin = TransitionNode->GetOutputPin();
    	
        if (InPin && OutPin && InPin->LinkedTo.Num() > 0 && OutPin->LinkedTo.Num() > 0) {
            UAnimStateNodeBase* FromState = Cast<UAnimStateNodeBase>(InPin->LinkedTo[0]->GetOwningNode());
            UAnimStateNodeBase* ToState = Cast<UAnimStateNodeBase>(OutPin->LinkedTo[0]->GetOwningNode());
        	
            if (FromState && ToState) { 
                OutgoingTransitions.FindOrAdd(FromState).AddUnique(ToState); 
            }
        }
    }
    
    while (!NodesToProcess.IsEmpty()) {
        UAnimStateNodeBase* Current;
        NodesToProcess.Dequeue(Current);
    	
        const int32 CurrentLevel = StateLevels.FindChecked(Current);
    	
        if (OutgoingTransitions.Contains(Current)) {
            for (UAnimStateNodeBase* NextState : OutgoingTransitions[Current]) {
                const int32 NextLevel = CurrentLevel + 1;
            	
                if (!StateLevels.Contains(NextState) || NextLevel < StateLevels[NextState]) {
                    StateLevels.Add(NextState, NextLevel);
                    NodesToProcess.Enqueue(NextState);
                }
            }
        }
    }
    
    TMap<int32, TArray<UAnimStateNodeBase*>> StatesByLevel;
    for (auto& Pair : StateLevels) {
        UAnimStateNodeBase* State = Pair.Key;
        int32 Level = Pair.Value;
    	
        StatesByLevel.FindOrAdd(Level).Add(State);
    }

    constexpr float HorizontalSpacing = 400.0f;
    constexpr float VerticalSpacing = 200.0f;
	
    for (auto& LevelGroup : StatesByLevel) {
        int32 Level = LevelGroup.Key;
        TArray<UAnimStateNodeBase*>& NodesInLevel = LevelGroup.Value;
    	
        NodesInLevel.Sort([](const UAnimStateNodeBase& A, const UAnimStateNodeBase& B) {
            return A.GetName() < B.GetName();
        });
    	
        const float X = Level * HorizontalSpacing;
        float StartY = -((NodesInLevel.Num() - 1) * VerticalSpacing) * 0.5f;
    	
        for (int32 i = 0; i < NodesInLevel.Num(); ++i) {
            UAnimStateNodeBase* Node = NodesInLevel[i];
            Node->NodePosX = X;
            Node->NodePosY = StartY + i * VerticalSpacing;
        }
    }
    
    for (UEdGraphNode* Node : StateMachineGraph->Nodes) {
        UAnimStateTransitionNode* TransitionNode = Cast<UAnimStateTransitionNode>(Node);
        if (!TransitionNode) { 
            continue; 
        }
        
        const UEdGraphPin* InputPin = TransitionNode->GetInputPin();
        const UEdGraphPin* OutputPin = TransitionNode->GetOutputPin();
    	
        if (InputPin && OutputPin && InputPin->LinkedTo.Num() > 0 && OutputPin->LinkedTo.Num() > 0) {
            UAnimStateNodeBase* FromState = Cast<UAnimStateNodeBase>(InputPin->LinkedTo[0]->GetOwningNode());
            UAnimStateNodeBase* ToState = Cast<UAnimStateNodeBase>(OutputPin->LinkedTo[0]->GetOwningNode());
        	
            if (FromState && ToState) {
                TransitionNode->NodePosX = (FromState->NodePosX + ToState->NodePosX) * 0.5f;
                TransitionNode->NodePosY = (FromState->NodePosY + ToState->NodePosY) * 0.5f;
            }
        }
    }
    
    StateMachineGraph->NotifyGraphChanged();
}

inline void CreateStateMachineGraph(
	UAnimationStateMachineGraph* StateMachineGraph,
	const TSharedPtr<FJsonObject>& StateMachineJsonObject,
	UObjectSerializer* ObjectSerializer,
	FUObjectExportContainer RootContainer,
	TArray<FString> ReversedNodesKeys
) {
	if (!StateMachineGraph || !StateMachineJsonObject.IsValid()) {
		return;
	}

	const UAnimationStateMachineSchema* Schema = Cast<UAnimationStateMachineSchema>(StateMachineGraph->GetSchema());
	StateMachineGraph->Modify();

	/* Creating States ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
	const TArray<TSharedPtr<FJsonValue>>& States = StateMachineJsonObject->GetArrayField("States");
	
	/* Store state nodes in a container */
	FUObjectExportContainer Container;
    
	/* Create each state */
	for (int32 i = 0; i < States.Num(); ++i) {
		const TSharedPtr<FJsonObject> StateObject = States[i]->AsObject();
		const FString StateName = StateObject->GetStringField("StateName");
		const bool bIsAConduit = StateObject->GetBoolField("bIsAConduit");

		UAnimStateNodeBase* Node;
		UEdGraph* BoundGraph = nullptr;

		if (bIsAConduit) {
			UAnimStateConduitNode* ConduitNode = FEdGraphSchemaAction_NewStateNode::SpawnNodeFromTemplate<UAnimStateConduitNode>(
				StateMachineGraph,
				NewObject<UAnimStateConduitNode>(StateMachineGraph, UAnimStateConduitNode::StaticClass(), *StateName, RF_Transactional)
			);

			BoundGraph = ConduitNode->BoundGraph;
					        
			if (!BoundGraph) {
				BoundGraph = NewObject<UAnimationTransitionGraph>(ConduitNode, UAnimationTransitionGraph::StaticClass(), *StateName, RF_Transactional);
				ConduitNode->BoundGraph = BoundGraph;
			}

			Node = ConduitNode;
		} else {
			UAnimStateNode* StateNode = FEdGraphSchemaAction_NewStateNode::SpawnNodeFromTemplate<UAnimStateNode>(
				StateMachineGraph,
				NewObject<UAnimStateNode>(StateMachineGraph, UAnimStateNode::StaticClass(), *StateName, RF_Transactional)
			);
			
			BoundGraph = StateNode->BoundGraph;
		    
			if (!BoundGraph) {
				BoundGraph = NewObject<UAnimationStateGraph>(StateNode, UAnimationStateGraph::StaticClass(), *StateName, RF_Transactional);
				StateNode->BoundGraph = BoundGraph;
			}

			Node = StateNode;
		}

		FEdGraphUtilities::RenameGraphToNameOrCloseToName(BoundGraph, *StateName);

		Container.Exports.Add(
			FUObjectExport(
				FName(*StateName),
				NAME_None,
				NAME_None,
				StateObject,
				Node,
				nullptr
			)
		);
	}

	/* Creating Transitions ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
	const TArray<TSharedPtr<FJsonValue>>& Transitions = StateMachineJsonObject->GetArrayField("Transitions");
	
	for (int32 TransitionIndex = 0; TransitionIndex < Transitions.Num(); ++TransitionIndex) {
	    const TSharedPtr<FJsonObject>& TransitionObject = Transitions[TransitionIndex]->AsObject();

		/* Get State Nodes from Container */
	    const int32 PreviousStateIndex = TransitionObject->GetIntegerField("PreviousState");
	    const int32 NextStateIndex = TransitionObject->GetIntegerField("NextState");

		const FUObjectExport PreviousStateExport = Container.Exports[PreviousStateIndex];
		const FUObjectExport NextStateExport = Container.Exports[NextStateIndex];
		if (!PreviousStateExport.Object || !NextStateExport.Object) continue;

		TSharedPtr<FJsonObject> PreviousStateObject = PreviousStateExport.JsonObject;
		
		UAnimStateNodeBase* const FromNode = Cast<UAnimStateNodeBase>(PreviousStateExport.Object);
		UAnimStateNodeBase* const ToNode = Cast<UAnimStateNodeBase>(NextStateExport.Object);

		/* State Nodes must exist */
		if (!FromNode || !ToNode) continue;

        UAnimStateTransitionNode* const TransitionNode = NewObject<UAnimStateTransitionNode>(StateMachineGraph);
        TransitionNode->SetFlags(RF_Transactional);
        TransitionNode->CreateNewGuid();
		
		/* Automatically creates the Transition Graph */
        TransitionNode->PostPlacedNewNode();

		/* Create default pins and add to state machine */
        TransitionNode->AllocateDefaultPins();
        StateMachineGraph->Nodes.Add(TransitionNode);

		/* Find the Transition Graph from the transition node */
        UAnimationTransitionGraph* AnimationTransitionGraph = Cast<UAnimationTransitionGraph>(TransitionNode->BoundGraph);
        if (!AnimationTransitionGraph || !AnimationTransitionGraph->MyResultNode) {
            continue;
        }

		UAnimGraphNode_TransitionResult* TransitionResult = AnimationTransitionGraph->MyResultNode;
        FAnimNode_TransitionResult& ResultStruct = TransitionResult->Node;

        ResultStruct.bCanEnterTransition = TransitionObject->GetBoolField(TEXT("bDesiredTransitionReturnValue"));

		/* Set TransitionNode data */
		TSharedPtr<FJsonObject> TransitionStateObject;

		if (PreviousStateObject->HasField("Transitions")) {
			const TArray<TSharedPtr<FJsonValue>> StateTransitions = PreviousStateObject->GetArrayField("Transitions");

			for (const TSharedPtr<FJsonValue>& StateTransValue : StateTransitions) {
				const TSharedPtr<FJsonObject> StateTransitionObject = StateTransValue->AsObject();
				
				if (StateTransitionObject.IsValid()) {
					const int32 StateTransitionIndex = StateTransitionObject->GetIntegerField("TransitionIndex");
                	
					if (TransitionIndex == StateTransitionIndex) {
						TransitionStateObject = StateTransitionObject;
						break;
					}
				}
			}
		}

		TransitionNode->bAutomaticRuleBasedOnSequencePlayerInState = TransitionStateObject->GetBoolField(TEXT("bAutomaticRemainingTimeRule"));
		ObjectSerializer->DeserializeObjectProperties(TransitionObject, TransitionNode);

		/* CanTakeDelegateIndex is the index in nodes where the transition result node is */
		const int CanTakeDelegateIndex = TransitionStateObject->GetIntegerField(TEXT("CanTakeDelegateIndex"));
		
		if (CanTakeDelegateIndex != -1 &&
			/* Hide if it's already automatically transitioning */
			!TransitionNode->bAutomaticRuleBasedOnSequencePlayerInState
		) {
			FString DelegateExportName = ReversedNodesKeys[CanTakeDelegateIndex];
			
			/* Use if needed */
			/* const FUObjectExport DelegateExport = RootContainer.Find(DelegateExportName); */

			TransitionResult->NodeComment = DelegateExportName;
			TransitionResult->bCommentBubbleVisible = true;
		}
		
		/* Connect State Nodes together using the Transition Node */
        if (UEdGraphPin* const FromOutput = FindOutputPin(FromNode)) {
            if (UEdGraphPin* const TransitionIn = TransitionNode->GetInputPin()) {
                FromOutput->MakeLinkTo(TransitionIn);
            }
        }
        if (UEdGraphPin* const TransitionOut = TransitionNode->GetOutputPin()) {
            if (UEdGraphPin* const ToInput = FindInputPin(ToNode)) {
                TransitionOut->MakeLinkTo(ToInput);
            }
	    }
	}

	/* Connecting Entry Node to Initial State ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
	if (!StateMachineGraph->EntryNode) return;

	/* What state is this state machine plugged into first */
	const int InitialState = StateMachineJsonObject->GetIntegerField("InitialState");
	if (!States.IsValidIndex(InitialState)) return;

	const TSharedPtr<FJsonObject> InitialStateObject = States[InitialState]->AsObject();
	const FString InitialStateName = InitialStateObject->GetStringField("StateName");

	/* Find Initial State using the StateNodesMap */
	UAnimStateNodeBase* InitialStateNode = Cast<UAnimStateNodeBase>(Container.Find(InitialStateName).Object);
	if (!InitialStateNode) return;

	/* Find the EntryNode's Output Pin */
	UEdGraphPin* EntryOutputPin = StateMachineGraph->EntryNode->FindPin(TEXT("Entry"));

	/* Connect Entry Node to Initial State */
	if (InitialStateNode) {
		InitialStateNode->AllocateDefaultPins();

		/* Find Input Pin of State Node */
		UEdGraphPin* InitialInputPin = nullptr;
		for (UEdGraphPin* Pin : InitialStateNode->Pins) {
			if(Pin && Pin->Direction == EGPD_Input) {
				InitialInputPin = Pin;
				break;
			}
		}

		if (EntryOutputPin && InitialInputPin) {
			if (Schema) {
				Schema->TryCreateConnection(EntryOutputPin, InitialInputPin);
				StateMachineGraph->NotifyGraphChanged();
			}
		}
	}

	AutoLayoutStateMachineGraph(StateMachineGraph);
}
