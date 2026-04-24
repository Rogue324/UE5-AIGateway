#include "Chat/Services/AIGatewayChatSessionStore.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
    bool SaveJsonObjectToFile(const FString& Path, const TSharedPtr<FJsonObject>& Object)
    {
        if (!Object.IsValid())
        {
            return false;
        }

        FString Output;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
        if (!FJsonSerializer::Serialize(Object.ToSharedRef(), Writer))
        {
            return false;
        }

        return FFileHelper::SaveStringToFile(Output, *Path);
    }

    bool LoadJsonObjectFromFile(const FString& Path, TSharedPtr<FJsonObject>& OutObject)
    {
        FString Input;
        if (!FFileHelper::LoadFileToString(Input, *Path))
        {
            return false;
        }

        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Input);
        return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
    }
}

bool FAIGatewayFileChatSessionStore::LoadSessionIndex(FAIGatewayChatSessionIndex& OutIndex) const
{
    OutIndex = FAIGatewayChatSessionIndex();
    EnsureChatsDirectoryExists();

    TSharedPtr<FJsonObject> IndexObject;
    if (!LoadJsonObjectFromFile(GetSessionIndexPath(), IndexObject))
    {
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* OpenSessionIds = nullptr;
    if (IndexObject->TryGetArrayField(TEXT("open_session_ids"), OpenSessionIds))
    {
        for (const TSharedPtr<FJsonValue>& Value : *OpenSessionIds)
        {
            FString SessionId;
            if (Value.IsValid() && Value->TryGetString(SessionId) && !SessionId.IsEmpty())
            {
                OutIndex.OpenSessionIds.Add(SessionId);
            }
        }
    }

    IndexObject->TryGetStringField(TEXT("active_session_id"), OutIndex.ActiveSessionId);
    return true;
}

bool FAIGatewayFileChatSessionStore::SaveSessionIndex(const FAIGatewayChatSessionIndex& SessionIndex) const
{
    EnsureChatsDirectoryExists();

    TSharedPtr<FJsonObject> IndexObject = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> OpenSessionIds;
    for (const FString& SessionId : SessionIndex.OpenSessionIds)
    {
        OpenSessionIds.Add(MakeShared<FJsonValueString>(SessionId));
    }

    IndexObject->SetArrayField(TEXT("open_session_ids"), OpenSessionIds);
    IndexObject->SetStringField(TEXT("active_session_id"), SessionIndex.ActiveSessionId);
    return SaveJsonObjectToFile(GetSessionIndexPath(), IndexObject);
}

bool FAIGatewayFileChatSessionStore::LoadSession(const FString& SessionId, FAIGatewayChatSession& OutSession) const
{
    TSharedPtr<FJsonObject> SessionObject;
    if (!LoadJsonObjectFromFile(GetSessionPath(SessionId), SessionObject))
    {
        return false;
    }

    OutSession = FAIGatewayChatSession();
    SessionObject->TryGetStringField(TEXT("session_id"), OutSession.SessionId);
    SessionObject->TryGetStringField(TEXT("title"), OutSession.Title);
    SessionObject->TryGetBoolField(TEXT("has_generated_title"), OutSession.bHasGeneratedTitle);
    SessionObject->TryGetStringField(TEXT("draft_prompt"), OutSession.DraftPrompt);

    const TArray<TSharedPtr<FJsonValue>>* PendingImagePathValues = nullptr;
    if (SessionObject->TryGetArrayField(TEXT("pending_image_paths"), PendingImagePathValues) && PendingImagePathValues != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& Value : *PendingImagePathValues)
        {
            FString PendingImagePath;
            if (Value.IsValid() && Value->TryGetString(PendingImagePath) && !PendingImagePath.IsEmpty())
            {
                OutSession.PendingImagePaths.Add(PendingImagePath);
            }
        }
    }
    else
    {
        FString LegacyPendingImagePath;
        if (SessionObject->TryGetStringField(TEXT("pending_image_path"), LegacyPendingImagePath) && !LegacyPendingImagePath.IsEmpty())
        {
            OutSession.PendingImagePaths.Add(LegacyPendingImagePath);
        }
    }

    if (OutSession.SessionId.IsEmpty())
    {
        OutSession.SessionId = SessionId;
    }

    FString CreatedAtText;
    if (SessionObject->TryGetStringField(TEXT("created_at"), CreatedAtText))
    {
        FDateTime::ParseIso8601(*CreatedAtText, OutSession.CreatedAt);
    }

    FString UpdatedAtText;
    if (SessionObject->TryGetStringField(TEXT("updated_at"), UpdatedAtText))
    {
        FDateTime::ParseIso8601(*UpdatedAtText, OutSession.UpdatedAt);
    }

    if (OutSession.CreatedAt == FDateTime())
    {
        OutSession.CreatedAt = FDateTime::UtcNow();
    }

    if (OutSession.UpdatedAt == FDateTime())
    {
        OutSession.UpdatedAt = OutSession.CreatedAt;
    }

    const TArray<TSharedPtr<FJsonValue>>* ConversationValues = nullptr;
    if (SessionObject->TryGetArrayField(TEXT("conversation_messages"), ConversationValues))
    {
        for (const TSharedPtr<FJsonValue>& Value : *ConversationValues)
        {
            const TSharedPtr<FJsonObject>* MessageObject = nullptr;
            if (!Value.IsValid() || !Value->TryGetObject(MessageObject) || MessageObject == nullptr || !(*MessageObject).IsValid())
            {
                continue;
            }

            FAIGatewayChatMessage Message;
            (*MessageObject)->TryGetStringField(TEXT("role"), Message.Role);
            (*MessageObject)->TryGetStringField(TEXT("content"), Message.Content);
            OutSession.ConversationMessages.Add(MoveTemp(Message));
        }
    }

    const TArray<TSharedPtr<FJsonValue>>* RequestValues = nullptr;
    if (SessionObject->TryGetArrayField(TEXT("request_messages"), RequestValues))
    {
        for (const TSharedPtr<FJsonValue>& Value : *RequestValues)
        {
            const TSharedPtr<FJsonObject>* MessageObject = nullptr;
            if (Value.IsValid() && Value->TryGetObject(MessageObject) && MessageObject != nullptr && (*MessageObject).IsValid())
            {
                OutSession.RequestMessages.Add(*MessageObject);
            }
        }
    }

    return true;
}

bool FAIGatewayFileChatSessionStore::SaveSession(const FAIGatewayChatSession& Session) const
{
    EnsureChatsDirectoryExists();

    TSharedPtr<FJsonObject> SessionObject = MakeShared<FJsonObject>();
    SessionObject->SetStringField(TEXT("session_id"), Session.SessionId);
    SessionObject->SetStringField(TEXT("title"), Session.Title);
    SessionObject->SetBoolField(TEXT("has_generated_title"), Session.bHasGeneratedTitle);
    SessionObject->SetStringField(TEXT("created_at"), Session.CreatedAt.ToIso8601());
    SessionObject->SetStringField(TEXT("updated_at"), Session.UpdatedAt.ToIso8601());
    SessionObject->SetStringField(TEXT("draft_prompt"), Session.DraftPrompt);

    TArray<TSharedPtr<FJsonValue>> PendingImagePathValues;
    for (const FString& PendingImagePath : Session.PendingImagePaths)
    {
        if (!PendingImagePath.IsEmpty())
        {
            PendingImagePathValues.Add(MakeShared<FJsonValueString>(PendingImagePath));
        }
    }
    SessionObject->SetArrayField(TEXT("pending_image_paths"), PendingImagePathValues);

    TArray<TSharedPtr<FJsonValue>> ConversationValues;
    for (const FAIGatewayChatMessage& Message : Session.ConversationMessages)
    {
        TSharedPtr<FJsonObject> MessageObject = MakeShared<FJsonObject>();
        MessageObject->SetStringField(TEXT("role"), Message.Role);
        MessageObject->SetStringField(TEXT("content"), Message.Content);
        ConversationValues.Add(MakeShared<FJsonValueObject>(MessageObject));
    }
    SessionObject->SetArrayField(TEXT("conversation_messages"), ConversationValues);

    TArray<TSharedPtr<FJsonValue>> RequestValues;
    for (const TSharedPtr<FJsonObject>& Message : Session.RequestMessages)
    {
        if (Message.IsValid())
        {
            RequestValues.Add(MakeShared<FJsonValueObject>(Message));
        }
    }
    SessionObject->SetArrayField(TEXT("request_messages"), RequestValues);

    return SaveJsonObjectToFile(GetSessionPath(Session.SessionId), SessionObject);
}

void FAIGatewayFileChatSessionStore::DeleteSession(const FString& SessionId) const
{
    IFileManager::Get().Delete(*GetSessionPath(SessionId), false, true, true);
}

FString FAIGatewayFileChatSessionStore::GetChatsDirectory() const
{
    return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AIGatewayEditor"), TEXT("Chats"));
}

FString FAIGatewayFileChatSessionStore::GetSessionIndexPath() const
{
    return FPaths::Combine(GetChatsDirectory(), TEXT("index.json"));
}

FString FAIGatewayFileChatSessionStore::GetSessionPath(const FString& SessionId) const
{
    return FPaths::Combine(GetChatsDirectory(), FString::Printf(TEXT("%s.json"), *SessionId));
}

void FAIGatewayFileChatSessionStore::EnsureChatsDirectoryExists() const
{
    IFileManager::Get().MakeDirectory(*GetChatsDirectory(), true);
}
