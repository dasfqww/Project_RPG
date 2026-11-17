#pragma once
#include "CoreMinimal.h"
#include "Character/RPGPlayer.h"
#include "Character/RPGNonPlayerCharacter.h"
#include "Combat/RPGApprovedHitContext.h"
#include "Components/ActorComponent.h"
#include "GameMode/RPGTrainingGameMode.h"
#include "UI/Content/RPGTrainingGuideWidget.h"
#include "RPGIntroRescue.generated.h"

UENUM() enum class ERPGRescuePhase:uint8 { Waiting, Contribution, Defense, Escape, Complete, Failed };
UENUM() enum class ERPGRescueRole:uint8 { Contribution, Survivor, Threat };
USTRUCT(BlueprintType)
struct FRPGIntroRescueProgress
{
    GENERATED_BODY()
    UPROPERTY() bool bActive=false;
    UPROPERTY() uint32 Attempt=0;
    UPROPERTY() ERPGRescuePhase Phase=ERPGRescuePhase::Waiting;
    UPROPERTY() float Stagger=0;
    UPROPERTY() float Remaining=0;
    UPROPERTY() float SurvivorHealth=0;
    UPROPERTY() bool bContribution=false;
    UPROPERTY() bool bDefense=false;
    UPROPERTY() bool bEscaped=false;
    bool IsComplete() const {return bActive&&Phase==ERPGRescuePhase::Complete&&bContribution&&bDefense&&bEscaped;}
};
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRPGRescueChanged,const FRPGIntroRescueProgress&,Progress);
class ARPGIntroRescueGameMode;

UCLASS()
class PROJECT_RPG_API URPGIntroRescueObjectives:public UActorComponent
{
    GENERATED_BODY()
public:
    URPGIntroRescueObjectives();
    virtual void TickComponent(float Delta,ELevelTick Type,FActorComponentTickFunction* Function) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    FRPGIntroRescueProgress GetProgress() const {return Progress;}
    bool IsReady() const;
    UPROPERTY(BlueprintAssignable) FRPGRescueChanged OnChanged;
private:
    friend class ARPGIntroRescueGameMode;
    friend struct FRPGIntroRescueTestAccess;
    void Update(const FRPGIntroRescueProgress& Value);
    UFUNCTION() void OnRep_Progress();
    UPROPERTY(ReplicatedUsing=OnRep_Progress) FRPGIntroRescueProgress Progress;
    UPROPERTY(Transient) TObjectPtr<UUserWidget> Guide;
};
UCLASS()
class PROJECT_RPG_API ARPGIntroRescuePlayer:public ARPGPlayer
{
    GENERATED_BODY()
public: ARPGIntroRescuePlayer();
};

/** Fixed practice opponents use the normal GAS damage receiver; no loot or boss AI. */
UCLASS()
class PROJECT_RPG_API ARPGIntroRescuePawn:public ARPGNonPlayerCharacter
{
    GENERATED_BODY()
public:
    ARPGIntroRescuePawn();
    void Configure(ERPGRescueRole Value);
    bool Living() const;
    float Health() const;
    ERPGRescueRole GetPracticeRole() const {return PracticeRole;}
    virtual void Tick(float Delta) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
protected:
    virtual void HandleNPCStartUpDataReady() override;
private:
    UPROPERTY(Replicated) ERPGRescueRole PracticeRole=ERPGRescueRole::Contribution;
    double NextAttack=0;
};

UCLASS()
class PROJECT_RPG_API ARPGIntroRescueGameMode:public ARPGTrainingGameMode
{
    GENERATED_BODY()
public:
    ARPGIntroRescueGameMode();
    virtual void BeginPlay() override;
    virtual void Tick(float Delta) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    ARPGIntroRescuePawn* GetSurvivor() const {return Survivor;}
    ARPGIntroRescuePawn* GetContributionTarget() const {return ContributionTarget;}
    ERPGRescuePhase GetPhase() const {return Phase;}
    bool IsCurrentThreat(const ARPGIntroRescuePawn* Pawn) const;
protected:
    virtual const TCHAR* GetPracticeContentId() const override;
    virtual UActorComponent* FindPracticeObjectives(ARPGPlayer* Pawn) const override;
    virtual FPracticeStatus ReadPracticeStatus(const UActorComponent* C) const override;
    virtual void BindPracticeObjectives(UActorComponent* C) override;
    virtual void UnbindPracticeObjectives(UActorComponent* C) override;
    UPROPERTY(EditDefaultsOnly) TSubclassOf<ARPGIntroRescuePawn> PracticePawnClass;
private:
    friend struct FRPGIntroRescueTestAccess;
    UFUNCTION() void Changed(const FRPGIntroRescueProgress& Progress);
    ARPGIntroRescuePawn* SpawnPractice(ERPGRescueRole PracticeRole,FVector Location);
    void ClearAttempt();
    void FailAttempt();
    void SpawnWave(int32 Number);
    UPROPERTY(Transient) TObjectPtr<ARPGIntroRescuePawn> ContributionTarget;
    UPROPERTY(Transient) TObjectPtr<ARPGIntroRescuePawn> Survivor;
    UPROPERTY(Transient) TArray<TObjectPtr<ARPGIntroRescuePawn>> Threats;
    struct FParticipant
    {
        TWeakObjectPtr<ARPGPlayer> Pawn;
        TWeakObjectPtr<URPGIntroRescueObjectives> Objectives;
        FRPGApprovedHitContext Lifetime;
        FString Scope;
        bool bWasInside=false,bEntered=false;
    };
    TArray<FParticipant> Participants;
    ERPGRescuePhase Phase=ERPGRescuePhase::Waiting;
    uint32 Attempt=0;
    double Deadline=0,DefenseStart=0;
    bool bSecondWave=false,bEnding=false;
};

UCLASS()
class PROJECT_RPG_API URPGIntroRescueGuide:public URPGTrainingGuideWidget
{
    GENERATED_BODY()
public: virtual const TCHAR* GetPracticeContentId() const override;
protected:
    virtual int32 GetGoalCount() const override {return 3;}
    virtual bool HasActiveProgress(const APawn* Pawn) const override;
    virtual void ShowCurrentProgress(const ARPGPlayerController& Controller) override;
};
