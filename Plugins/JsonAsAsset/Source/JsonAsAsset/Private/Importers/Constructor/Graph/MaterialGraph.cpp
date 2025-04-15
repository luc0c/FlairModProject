/* Copyright JsonAsAsset Contributors 2024-2025 */

#include "Importers/Constructor/Graph/MaterialGraph.h"

/* Expressions */
#include "Factories/MaterialFunctionFactoryNew.h"
#include "Materials/MaterialExpressionComment.h"
#include "Materials/MaterialExpressionFeatureLevelSwitch.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionShadingPathSwitch.h"
#include "Materials/MaterialExpressionQualitySwitch.h"
#include "Materials/MaterialExpressionReroute.h"

#if ENGINE_UE5
#include "Materials/MaterialExpressionTextureBase.h"
#endif

TSharedPtr<FJsonObject> IMaterialGraph::FindMaterialData(UObject* Parent, const FString& Type, const FString& Outer, FMaterialExpressionNodeExportContainer& Container) {
	TSharedPtr<FJsonObject> EditorOnlyData;

	/* Filter array if needed */
	for (const TSharedPtr<FJsonValue> Value : AllJsonObjects) {
		TSharedPtr<FJsonObject> Object = TSharedPtr<FJsonObject>(Value->AsObject());

		FString ExportType = Object->GetStringField(TEXT("Type"));
		FName ExportName(Object->GetStringField(TEXT("Name")));

		/* If an editor only data object is found, just set it */
		if (ExportType == Type + "EditorOnlyData") {
			EditorOnlyData = Object;
			continue;
		}

		/* For older versions, the "editor" data is in the main UMaterial/UMaterialFunction export */
		if (ExportType == Type) {
			EditorOnlyData = Object;
			continue;
		}

		/* Add to the list of expressions */
		Container.Expressions.Add(FMaterialExpressionNodeExport(
			ExportName,
			FName(ExportType),
			FName(Outer),
			Object,
			Parent
		));
	}

	return EditorOnlyData->GetObjectField(TEXT("Properties"));
}

void IMaterialGraph::ConstructExpressions(FMaterialExpressionNodeExportContainer& Container) {
	/* Go through each expression, and create the expression */
	for (FMaterialExpressionNodeExport& Export : Container.Expressions) {
		/* Invalid Json Object */
		if (!Export.JsonObject.IsValid()) {
			continue;
		}

		UMaterialExpression* Expression = Export.Expression;

		if (Expression == nullptr) {
			Expression = CreateEmptyExpression(Export, Container);
		}

		/* If nullptr, expression isn't valid */
		if (Expression == nullptr) {
			continue;
		}

		Export.Expression = Expression;
	}
}

void IMaterialGraph::PropagateExpressions(FMaterialExpressionNodeExportContainer& Container) {
	for (FMaterialExpressionNodeExport Export : Container.Expressions) {
		/* Get variables from the export data */
		FName Type = Export.Type;
		
		UObject* Parent = Export.Parent;

		/* Get Json Objects from Export */
		TSharedPtr<FJsonObject> ExportJsonObject = Export.JsonObject;
		TSharedPtr<FJsonObject> Properties = Export.GetProperties();

		/* Created Expression */
		UMaterialExpression* Expression = Export.Expression;

		/* Skip null expressions */
		if (Expression == nullptr) {
			continue;
		}
		
		bool bAddToParentExpression = true;
		
		/* Sub-graph (natively only on Unreal Engine 5) */
		if (Properties->HasField(TEXT("SubgraphExpression"))) {
			TSharedPtr<FJsonObject> SubGraphExpressionObject = Properties->GetObjectField(TEXT("SubgraphExpression"));

			FName SubGraphExpressionName = GetExportNameOfSubobject(SubGraphExpressionObject->GetStringField(TEXT("ObjectName")));

			FMaterialExpressionNodeExport SubGraphExport = Container.Find(SubGraphExpressionName);
			UMaterialExpression* SubGraphExpression = SubGraphExport.Expression;

#if ENGINE_UE5
			/* SubgraphExpression is only on Unreal Engine 5 */
			Expression->SubgraphExpression = SubGraphExpression;
#else
			/* Add it to the subgraph function ~ UE4 ONLY */
			UMaterialFunction* ParentSubgraphFunction = SubgraphFunctions[SubGraphExpressionName];

			Export.Parent = ParentSubgraphFunction;
			Expression = CreateEmptyExpression(Export, Container);

			Expression->Function = ParentSubgraphFunction;
			ParentSubgraphFunction->FunctionExpressions.Add(Expression);

			bAddToParentExpression = false;

			continue;
#endif
		}

		/* Sets 99% of properties for nodes */
		GetObjectSerializer()->DeserializeObjectProperties(Properties, Expression);

		/* Material Nodes with edited properties (ex: 9 objects with the same name ---> array of objects) */
		if (Type == "MaterialExpressionQualitySwitch") {
			UMaterialExpressionQualitySwitch* QualitySwitch = Cast<UMaterialExpressionQualitySwitch>(Expression);

			const TArray<TSharedPtr<FJsonValue>>* InputsPtr;
			
			if (ExportJsonObject->TryGetArrayField(TEXT("Inputs"), InputsPtr)) {
				int i = 0;
				for (const TSharedPtr<FJsonValue> InputValue : *InputsPtr) {
					FJsonObject* InputObject = InputValue->AsObject().Get();
					FName InputExpressionName = GetExpressionName(InputObject);
					
					if (Container.Contains(InputExpressionName)) {
						FExpressionInput Input = PopulateExpressionInput(InputObject, Container.GetExpressionByName(InputExpressionName));
						QualitySwitch->Inputs[i] = Input;
					}
					i++;
				}
			}
		} else if (Type == "MaterialExpressionShadingPathSwitch") {
			UMaterialExpressionShadingPathSwitch* ShadingPathSwitch = Cast<UMaterialExpressionShadingPathSwitch>(Expression);

			const TArray<TSharedPtr<FJsonValue>>* InputsPtr;
			
			if (ExportJsonObject->TryGetArrayField(TEXT("Inputs"), InputsPtr)) {
				int i = 0;
				for (const TSharedPtr<FJsonValue> InputValue : *InputsPtr) {
					FJsonObject* InputObject = InputValue->AsObject().Get();
					FName InputExpressionName = GetExpressionName(InputObject);
					
					if (Container.Contains(InputExpressionName)) {
						FExpressionInput Input = PopulateExpressionInput(InputObject, Container.GetExpressionByName(InputExpressionName));
						ShadingPathSwitch->Inputs[i] = Input;
					}
					i++;
				}
			}
		} else if (Type == "MaterialExpressionFeatureLevelSwitch") {
			UMaterialExpressionFeatureLevelSwitch* FeatureLevelSwitch = Cast<UMaterialExpressionFeatureLevelSwitch>(Expression);

			const TArray<TSharedPtr<FJsonValue>>* InputsPtr;
			
			if (ExportJsonObject->TryGetArrayField(TEXT("Inputs"), InputsPtr)) {
				int i = 0;
				for (const TSharedPtr<FJsonValue> InputValue : *InputsPtr) {
					FJsonObject* InputObject = InputValue->AsObject().Get();
					FName InputExpressionName = GetExpressionName(InputObject);
					
					if (Container.Contains(InputExpressionName)) {
						FExpressionInput Input = PopulateExpressionInput(InputObject, Container.GetExpressionByName(InputExpressionName));
						FeatureLevelSwitch->Inputs[i] = Input;
					}
					i++;
				}
			}
		}

		SetExpressionParent(Parent, Expression, Properties);

		if (bAddToParentExpression) {
			AddExpressionToParent(Parent, Expression);
		}
	}
}

void IMaterialGraph::SetExpressionParent(UObject* Parent, UMaterialExpression* Expression, const TSharedPtr<FJsonObject>& Json) {
	if (UMaterialFunction* MaterialFunction = Cast<UMaterialFunction>(Parent)) {
		Expression->Function = MaterialFunction;
	} else if (UMaterial* Material = Cast<UMaterial>(Parent)) {
		Expression->Material = Material;
	}

	if (Cast<UMaterialExpressionTextureBase>(Expression)) {
		const TSharedPtr<FJsonObject>* TexturePtr;
	
		if (Json->TryGetObjectField(TEXT("Texture"), TexturePtr)) {
			Expression->UpdateParameterGuid(true, false);
		}
	}
}

void IMaterialGraph::AddExpressionToParent(UObject* Parent, UMaterialExpression* Expression) {
	/* Comments are added differently */
	if (UMaterialExpressionComment* Comment = Cast<UMaterialExpressionComment>(Expression)) {
		/* In Unreal Engine 5, we have to get the expression collection to add the comment */
#if ENGINE_UE5
		if (UMaterialFunction* MaterialFunction = Cast<UMaterialFunction>(Parent)) MaterialFunction->GetExpressionCollection().AddComment(Comment);
		if (UMaterial* Material = Cast<UMaterial>(Parent)) Material->GetExpressionCollection().AddComment(Comment);
#else
		/* In Unreal Engine 4, we have to get the EditorComments array to add the comment */
		if (UMaterialFunction* MaterialFunction = Cast<UMaterialFunction>(Parent)) MaterialFunction->FunctionEditorComments.Add(Comment);
		if (UMaterial* Material = Cast<UMaterial>(Parent)) Material->EditorComments.Add(Comment);
#endif
	} else {
		/* Adding expressions is different between UE4 and UE5 */
#if ENGINE_UE5
		if (UMaterialFunction* MaterialFunction = Cast<UMaterialFunction>(Parent)) {
			MaterialFunction->GetExpressionCollection().AddExpression(Expression);
		}

		if (UMaterial* Material = Cast<UMaterial>(Parent)) {
			Material->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(Expression);
			Expression->UpdateMaterialExpressionGuid(true, false);
			Material->AddExpressionParameter(Expression, Material->EditorParameters);
		}
#else
		if (UMaterialFunction* MaterialFunction = Cast<UMaterialFunction>(Parent)) {
			MaterialFunction->FunctionExpressions.Add(Expression);
		}

		if (UMaterial* Material = Cast<UMaterial>(Parent)) {
			Material->Expressions.Add(Expression);
			Expression->UpdateMaterialExpressionGuid(true, false);
			Material->AddExpressionParameter(Expression, Material->EditorParameters);
		}
#endif
	}
}

UMaterialExpression* IMaterialGraph::CreateEmptyExpression(FMaterialExpressionNodeExport& Export, FMaterialExpressionNodeExportContainer& Container) {
	const FName Type = Export.Type;
	const FName Name = Export.Name;
	
	const UClass* Class = FindObject<UClass>(ANY_PACKAGE, *Type.ToString());

	/* Material/MaterialFunction Parent */
	UObject* Parent = Export.Parent;

	if (!Class) {
#if ENGINE_UE5
		TArray<FString> Redirects = TArray{
			FLinkerLoad::FindNewPathNameForClass("/Script/InterchangeImport." + Type.ToString(), false),
			FLinkerLoad::FindNewPathNameForClass("/Script/Landscape." + Type.ToString(), false)
		};
		
		for (FString RedirectedPath : Redirects) {
			if (!RedirectedPath.IsEmpty() && !Class)
				Class = FindObject<UClass>(nullptr, *RedirectedPath);
		}
#endif

		if (!Class) {
			Class = FindObject<UClass>(ANY_PACKAGE, *Type.ToString().Replace(TEXT("MaterialExpressionPhysicalMaterialOutput"), TEXT("MaterialExpressionLandscapePhysicalMaterialOutput")));
		}
	}

	/* If a node is missing in the class, notify the user */
	if (!Class) {
		return OnMissingNodeClass(Export, Container);
	}

#if ENGINE_UE4
	/* Manually handled in UE4, as it doesn't exist */
	if (Type == "MaterialExpressionPinBase") {
		return NewObject<UMaterialExpression>(
			Parent,
			UMaterialExpressionReroute::StaticClass(),
			Name
		);
	}
#endif

	return NewObject<UMaterialExpression>
	(
		Parent,
		Class, /* Find class using ANY_PACKAGE (may error in the future) */
		Name,
		RF_Transactional
	);
}

/* ReSharper disable once CppMemberFunctionMayBeConst */
UMaterialExpression* IMaterialGraph::OnMissingNodeClass(FMaterialExpressionNodeExport& Export, FMaterialExpressionNodeExportContainer& Container) {
	/* Get variables from the export data */
	const FName Name = Export.Name;
	FName Type = Export.Type;

	/* Material/MaterialFunction Parent */
	UObject* Parent = Export.Parent;

	/* Get Json Objects from Export */
	const TSharedPtr<FJsonObject> Properties = Export.GetProperties();

#if ENGINE_UE4
	/* In Unreal Engine 4, to combat the absence of Sub-graphs, create a Material Function in place of it */
	if (Type == "MaterialExpressionComposite") {
		const FString SubgraphFunctionName = FileName + "_" + Name.ToString().Replace(TEXT("MaterialExpression"), TEXT(""));

		const UPackage* ParentPackage = Parent->GetOutermost();
		FString SubgraphFunctionPath = ParentPackage->GetPathName();

		SubgraphFunctionPath.Split("/", &SubgraphFunctionPath, nullptr, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		SubgraphFunctionPath = SubgraphFunctionPath + "/";
		
		UPackage* SubgraphLocalOutermostPkg;
		UPackage* SubgraphLocalPackage = FAssetUtilities::CreateAssetPackage(SubgraphFunctionName, SubgraphFunctionPath, SubgraphLocalOutermostPkg);

		UMaterialFunctionFactoryNew* SubgraphMaterialFunctionFactory = NewObject<UMaterialFunctionFactoryNew>();
		UMaterialFunction* SubgraphMaterialFunction = Cast<UMaterialFunction>(SubgraphMaterialFunctionFactory->FactoryCreateNew(UMaterialFunction::StaticClass(), SubgraphLocalOutermostPkg, *SubgraphFunctionName, RF_Standalone | RF_Public, nullptr, GWarn));

		OnAssetCreation(SubgraphMaterialFunction);

		SubgraphFunctions.Add(Name, SubgraphMaterialFunction);

		FMaterialExpressionNodeExport FunctionCall = FMaterialExpressionNodeExport(
			FName(*("MaterialExpressionMaterialFunctionCall_" + Name.ToString().Replace(TEXT("MaterialExpression"), TEXT("")))),
			FName("MaterialExpressionMaterialFunctionCall"),
			FName(""),
			Export.JsonObject,
			Parent
		);

		UMaterialExpressionMaterialFunctionCall* MaterialExpression = Cast<UMaterialExpressionMaterialFunctionCall>(CreateEmptyExpression(FunctionCall, Container));

		MaterialExpression->MaterialFunction = SubgraphMaterialFunction;
		SubgraphMaterialFunction->FunctionExpressions.Empty();

		/* Handle InputExpressions ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
		if (Properties->HasField(TEXT("InputExpressions"))) {
			const TSharedPtr<FJsonObject> InputExpressions = Properties->GetObjectField(TEXT("InputExpressions"));

			const FName InputExpressionName = GetExpressionName(Properties.Get(), "InputExpressions");
					
			if (Container.Contains(InputExpressionName)) {
				FMaterialExpressionNodeExport PinBaseExport = Container.Find(InputExpressionName);

				for (auto Value : PinBaseExport.GetProperties()->GetArrayField(TEXT("ReroutePins"))) {
					auto ReroutePinObject = Value->AsObject();

					FMaterialExpressionNodeExport FunctionOutput = FMaterialExpressionNodeExport(
						FName(*("MaterialExpressionFunctionInput_" + InputExpressionName.ToString().Replace(TEXT("MaterialExpression"), TEXT(""))) + ReroutePinObject->GetStringField(TEXT("Name")).Replace(TEXT(" "), TEXT(""))),
						FName("MaterialExpressionFunctionInput"),
						FName(""),
						Export.JsonObject,
						SubgraphMaterialFunction
					);
				
					UMaterialExpressionFunctionInput* FunctionInputExpression = Cast<UMaterialExpressionFunctionInput>(CreateEmptyExpression(FunctionOutput, Container));

					FunctionInputExpression->InputName = FName(*ReroutePinObject->GetStringField(TEXT("Name")));

					GetObjectSerializer()->DeserializeObjectProperties(RemovePropertiesShared(ReroutePinObject,
					{
						"Material",
						"MaterialFunction",
					}), FunctionInputExpression);
					
					AddExpressionToParent(SubgraphMaterialFunction, FunctionInputExpression);
					SetExpressionParent(SubgraphMaterialFunction, FunctionInputExpression, FunctionOutput.JsonObject);
				}
			}
		}

		/* Handle OutputExpressions ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
		if (Properties->HasField(TEXT("OutputExpressions"))) {
			const TSharedPtr<FJsonObject> InputExpressions = Properties->GetObjectField(TEXT("OutputExpressions"));

			const FName InputExpressionName = GetExpressionName(Properties.Get(), "OutputExpressions");
					
			if (Container.Contains(InputExpressionName)) {
				FMaterialExpressionNodeExport PinBaseExport = Container.Find(InputExpressionName);

				for (auto Value : PinBaseExport.GetProperties()->GetArrayField(TEXT("ReroutePins"))) {
					auto ReroutePinObject = Value->AsObject();

					FMaterialExpressionNodeExport FunctionOutput = FMaterialExpressionNodeExport(
						FName(*("MaterialExpressionFunctionOutput_" + InputExpressionName.ToString().Replace(TEXT("MaterialExpression"), TEXT(""))) + ReroutePinObject->GetStringField(TEXT("Name")).Replace(TEXT(" "), TEXT(""))),
						FName("MaterialExpressionFunctionOutput"),
						FName(""),
						Export.JsonObject,
						SubgraphMaterialFunction
					);
				
					UMaterialExpressionFunctionOutput* FunctionOutputExpression = Cast<UMaterialExpressionFunctionOutput>(CreateEmptyExpression(FunctionOutput, Container));

					FunctionOutputExpression->OutputName = FName(*ReroutePinObject->GetStringField(TEXT("Name")));

					GetObjectSerializer()->DeserializeObjectProperties(RemovePropertiesShared(ReroutePinObject,
					{
						"Material",
						"MaterialFunction",
					}), FunctionOutputExpression);

					FGuid ID = FunctionOutputExpression->GetMaterialExpressionId();

					AddExpressionToParent(SubgraphMaterialFunction, FunctionOutputExpression);
					SetExpressionParent(SubgraphMaterialFunction, FunctionOutputExpression, FunctionOutput.JsonObject);

					FFunctionExpressionInput InputExpression = FFunctionExpressionInput();

					InputExpression.ExpressionInputId = ID;

					const FName RerouteExpressionName = GetExpressionName(ReroutePinObject.Get());
					FMaterialExpressionNodeExport RerouteInputExport = Container.Find(RerouteExpressionName);

					TSharedPtr<FJsonObject> ExpressionReroute = RerouteInputExport.JsonObject->GetObjectField(TEXT("Properties"))->GetObjectField(TEXT("Input"));
					const FName NewExpressionName = GetExpressionName(ExpressionReroute.Get());
					FMaterialExpressionNodeExport NewExpressionExport = Container.Find(NewExpressionName);

					if (!NewExpressionExport.Expression)
					{
						NewExpressionExport.Expression = CreateEmptyExpression(NewExpressionExport, Container);
					}

					InputExpression.ExpressionInputId = ID;
					FExpressionInput Input = PopulateExpressionInput(ExpressionReroute.Get(), NewExpressionExport.Expression);

					InputExpression.Input = Input;
					MaterialExpression->FunctionInputs.Add(InputExpression);
				}
			}
		}

		TSharedPtr<FJsonObject> OutputExpressions = Properties->GetObjectField(TEXT("OutputExpressions"));
		
		return MaterialExpression;
	}
#endif

	/* Add a comment in the graph notifying the user of a missing node ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
	UMaterialExpressionComment* Comment = NewObject<UMaterialExpressionComment>(Parent, UMaterialExpressionComment::StaticClass(), *("UMaterialExpressionComment_" + Type.ToString()), RF_Transactional);

	Comment->Text = *("Missing Node Class " + Type.ToString());
	Comment->CommentColor = FLinearColor(1.0, 0.0, 0.0);
	Comment->bCommentBubbleVisible = true;
	Comment->SizeX = 415;
	Comment->SizeY = 40;

	Comment->Desc = "A node is missing from your Unreal Engine build. This can occur for several reasons, but it is most likely because your version of Unreal Engine is older than the one you are porting from.";

	GetObjectSerializer()->DeserializeObjectProperties(Properties, Comment);
	AddExpressionToParent(Parent, Comment);

	/* Add a notification letting the user know of a missing node ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
	GLog->Log(*("JsonAsAsset: Missing Node " + Type.ToString() + " in Parent " + Parent->GetName()));
	AppendNotification(
		FText::FromString("Missing Node (" + Parent->GetName() + ")"),
		FText::FromString(Type.ToString()),
		8.0f,
		SNotificationItem::ECompletionState::CS_Fail,
		true,
		456.0
	);

	/* Put a reroute in place of the missing node */
	return NewObject<UMaterialExpression>(
		Parent,
		UMaterialExpressionReroute::StaticClass(),
		Name
	);
}

void IMaterialGraph::SpawnMaterialDataMissingNotification() const {
	FNotificationInfo Info = FNotificationInfo(FText::FromString("Material Data Missing (" + FileName + ")"));
	Info.ExpireDuration = 7.0f;
	Info.bUseLargeFont = true;
	Info.bUseSuccessFailIcons = true;
	Info.WidthOverride = FOptionalSize(350);
	SetNotificationSubText(Info, FText::FromString(FString("Please use the correct FModel provided in the JsonAsAsset server.")));

	const TSharedPtr<SNotificationItem> NotificationPtr = FSlateNotificationManager::Get().AddNotification(Info);
	NotificationPtr->SetCompletionState(SNotificationItem::CS_Fail);
}

FExpressionInput IMaterialGraph::PopulateExpressionInput(const FJsonObject* JsonProperties, UMaterialExpression* Expression, const FString& Type) {
	FExpressionInput Input;
	Input.Expression = Expression;

	/* Each Mask input/output */
	int OutputIndex;
	if (JsonProperties->TryGetNumberField(TEXT("OutputIndex"), OutputIndex)) Input.OutputIndex = OutputIndex;
	FString InputName;
	if (JsonProperties->TryGetStringField(TEXT("InputName"), InputName)) Input.InputName = FName(InputName);
	int Mask;
	if (JsonProperties->TryGetNumberField(TEXT("Mask"), Mask)) Input.Mask = Mask;
	int MaskR;
	if (JsonProperties->TryGetNumberField(TEXT("MaskR"), MaskR)) Input.MaskR = MaskR;
	int MaskG;
	if (JsonProperties->TryGetNumberField(TEXT("MaskG"), MaskG)) Input.MaskG = MaskG;
	int MaskB;
	if (JsonProperties->TryGetNumberField(TEXT("MaskB"), MaskB)) Input.MaskB = MaskB;
	int MaskA;
	if (JsonProperties->TryGetNumberField(TEXT("MaskA"), MaskA)) Input.MaskA = MaskA;

	if (Type == "Color") {
		if (FColorMaterialInput* ColorInput = reinterpret_cast<FColorMaterialInput*>(&Input)) {
			bool UseConstant;
			if (JsonProperties->TryGetBoolField(TEXT("UseConstant"), UseConstant)) ColorInput->UseConstant = UseConstant;
			const TSharedPtr<FJsonObject>* Constant;
			if (JsonProperties->TryGetObjectField(TEXT("Constant"), Constant)) ColorInput->Constant = ObjectToLinearColor(Constant->Get()).ToFColor(true);
			Input = FExpressionInput(*ColorInput);
		}
	} else if (Type == "Scalar") {
		if (FScalarMaterialInput* ScalarInput = reinterpret_cast<FScalarMaterialInput*>(&Input)) {
			bool UseConstant;
			if (JsonProperties->TryGetBoolField(TEXT("UseConstant"), UseConstant)) ScalarInput->UseConstant = UseConstant;
#if ENGINE_UE5
			float Constant;
#else
			double Constant;
#endif
			if (JsonProperties->TryGetNumberField(TEXT("Constant"), Constant)) ScalarInput->Constant = Constant;
			Input = FExpressionInput(*ScalarInput);
		}
	} else if (Type == "Vector") {
		if (FVectorMaterialInput* VectorInput = reinterpret_cast<FVectorMaterialInput*>(&Input)) {
			bool UseConstant;
			if (JsonProperties->TryGetBoolField(TEXT("UseConstant"), UseConstant)) VectorInput->UseConstant = UseConstant;
			const TSharedPtr<FJsonObject>* Constant;
			if (JsonProperties->TryGetObjectField(TEXT("Constant"), Constant)) VectorInput->Constant = ObjectToVector3f(Constant->Get());
			Input = FExpressionInput(*VectorInput);
		}
	}

	return Input;
}

FName IMaterialGraph::GetExpressionName(const FJsonObject* JsonProperties, const FString& OverrideParameterName) {
	const TSharedPtr<FJsonValue> ExpressionField = JsonProperties->TryGetField(OverrideParameterName);

	if (ExpressionField == nullptr || ExpressionField->IsNull()) {
		/* Must be from < 4.25 */
		return FName(JsonProperties->GetStringField(TEXT("ExpressionName")));
	}

	const TSharedPtr<FJsonObject> ExpressionObject = ExpressionField->AsObject();
	FString ObjectName;
	
	if (ExpressionObject->TryGetStringField(TEXT("ObjectName"), ObjectName)) {
		return GetExportNameOfSubobject(ObjectName);
	}

	return NAME_None;
}