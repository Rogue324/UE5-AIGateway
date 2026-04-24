#include "Tools/AIGatewayToolRuntime.h"

#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Tools/BridgeToolRegistry.h"

namespace
{
    FString SerializeToolRuntimeJsonObject(const TSharedPtr<FJsonObject>& Object)
    {
        if (!Object.IsValid())
        {
            return TEXT("{}");
        }

        FString Output;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
        FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
        return Output;
    }

    bool TryParseJsonObject(const FString& Text, TSharedPtr<FJsonObject>& OutObject)
    {
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
        return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
    }

    FString MakeSummary(const FString& ToolName, const FString& MessageText)
    {
        FString Summary = MessageText.TrimStartAndEnd();
        Summary.ReplaceInline(TEXT("\r"), TEXT(" "));
        Summary.ReplaceInline(TEXT("\n"), TEXT(" "));
        Summary = Summary.Left(200).TrimEnd();
        return Summary.IsEmpty() ? FString::Printf(TEXT("Executed '%s'."), *ToolName) : Summary;
    }

    FAIGatewayToolResult ConvertBridgeResult(const FString& ToolName, const FBridgeToolResult& BridgeResult)
    {
        FString MessageText;
        for (const TSharedPtr<FJsonObject>& Item : BridgeResult.Content)
        {
            if (!Item.IsValid())
            {
                continue;
            }

            FString Text;
            if (Item->TryGetStringField(TEXT("text"), Text))
            {
                if (!MessageText.IsEmpty())
                {
                    MessageText.Append(TEXT("\n"));
                }
                MessageText.Append(Text);
            }
        }

        TSharedPtr<FJsonObject> ParsedPayload;
        const bool bHasPayload = !MessageText.IsEmpty() && TryParseJsonObject(MessageText, ParsedPayload);
        return BridgeResult.bIsError
            ? FAIGatewayToolResult::Error(MakeSummary(ToolName, MessageText), bHasPayload ? ParsedPayload : nullptr)
            : FAIGatewayToolResult::Success(MakeSummary(ToolName, MessageText), bHasPayload ? ParsedPayload : nullptr);
    }

    TSharedPtr<FJsonObject> MakeSchemaProperty(const FString& Type, const FString& Description)
    {
        TSharedPtr<FJsonObject> PropertyObject = MakeShared<FJsonObject>();
        PropertyObject->SetStringField(TEXT("type"), Type);
        PropertyObject->SetStringField(TEXT("description"), Description);
        return PropertyObject;
    }

    TSharedPtr<FJsonObject> MakeObjectSchema(const TMap<FString, TSharedPtr<FJsonObject>>& Properties, const TArray<FString>& RequiredFields)
    {
        TSharedPtr<FJsonObject> SchemaObject = MakeShared<FJsonObject>();
        SchemaObject->SetStringField(TEXT("type"), TEXT("object"));

        TSharedPtr<FJsonObject> PropertiesObject = MakeShared<FJsonObject>();
        for (const TPair<FString, TSharedPtr<FJsonObject>>& Pair : Properties)
        {
            PropertiesObject->SetObjectField(Pair.Key, Pair.Value);
        }
        SchemaObject->SetObjectField(TEXT("properties"), PropertiesObject);
        SchemaObject->SetBoolField(TEXT("additionalProperties"), false);

        if (RequiredFields.Num() > 0)
        {
            TArray<TSharedPtr<FJsonValue>> RequiredArray;
            for (const FString& Field : RequiredFields)
            {
                RequiredArray.Add(MakeShared<FJsonValueString>(Field));
            }
            SchemaObject->SetArrayField(TEXT("required"), RequiredArray);
        }

        return SchemaObject;
    }

    EAIGatewayToolConfirmationPolicy GetConfirmationPolicy(const FString& ToolName)
    {
        return ToolName == TEXT("delete-actor") ||
            ToolName == TEXT("batch-delete-actors") ||
            ToolName == TEXT("start-pie") ||
            ToolName == TEXT("stop-pie") ||
            ToolName == TEXT("pie-session")
            ? EAIGatewayToolConfirmationPolicy::ExplicitApproval
            : EAIGatewayToolConfirmationPolicy::None;
    }

    TSharedPtr<FJsonObject> ActorToJson(const AActor* Actor)
    {
        TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
        Object->SetStringField(TEXT("name"), Actor->GetName());
#if WITH_EDITOR
        Object->SetStringField(TEXT("label"), Actor->GetActorLabel());
#else
        Object->SetStringField(TEXT("label"), Actor->GetName());
#endif
        Object->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
        Object->SetStringField(TEXT("path"), Actor->GetPathName());
        return Object;
    }

    AActor* FindActorByNameOrLabel(const FString& ActorName)
    {
        UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
        if (!World)
        {
            return nullptr;
        }

        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* Actor = *It;
            if (!Actor)
            {
                continue;
            }

            if (Actor->GetName().Equals(ActorName, ESearchCase::IgnoreCase))
            {
                return Actor;
            }

#if WITH_EDITOR
            if (Actor->GetActorLabel().Equals(ActorName, ESearchCase::IgnoreCase))
            {
                return Actor;
            }
#endif
        }

        return nullptr;
    }
}

FAIGatewayToolResult FAIGatewayToolResult::Success(const FString& InSummary, const TSharedPtr<FJsonObject>& InPayload)
{
    FAIGatewayToolResult Result;
    Result.bSuccess = true;
    Result.Summary = InSummary;
    Result.Payload = InPayload;
    return Result;
}

FAIGatewayToolResult FAIGatewayToolResult::Error(const FString& InSummary, const TSharedPtr<FJsonObject>& InPayload)
{
    FAIGatewayToolResult Result;
    Result.Summary = InSummary;
    Result.Payload = InPayload;
    return Result;
}

FAIGatewayToolResult FAIGatewayToolResult::Rejected(const FString& InSummary)
{
    FAIGatewayToolResult Result = Error(InSummary);
    Result.bWasRejected = true;
    return Result;
}

FString FAIGatewayToolResult::ToMessageContent() const
{
    TSharedPtr<FJsonObject> ContentObject = MakeShared<FJsonObject>();
    ContentObject->SetBoolField(TEXT("success"), bSuccess);
    ContentObject->SetBoolField(TEXT("rejected"), bWasRejected);
    ContentObject->SetStringField(TEXT("summary"), Summary);

    if (Payload.IsValid())
    {
        ContentObject->SetObjectField(TEXT("data"), Payload);
    }

    return SerializeToolRuntimeJsonObject(ContentObject);
}

FAIGatewayToolRuntime& FAIGatewayToolRuntime::Get()
{
    static FAIGatewayToolRuntime Instance;
    return Instance;
}

void FAIGatewayToolRuntime::Startup()
{
    if (bStarted)
    {
        return;
    }

    FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("SoftUEBridge"));
    FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("SoftUEBridgeEditor"));
    BuildDefinitions();
    bStarted = true;
}

void FAIGatewayToolRuntime::Shutdown()
{
    bStarted = false;
}

const TArray<FAIGatewayToolDefinition>& FAIGatewayToolRuntime::GetToolDefinitions() const
{
    return Definitions;
}

const FAIGatewayToolDefinition* FAIGatewayToolRuntime::FindDefinition(const FString& ToolName) const
{
    const int32* DefinitionIndex = DefinitionIndexByName.Find(ToolName);
    return (DefinitionIndex != nullptr && Definitions.IsValidIndex(*DefinitionIndex)) ? &Definitions[*DefinitionIndex] : nullptr;
}

FAIGatewayToolResult FAIGatewayToolRuntime::ExecuteTool(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments) const
{
    if (ToolName == TEXT("get-selected-actors"))
    {
        USelection* Selection = GEditor ? GEditor->GetSelectedActors() : nullptr;
        if (!Selection)
        {
            return FAIGatewayToolResult::Error(TEXT("Actor selection is not available."));
        }

        TArray<TSharedPtr<FJsonValue>> Actors;
        for (FSelectionIterator It(*Selection); It; ++It)
        {
            if (AActor* Actor = Cast<AActor>(*It))
            {
                Actors.Add(MakeShared<FJsonValueObject>(ActorToJson(Actor)));
            }
        }

        TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
        Payload->SetArrayField(TEXT("actors"), Actors);
        Payload->SetNumberField(TEXT("count"), Actors.Num());
        return FAIGatewayToolResult::Success(FString::Printf(TEXT("Found %d selected actor(s)."), Actors.Num()), Payload);
    }

    FBridgeToolContext Context;
    Context.RequestId = TEXT("aigateway-chat");

    if (ToolName == TEXT("delete-actor"))
    {
        const FString ActorName = Arguments.IsValid() ? Arguments->GetStringField(TEXT("actor")) : FString();
        TSharedPtr<FJsonObject> BridgeArgs = MakeShared<FJsonObject>();
        TArray<TSharedPtr<FJsonValue>> Actors;
        Actors.Add(MakeShared<FJsonValueString>(ActorName));
        BridgeArgs->SetArrayField(TEXT("actors"), Actors);
        return ConvertBridgeResult(TEXT("batch-delete-actors"), FBridgeToolRegistry::Get().ExecuteTool(TEXT("batch-delete-actors"), BridgeArgs, Context));
    }

    if (ToolName == TEXT("start-pie") || ToolName == TEXT("stop-pie"))
    {
        TSharedPtr<FJsonObject> BridgeArgs = MakeShared<FJsonObject>();
        BridgeArgs->SetStringField(TEXT("action"), ToolName == TEXT("start-pie") ? TEXT("start") : TEXT("stop"));
        return ConvertBridgeResult(TEXT("pie-session"), FBridgeToolRegistry::Get().ExecuteTool(TEXT("pie-session"), BridgeArgs, Context));
    }

    if (ToolName == TEXT("set-blueprint-default"))
    {
        TSharedPtr<FJsonObject> BridgeArgs = MakeShared<FJsonObject>();
        BridgeArgs->SetStringField(TEXT("asset_path"), Arguments->GetStringField(TEXT("asset_path")));
        BridgeArgs->SetStringField(TEXT("property_path"), Arguments->GetStringField(TEXT("property_name")));
        BridgeArgs->SetField(TEXT("value"), Arguments->TryGetField(TEXT("value")));
        return ConvertBridgeResult(TEXT("set-asset-property"), FBridgeToolRegistry::Get().ExecuteTool(TEXT("set-asset-property"), BridgeArgs, Context));
    }

    if (ToolName == TEXT("set-actor-transform"))
    {
        const FString ActorName = Arguments.IsValid() ? Arguments->GetStringField(TEXT("actor")) : FString();
        AActor* Actor = FindActorByNameOrLabel(ActorName);
        if (!Actor)
        {
            return FAIGatewayToolResult::Error(FString::Printf(TEXT("Actor '%s' was not found."), *ActorName));
        }

        FVector Location = Actor->GetActorLocation();
        FRotator Rotation = Actor->GetActorRotation();
        FVector Scale = Actor->GetActorScale3D();

        const TSharedPtr<FJsonObject>* TransformObject = nullptr;
        if (Arguments.IsValid() && Arguments->TryGetObjectField(TEXT("transform"), TransformObject))
        {
            const TSharedPtr<FJsonObject>* LocationObject = nullptr;
            if ((*TransformObject)->TryGetObjectField(TEXT("location"), LocationObject))
            {
                Location.X = (*LocationObject)->GetNumberField(TEXT("x"));
                Location.Y = (*LocationObject)->GetNumberField(TEXT("y"));
                Location.Z = (*LocationObject)->GetNumberField(TEXT("z"));
            }
            const TSharedPtr<FJsonObject>* RotationObject = nullptr;
            if ((*TransformObject)->TryGetObjectField(TEXT("rotation"), RotationObject))
            {
                Rotation.Pitch = (*RotationObject)->GetNumberField(TEXT("pitch"));
                Rotation.Yaw = (*RotationObject)->GetNumberField(TEXT("yaw"));
                Rotation.Roll = (*RotationObject)->GetNumberField(TEXT("roll"));
            }
            const TSharedPtr<FJsonObject>* ScaleObject = nullptr;
            if ((*TransformObject)->TryGetObjectField(TEXT("scale"), ScaleObject))
            {
                Scale.X = (*ScaleObject)->GetNumberField(TEXT("x"));
                Scale.Y = (*ScaleObject)->GetNumberField(TEXT("y"));
                Scale.Z = (*ScaleObject)->GetNumberField(TEXT("z"));
            }
        }

        const TSharedPtr<FJsonObject>* LocationObject = nullptr;
        if (Arguments.IsValid() && Arguments->TryGetObjectField(TEXT("location"), LocationObject))
        {
            Location.X = (*LocationObject)->GetNumberField(TEXT("x"));
            Location.Y = (*LocationObject)->GetNumberField(TEXT("y"));
            Location.Z = (*LocationObject)->GetNumberField(TEXT("z"));
        }

        const TSharedPtr<FJsonObject>* RotationObject = nullptr;
        if (Arguments.IsValid() && Arguments->TryGetObjectField(TEXT("rotation"), RotationObject))
        {
            Rotation.Pitch = (*RotationObject)->GetNumberField(TEXT("pitch"));
            Rotation.Yaw = (*RotationObject)->GetNumberField(TEXT("yaw"));
            Rotation.Roll = (*RotationObject)->GetNumberField(TEXT("roll"));
        }

        const TSharedPtr<FJsonObject>* ScaleObject = nullptr;
        if (Arguments.IsValid() && Arguments->TryGetObjectField(TEXT("scale"), ScaleObject))
        {
            Scale.X = (*ScaleObject)->GetNumberField(TEXT("x"));
            Scale.Y = (*ScaleObject)->GetNumberField(TEXT("y"));
            Scale.Z = (*ScaleObject)->GetNumberField(TEXT("z"));
        }

        Actor->Modify();
        Actor->SetActorLocationAndRotation(Location, Rotation);
        Actor->SetActorScale3D(Scale);

        TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
        Payload->SetObjectField(TEXT("actor"), ActorToJson(Actor));
        return FAIGatewayToolResult::Success(FString::Printf(TEXT("Updated transform for '%s'."), *Actor->GetName()), Payload);
    }

    if (FBridgeToolRegistry::Get().HasTool(ToolName))
    {
        return ConvertBridgeResult(ToolName, FBridgeToolRegistry::Get().ExecuteTool(ToolName, Arguments, Context));
    }

    return FAIGatewayToolResult::Error(FString::Printf(TEXT("Unknown tool '%s'."), *ToolName));
}

void FAIGatewayToolRuntime::BuildDefinitions()
{
    Definitions.Reset();
    DefinitionIndexByName.Reset();

    auto AddDefinition = [this](const FString& Name, const FString& Description, const TSharedPtr<FJsonObject>& Parameters, const EAIGatewayToolConfirmationPolicy ConfirmationPolicy = EAIGatewayToolConfirmationPolicy::None)
    {
        if (DefinitionIndexByName.Contains(Name))
        {
            return;
        }

        FAIGatewayToolDefinition Definition;
        Definition.Name = Name;
        Definition.Description = Description;
        Definition.Parameters = Parameters;
        Definition.ConfirmationPolicy = ConfirmationPolicy;
        DefinitionIndexByName.Add(Name, Definitions.Add(MoveTemp(Definition)));
    };

    for (const FBridgeToolDefinition& BridgeDefinition : FBridgeToolRegistry::Get().GetAllToolDefinitions())
    {
        TSharedPtr<FJsonObject> PropertiesObject = MakeShared<FJsonObject>();
        for (const TPair<FString, FBridgeSchemaProperty>& Pair : BridgeDefinition.InputSchema)
        {
            PropertiesObject->SetObjectField(Pair.Key, Pair.Value.ToJson());
        }

        TSharedPtr<FJsonObject> SchemaObject = MakeShared<FJsonObject>();
        SchemaObject->SetStringField(TEXT("type"), TEXT("object"));
        SchemaObject->SetObjectField(TEXT("properties"), PropertiesObject);
        SchemaObject->SetBoolField(TEXT("additionalProperties"), false);

        if (BridgeDefinition.Required.Num() > 0)
        {
            TArray<TSharedPtr<FJsonValue>> RequiredArray;
            for (const FString& RequiredField : BridgeDefinition.Required)
            {
                RequiredArray.Add(MakeShared<FJsonValueString>(RequiredField));
            }
            SchemaObject->SetArrayField(TEXT("required"), RequiredArray);
        }

        AddDefinition(BridgeDefinition.Name, BridgeDefinition.Description, SchemaObject, GetConfirmationPolicy(BridgeDefinition.Name));
    }

    AddDefinition(
        TEXT("get-selected-actors"),
        TEXT("Get the current actor selection in the editor."),
        MakeObjectSchema({}, {}));

    AddDefinition(
        TEXT("delete-actor"),
        TEXT("Delete a single actor from the current editor level."),
        MakeObjectSchema(
            {
                { TEXT("actor"), MakeSchemaProperty(TEXT("string"), TEXT("Actor name or label.")) }
            },
            { TEXT("actor") }),
        EAIGatewayToolConfirmationPolicy::ExplicitApproval);

    AddDefinition(
        TEXT("set-actor-transform"),
        TEXT("Set an actor transform in the editor level."),
        MakeObjectSchema(
            {
                { TEXT("actor"), MakeSchemaProperty(TEXT("string"), TEXT("Actor name or label.")) },
                { TEXT("transform"), MakeSchemaProperty(TEXT("object"), TEXT("Transform object with location, rotation, and scale sub-objects.")) },
                { TEXT("location"), MakeSchemaProperty(TEXT("object"), TEXT("Optional location override with x, y, z.")) },
                { TEXT("rotation"), MakeSchemaProperty(TEXT("object"), TEXT("Optional rotation override with pitch, yaw, roll.")) },
                { TEXT("scale"), MakeSchemaProperty(TEXT("object"), TEXT("Optional scale override with x, y, z.")) }
            },
            { TEXT("actor") }));

    AddDefinition(
        TEXT("set-blueprint-default"),
        TEXT("Set a Blueprint default property via the asset property bridge tool."),
        MakeObjectSchema(
            {
                { TEXT("asset_path"), MakeSchemaProperty(TEXT("string"), TEXT("Blueprint asset path.")) },
                { TEXT("property_name"), MakeSchemaProperty(TEXT("string"), TEXT("Property path on the Blueprint default object.")) },
                { TEXT("value"), MakeSchemaProperty(TEXT("string"), TEXT("Value to assign. Scalars use native JSON values; structs use JSON objects.")) }
            },
            { TEXT("asset_path"), TEXT("property_name"), TEXT("value") }));

    AddDefinition(
        TEXT("start-pie"),
        TEXT("Start Play In Editor."),
        MakeObjectSchema({}, {}),
        EAIGatewayToolConfirmationPolicy::ExplicitApproval);

    AddDefinition(
        TEXT("stop-pie"),
        TEXT("Stop Play In Editor."),
        MakeObjectSchema({}, {}),
        EAIGatewayToolConfirmationPolicy::ExplicitApproval);
}
