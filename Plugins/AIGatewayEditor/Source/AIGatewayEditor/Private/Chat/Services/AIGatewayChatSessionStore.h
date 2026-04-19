#pragma once

#include "Chat/Model/AIGatewayChatTypes.h"

class IAIGatewayChatSessionStore
{
public:
    virtual ~IAIGatewayChatSessionStore() = default;

    virtual bool LoadSessionIndex(FAIGatewayChatSessionIndex& OutIndex) const = 0;
    virtual bool SaveSessionIndex(const FAIGatewayChatSessionIndex& SessionIndex) const = 0;
    virtual bool LoadSession(const FString& SessionId, FAIGatewayChatSession& OutSession) const = 0;
    virtual bool SaveSession(const FAIGatewayChatSession& Session) const = 0;
    virtual void DeleteSession(const FString& SessionId) const = 0;
};

class FAIGatewayFileChatSessionStore : public IAIGatewayChatSessionStore
{
public:
    virtual bool LoadSessionIndex(FAIGatewayChatSessionIndex& OutIndex) const override;
    virtual bool SaveSessionIndex(const FAIGatewayChatSessionIndex& SessionIndex) const override;
    virtual bool LoadSession(const FString& SessionId, FAIGatewayChatSession& OutSession) const override;
    virtual bool SaveSession(const FAIGatewayChatSession& Session) const override;
    virtual void DeleteSession(const FString& SessionId) const override;

private:
    FString GetChatsDirectory() const;
    FString GetSessionIndexPath() const;
    FString GetSessionPath(const FString& SessionId) const;
    void EnsureChatsDirectoryExists() const;
};
