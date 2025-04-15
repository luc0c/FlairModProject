﻿/* Copyright JsonAsAsset Contributors 2024-2025 */

#pragma once

#include "Importers/Constructor/Importer.h"

class UAnimGraphNode_Base;

class IAnimationBlueprintImporter final : public IImporter {
public:
	IAnimationBlueprintImporter(const FString& FileName, const FString& FilePath, const TSharedPtr<FJsonObject>& JsonObject, UPackage* Package, UPackage* OutermostPkg, const TArray<TSharedPtr<FJsonValue>>& AllJsonObjects, UClass* AssetClass):
		IImporter(FileName, FilePath, JsonObject, Package, OutermostPkg, AllJsonObjects, AssetClass) {
	}

	virtual bool Import() override;

private:
	void ProcessEvaluateGraphExposedInputs(const TSharedPtr<FJsonObject>& AnimNodeProperties) const;

	/* Finds an Animation Graph in an Animation Blueprint */
	static UEdGraph* FindAnimGraph(UAnimBlueprint* AnimBlueprint);

	/* Using [AnimNodeProperties] that is filled with animation nodes, create new nodes, and the Outer being [AnimGraph], then add it to the [Container] */
	void CreateGraph(const TSharedPtr<FJsonObject>& AnimNodeProperties, UEdGraph* AnimGraph, FUObjectExportContainer& Container);

	/* Create Animation Graph Nodes and create a UObjectExportContainer to hold the data */
	static void CreateAnimGraphNodes(UEdGraph* AnimGraph, const TSharedPtr<FJsonObject>& AnimNodeProperties, FUObjectExportContainer& OutContainer);

	/* Add a container full of nodes to a graph */
	static void AddNodesToGraph(UEdGraph* AnimGraph, FUObjectExportContainer& Container);

	/* Deserializes each node in the node container */
	void HandleNodeDeserialization(FUObjectExportContainer& Container);

	/* Links Animation Graph Nodes together using a container */
	static void ConnectAnimGraphNodes(FUObjectExportContainer& Container, UEdGraph* AnimGraph);

	/* Links two nodes together using a PinName */
	static void LinkPoseInputPin(const FString& PinName, UAnimGraphNode_Base* Node, UAnimGraphNode_Base* TargetNode, UEdGraph* AnimGraph);

protected:
	/* Global Cached data to reuse */
	TArray<FString> NodesKeys;
	TArray<FString> ReversedNodesKeys;
	
	TArray<TSharedPtr<FJsonValue>> BakedStateMachines;
	
	TSharedPtr<FJsonObject> RootAnimNodeProperties;
	FUObjectExportContainer RootAnimNodeContainer;

	/* UE5 Copy Record Cache Data */
	TSharedPtr<FJsonObject> SerializedSparseClassData;
};

REGISTER_IMPORTER(IAnimationBlueprintImporter, (TArray<FString>{ 
	TEXT("AnimBlueprintGeneratedClass")
}), TEXT("Blueprint Assets"));