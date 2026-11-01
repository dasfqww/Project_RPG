#pragma once

#include "Blueprint/UserWidget.h"
#include "Online/RPGDungeonPartyCombatTypes.h"
#include "RPGDungeonPartyCombatWidget.generated.h"

class UTextBlock;
class UVerticalBox;
class UProgressBar;

/** Passive owner HUD; no input mode, focus, gameplay action or membership mutation. */
UCLASS(Blueprintable)
class PROJECT_RPG_API URPGDungeonPartyCombatWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	void SetPartySnapshot(const FRPGDungeonPartyCombatSnapshot& Snapshot, double ServerWorldTime = 0.0);
	/** Presentation only. These rows contain no peer backend/account identities. */
	UFUNCTION(BlueprintPure, Category = "RPG|Dungeon|Party")
	TArray<FRPGDungeonPartyCombatMember> GetPartyMembers() const { return Members; }
	UFUNCTION(BlueprintPure, Category = "RPG|Dungeon|Party")
	TArray<FRPGDungeonPartyNotice> GetActivePartyNotices() const;
	UFUNCTION(BlueprintPure, Category = "RPG|Dungeon|Party")
	FText GetPartyNoticeText(const FRPGDungeonPartyNotice& Notice) const;
	UFUNCTION(BlueprintPure, Category = "RPG|Dungeon|Party")
	FText GetPartyHeaderText() const;
	UFUNCTION(BlueprintPure, Category = "RPG|Dungeon|Party")
	FText GetPartyMemberText(const FRPGDungeonPartyCombatMember& Member) const;
	UFUNCTION(BlueprintPure, Category = "RPG|Dungeon|Party")
	float GetPartyHealthFraction(const FRPGDungeonPartyCombatMember& Member) const;
	UFUNCTION(BlueprintPure, Category = "RPG|Dungeon|Party")
	bool IsLastKnownPartyRoster() const { return bLastKnownRoster; }
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	/** Called after construction and each owner refresh, including countdown-only updates. */
	UFUNCTION(BlueprintImplementableEvent, Category = "RPG|Dungeon|Party")
	void OnPartyPresentationUpdated();
private:
	void RefreshMembers();
	UPROPERTY(Transient) TArray<FRPGDungeonPartyCombatMember> Members;
	UPROPERTY(Transient) TArray<FRPGDungeonPartyNotice> Notices;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Header;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ScopeNote;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> NoticeFeed;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> Rows;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> MemberLabels;
	UPROPERTY(Transient) TArray<TObjectPtr<UProgressBar>> HealthBars;
	double DisplayServerWorldTime = 0.0;
	bool bLastKnownRoster = true;
	bool bPresentationConstructed = false;
};
