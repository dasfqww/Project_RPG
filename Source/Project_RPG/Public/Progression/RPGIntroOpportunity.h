#pragma once
#include "CoreMinimal.h"
#include "Character/RPGPlayer.h"
#include "Character/RPGNonPlayerCharacter.h"
#include "Combat/RPGApprovedHitContext.h"
#include "Components/ActorComponent.h"
#include "GameMode/RPGTrainingGameMode.h"
#include "UI/Content/RPGTrainingGuideWidget.h"
#include "RPGIntroOpportunity.generated.h"

class ARPGIntroOpportunityTarget;
struct FGameplayEffectModCallbackData;

UENUM()
enum class ERPGOpportunityPhase : uint8 { Waiting, Telegraph, Open, Recovery };

USTRUCT(BlueprintType)
struct FRPGIntroOpportunityProgress
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) bool bActive = false;
    UPROPERTY(BlueprintReadOnly) int32 Completed = 0;
    bool IsComplete() const { return bActive && Completed == 2; }
};
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRPGIntroOpportunityChanged, const FRPGIntroOpportunityProgress&, Progress);

/** Per-Pawn, owner-only current-attempt facts. Only the authoritative damage receiver can credit them. */
UCLASS()
class PROJECT_RPG_API URPGIntroOpportunityObjectiveComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    URPGIntroOpportunityObjectiveComponent();
    virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Function) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    FRPGIntroOpportunityProgress GetProgress() const { return Progress; }
    UPROPERTY(BlueprintAssignable) FRPGIntroOpportunityChanged OnProgressChanged;
private:
    friend class ARPGIntroOpportunityTarget;
    friend struct FRPGIntroOpportunityTestAccess;
    bool Eligible() const;
    void RefreshScope();
    bool Credit(ARPGIntroOpportunityTarget& Target, uint32 Round);
    void Publish();
    UFUNCTION() void OnRep_Progress();
    UPROPERTY(ReplicatedUsing=OnRep_Progress) FRPGIntroOpportunityProgress Progress;
    UPROPERTY(Transient) TObjectPtr<UUserWidget> Guide;
    TWeakObjectPtr<ARPGPlayerState> ObservedState;
    TWeakObjectPtr<AController> ObservedController;
    TWeakObjectPtr<ARPGIntroOpportunityTarget> LastTarget;
    FString Session, Character, Account;
    int64 Connection = 0;
    uint32 Restore = 0, LastRound = 0;
    FRPGApprovedHitContext Lifetime;
};

UCLASS()
class PROJECT_RPG_API ARPGIntroOpportunityPlayer : public ARPGPlayer
{
    GENERATED_BODY()
public:
    ARPGIntroOpportunityPlayer();
};

/** A stationary, non-looting practice opponent; all hits still use normal GAS damage approval. */
UCLASS()
class PROJECT_RPG_API ARPGIntroOpportunityTarget : public ARPGNonPlayerCharacter
{
    GENERATED_BODY()
public:
    ARPGIntroOpportunityTarget();
    virtual void Tick(float Delta) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    bool IsWindowOpen() const;
    ERPGOpportunityPhase GetPhase() const { return Phase; }
    uint32 GetRound() const { return Round; }
    double GetSecondsRemaining() const;
    void ObserveDamage(const FGameplayEffectModCallbackData& Data, float Before, float After);
protected:
    virtual void HandleNPCStartUpDataReady() override;
private:
    friend struct FRPGIntroOpportunityTestAccess;
    UPROPERTY(Replicated) ERPGOpportunityPhase Phase = ERPGOpportunityPhase::Waiting;
    UPROPERTY(Replicated) uint32 Round = 0;
    UPROPERTY(Replicated) double Deadline = 0;
};

UCLASS()
class PROJECT_RPG_API ARPGIntroOpportunityGameMode : public ARPGTrainingGameMode
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override;
    ARPGIntroOpportunityTarget* GetTarget() const { return Target; }
protected:
    virtual const TCHAR* GetPracticeContentId() const override;
    virtual UActorComponent* FindPracticeObjectives(ARPGPlayer* Pawn) const override;
    virtual FPracticeStatus ReadPracticeStatus(const UActorComponent* Objectives) const override;
    virtual void BindPracticeObjectives(UActorComponent* Objectives) override;
    virtual void UnbindPracticeObjectives(UActorComponent* Objectives) override;
    UPROPERTY(EditDefaultsOnly) TSubclassOf<ARPGIntroOpportunityTarget> TargetClass;
private:
    UFUNCTION() void Changed(const FRPGIntroOpportunityProgress& Progress);
    UPROPERTY(Transient) TObjectPtr<ARPGIntroOpportunityTarget> Target;
};

UCLASS()
class PROJECT_RPG_API URPGIntroOpportunityGuideWidget : public URPGTrainingGuideWidget
{
    GENERATED_BODY()
public:
    virtual const TCHAR* GetPracticeContentId() const override;
protected:
    virtual int32 GetGoalCount() const override { return 2; }
    virtual bool HasActiveProgress(const APawn* Pawn) const override;
    virtual void ShowCurrentProgress(const ARPGPlayerController& Controller) override;
};
