// Copyright softdaddy-o 2024. All Rights Reserved.

#include "Tools/Write/AddInterfaceFunctionTool.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_EditablePinBase.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"
#include "SoftUEBridgeEditorModule.h"
#include "Utils/BridgeAssetModifier.h"

namespace
{
	UClass* ResolveInterfaceParamClass(const FString& ClassName)
	{
		if (ClassName.IsEmpty())
		{
			return nullptr;
		}

		UClass* Class = LoadClass<UObject>(nullptr, *ClassName);
		if (!Class && !ClassName.EndsWith(TEXT("_C")))
		{
			Class = LoadClass<UObject>(nullptr, *(ClassName + TEXT("_C")));
		}
		if (!Class)
		{
			Class = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::ExactClass);
		}
		if (!Class && !ClassName.StartsWith(TEXT("U")))
		{
			Class = FindFirstObject<UClass>(*(TEXT("U") + ClassName), EFindFirstObjectOptions::ExactClass);
		}
		if (!Class && !ClassName.StartsWith(TEXT("A")))
		{
			Class = FindFirstObject<UClass>(*(TEXT("A") + ClassName), EFindFirstObjectOptions::ExactClass);
		}

		return Class;
	}

	UEnum* ResolveInterfaceParamEnum(const FString& EnumName)
	{
		if (EnumName.IsEmpty())
		{
			return nullptr;
		}

		UEnum* Enum = LoadObject<UEnum>(nullptr, *EnumName);
		if (!Enum)
		{
			Enum = FindFirstObject<UEnum>(*EnumName, EFindFirstObjectOptions::ExactClass);
		}

		return Enum;
	}

	bool BuildInterfacePinType(const FString& InType, const FString& InSubType, const FString& InContainer, FEdGraphPinType& OutPinType, FString& OutError)
	{
		const FString Type = InType.ToLower();
		const FString Container = InContainer.ToLower();
		OutPinType = FEdGraphPinType();

		if (Container.IsEmpty() || Container == TEXT("scalar") || Container == TEXT("none"))
		{
			OutPinType.ContainerType = EPinContainerType::None;
		}
		else if (Container == TEXT("array"))
		{
			OutPinType.ContainerType = EPinContainerType::Array;
		}
		else if (Container == TEXT("set"))
		{
			OutPinType.ContainerType = EPinContainerType::Set;
		}
		else
		{
			OutError = TEXT("Unsupported container. Use 'scalar', 'array', or 'set'.");
			return false;
		}

		if (Type == TEXT("bool") || Type == TEXT("boolean"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		}
		else if (Type == TEXT("int") || Type == TEXT("integer"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int;
		}
		else if (Type == TEXT("int64") || Type == TEXT("integer64"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
		}
		else if (Type == TEXT("float"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
			OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
		}
		else if (Type == TEXT("double") || Type == TEXT("real"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
			OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
		}
		else if (Type == TEXT("string"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_String;
		}
		else if (Type == TEXT("name"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Name;
		}
		else if (Type == TEXT("text"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Text;
		}
		else if (Type == TEXT("vector"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			OutPinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
		}
		else if (Type == TEXT("rotator"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			OutPinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
		}
		else if (Type == TEXT("transform"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			OutPinType.PinSubCategoryObject = TBaseStructure<FTransform>::Get();
		}
		else if (Type == TEXT("linearcolor") || Type == TEXT("color"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			OutPinType.PinSubCategoryObject = TBaseStructure<FLinearColor>::Get();
		}
		else if (Type == TEXT("vector2d"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			OutPinType.PinSubCategoryObject = TBaseStructure<FVector2D>::Get();
		}
		else if (Type == TEXT("object") || Type == TEXT("actor") || Type == TEXT("component"))
		{
			UClass* ObjectClass = ResolveInterfaceParamClass(InSubType.IsEmpty() ? (Type == TEXT("actor") ? TEXT("Actor") : TEXT("Object")) : InSubType);
			if (!ObjectClass)
			{
				OutError = FString::Printf(TEXT("Object class not found: %s"), *InSubType);
				return false;
			}

			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Object;
			OutPinType.PinSubCategoryObject = ObjectClass;
		}
		else if (Type == TEXT("class"))
		{
			UClass* ObjectClass = ResolveInterfaceParamClass(InSubType.IsEmpty() ? TEXT("Object") : InSubType);
			if (!ObjectClass)
			{
				OutError = FString::Printf(TEXT("Class type not found: %s"), *InSubType);
				return false;
			}

			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Class;
			OutPinType.PinSubCategoryObject = ObjectClass;
		}
		else if (Type == TEXT("enum"))
		{
			UEnum* Enum = ResolveInterfaceParamEnum(InSubType);
			if (!Enum)
			{
				OutError = FString::Printf(TEXT("Enum not found: %s"), *InSubType);
				return false;
			}

			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
			OutPinType.PinSubCategoryObject = Enum;
		}
		else
		{
			OutError = FString::Printf(TEXT("Unsupported parameter type '%s'."), *InType);
			return false;
		}

		return true;
	}

	bool AddPinsFromSpec(
		const TArray<TSharedPtr<FJsonValue>>* Specs,
		UK2Node_EditablePinBase* Node,
		EEdGraphPinDirection Direction,
		TArray<TSharedPtr<FJsonValue>>& OutPins,
		FString& OutError)
	{
		if (!Specs || !Node)
		{
			return true;
		}

		for (const TSharedPtr<FJsonValue>& Value : *Specs)
		{
			const TSharedPtr<FJsonObject>* SpecObject = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(SpecObject) || !SpecObject || !SpecObject->IsValid())
			{
				OutError = TEXT("Each parameter spec must be an object.");
				return false;
			}

			const FString Name = SpecObject->Get()->GetStringField(TEXT("name"));
			const FString Type = SpecObject->Get()->GetStringField(TEXT("type"));
			const FString SubType = SpecObject->Get()->GetStringField(TEXT("sub_type"));
			const FString Container = SpecObject->Get()->GetStringField(TEXT("container"));
			const FString DefaultValue = SpecObject->Get()->GetStringField(TEXT("default_value"));

			if (Name.IsEmpty() || Type.IsEmpty())
			{
				OutError = TEXT("Each parameter spec requires 'name' and 'type'.");
				return false;
			}

			FEdGraphPinType PinType;
			if (!BuildInterfacePinType(Type, SubType, Container, PinType, OutError))
			{
				return false;
			}

			UEdGraphPin* CreatedPin = Node->CreateUserDefinedPin(FName(*Name), PinType, Direction);
			if (CreatedPin && !DefaultValue.IsEmpty())
			{
				CreatedPin->DefaultValue = DefaultValue;
			}

			TSharedPtr<FJsonObject> PinInfo = MakeShared<FJsonObject>();
			PinInfo->SetStringField(TEXT("name"), Name);
			PinInfo->SetStringField(TEXT("type"), PinType.PinCategory.ToString());
			PinInfo->SetStringField(TEXT("container"), Container.IsEmpty() ? TEXT("scalar") : Container);
			if (PinType.PinSubCategoryObject.IsValid())
			{
				PinInfo->SetStringField(TEXT("sub_type"), PinType.PinSubCategoryObject->GetName());
			}
			OutPins.Add(MakeShared<FJsonValueObject>(PinInfo));
		}

		return true;
	}
}

FString UAddInterfaceFunctionTool::GetToolDescription() const
{
	return TEXT("Add a function definition to a Blueprint Interface asset (BPI). "
		"Use this to create interface functions on Blueprint Interface assets. "
		"Optional inputs and outputs define the interface signature.");
}

TMap<FString, FBridgeSchemaProperty> UAddInterfaceFunctionTool::GetInputSchema() const
{
	TMap<FString, FBridgeSchemaProperty> Schema;

	FBridgeSchemaProperty AssetPath;
	AssetPath.Type = TEXT("string");
	AssetPath.Description = TEXT("Blueprint Interface asset path, e.g. '/Game/Blueprints/BPI_Interact'");
	AssetPath.bRequired = true;
	Schema.Add(TEXT("asset_path"), AssetPath);

	FBridgeSchemaProperty FunctionName;
	FunctionName.Type = TEXT("string");
	FunctionName.Description = TEXT("New interface function name.");
	FunctionName.bRequired = true;
	Schema.Add(TEXT("function_name"), FunctionName);

	FBridgeSchemaProperty Inputs;
	Inputs.Type = TEXT("array");
	Inputs.Description = TEXT("Optional array of input parameters. Each item: {name, type, sub_type?, container?, default_value?}.");
	Inputs.bRequired = false;
	Schema.Add(TEXT("inputs"), Inputs);

	FBridgeSchemaProperty Outputs;
	Outputs.Type = TEXT("array");
	Outputs.Description = TEXT("Optional array of output parameters. Each item: {name, type, sub_type?, container?, default_value?}.");
	Outputs.bRequired = false;
	Schema.Add(TEXT("outputs"), Outputs);

	return Schema;
}

TArray<FString> UAddInterfaceFunctionTool::GetRequiredParams() const
{
	return { TEXT("asset_path"), TEXT("function_name") };
}

FBridgeToolResult UAddInterfaceFunctionTool::Execute(
	const TSharedPtr<FJsonObject>& Arguments,
	const FBridgeToolContext& Context)
{
	const FString AssetPath = GetStringArgOrDefault(Arguments, TEXT("asset_path"));
	const FString FunctionName = GetStringArgOrDefault(Arguments, TEXT("function_name"));

	if (AssetPath.IsEmpty() || FunctionName.IsEmpty())
	{
		return FBridgeToolResult::Error(TEXT("asset_path and function_name are required"));
	}

	FString LoadError;
	UBlueprint* Blueprint = FBridgeAssetModifier::LoadAssetByPath<UBlueprint>(AssetPath, LoadError);
	if (!Blueprint)
	{
		return FBridgeToolResult::Error(LoadError);
	}

	if (Blueprint->BlueprintType != BPTYPE_Interface)
	{
		return FBridgeToolResult::Error(TEXT("add-interface-function only supports Blueprint Interface assets (BPI)."));
	}

	if (FBridgeAssetModifier::FindGraphByName(Blueprint, FunctionName))
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("An interface function named '%s' already exists."), *FunctionName));
	}

	const FScopedTransaction Transaction(FText::Format(
		NSLOCTEXT("MCP", "AddInterfaceFunction", "Add Interface Function {0}"),
		FText::FromString(FunctionName)));

	Blueprint->Modify();

	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint,
		FName(*FunctionName),
		UEdGraph::StaticClass(),
		UEdGraphSchema_K2::StaticClass());

	if (!NewGraph)
	{
		return FBridgeToolResult::Error(FString::Printf(TEXT("Failed to create interface function graph '%s'."), *FunctionName));
	}

	FBlueprintEditorUtils::AddFunctionGraph(Blueprint, NewGraph, /*bIsUserCreated=*/true, static_cast<UFunction*>(nullptr));

	UK2Node_FunctionEntry* EntryNode = nullptr;
	UK2Node_FunctionResult* ResultNode = nullptr;
	for (UEdGraphNode* Node : NewGraph->Nodes)
	{
		if (!Node)
		{
			continue;
		}

		if (!EntryNode)
		{
			EntryNode = Cast<UK2Node_FunctionEntry>(Node);
		}
		if (!ResultNode)
		{
			ResultNode = Cast<UK2Node_FunctionResult>(Node);
		}
	}

	if (!EntryNode)
	{
		return FBridgeToolResult::Error(TEXT("Failed to initialize the interface function entry node."));
	}

	const TArray<TSharedPtr<FJsonValue>>* InputSpecs = nullptr;
	Arguments->TryGetArrayField(TEXT("inputs"), InputSpecs);
	const TArray<TSharedPtr<FJsonValue>>* OutputSpecs = nullptr;
	Arguments->TryGetArrayField(TEXT("outputs"), OutputSpecs);

	if (OutputSpecs && OutputSpecs->Num() > 0 && !ResultNode)
	{
		return FBridgeToolResult::Error(TEXT("Failed to initialize the interface function result node."));
	}

	TArray<TSharedPtr<FJsonValue>> InputPins;
	TArray<TSharedPtr<FJsonValue>> OutputPins;
	FString PinError;
	if (!AddPinsFromSpec(InputSpecs, EntryNode, EGPD_Output, InputPins, PinError))
	{
		return FBridgeToolResult::Error(PinError);
	}
	if (!AddPinsFromSpec(OutputSpecs, ResultNode, EGPD_Input, OutputPins, PinError))
	{
		return FBridgeToolResult::Error(PinError);
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	Blueprint->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("function_name"), FunctionName);
	Result->SetArrayField(TEXT("inputs"), InputPins);
	Result->SetArrayField(TEXT("outputs"), OutputPins);
	Result->SetBoolField(TEXT("needs_save"), true);

	return FBridgeToolResult::Json(Result);
}
