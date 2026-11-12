#pragma once
#include "CoreMinimal.h"
#include "Narrative/RPGNarrativeTransportInterface.h"

struct PROJECT_RPG_API FRPGNarrativeSkipQuest
{
    FString Id;
    FText Title;
};
struct PROJECT_RPG_API FRPGNarrativeSkipPreview
{
    FGuid Character;
    FString Chapter, Hash;
    FText Title;
    int64 Revision = 0;
    int32 CatalogVersion = 0;
    TArray<FRPGNarrativeSkipQuest> Quests;
    TArray<FString> Unlocks;
};
struct PROJECT_RPG_API FRPGNarrativeSkipCodec
{
    static bool ValidId(const FString& Id);
    static bool DecodePreview(const FString& Body, FGuid Character, const FString& Chapter, FRPGNarrativeSkipPreview& Out);
    static bool Encode(const FRPGNarrativeSkipPreview& Preview, FGuid Request, FString& Out);
    static bool DecodeCommit(const FString& Body, const FRPGNarrativeSkipPreview& Preview, FRPGNarrativeSnapshot& Out);
};
enum class ERPGNarrativeSkipState : uint8 { Idle, Previewing, Ready, Applying, Uncertain, Failed, Completed };

/** Owner-local operation. Closing a widget does not abandon an uncertain command. */
class PROJECT_RPG_API FRPGNarrativeSkipClient : public TSharedFromThis<FRPGNarrativeSkipClient>
{
public:
    using FContext = TFunction<bool(FGuid, int64&, int32&, bool /*new operation*/)>;
    FRPGNarrativeSkipClient(TSharedRef<IRPGNarrativeTransport> InTransport, FContext InContext,
        TFunction<bool(FGuid, const FString&)> InFinished, TFunction<void(FRPGNarrativeSnapshot)> InCommitted);
    ~FRPGNarrativeSkipClient();
    bool Preview(FGuid Character, const FString& Chapter);
    bool Confirm(FGuid Character);
    bool Retry(FGuid Character);
    void CancelPreview();
    void Reset();
    bool IsSelected(FGuid Character) const;
    bool CanStart(FGuid Character) const;
    bool IsFinished(FGuid Character, const FString& Chapter) const;
    bool IsBusy() const { return State == ERPGNarrativeSkipState::Previewing || State == ERPGNarrativeSkipState::Applying; }
    bool HasPending() const { return !PendingBody.IsEmpty(); }
    int64 GetRevision(FGuid Character) const;
    ERPGNarrativeSkipState GetState(FGuid Character) const;
    const FRPGNarrativeSkipPreview* GetPreview(FGuid Character) const;
    FSimpleMulticastDelegate OnChanged;
private:
    bool Matches() const;
    bool SendPending();
    void ReceivePreview(uint64 Token, FString Chapter, FRPGNarrativeHttpResult Result);
    void ReceiveCommit(uint64 Token, FRPGNarrativeHttpResult Result);
    TSharedRef<IRPGNarrativeTransport> Transport;
    FContext Context;
    TFunction<bool(FGuid, const FString&)> Finished;
    TFunction<void(FRPGNarrativeSnapshot)> Committed;
    FGuid Character;
    FString Endpoint, SteamId, PendingBody;
    TOptional<FRPGNarrativeSkipPreview> Review;
    ERPGNarrativeSkipState State = ERPGNarrativeSkipState::Idle;
    uint64 Generation = 0;
};
