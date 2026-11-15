#include "Development/RPGSoloRaidSubsystem.h"

#include "AbilitySystemComponent.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Character/RPGBossCharacter.h"
#include "Character/RPGPlayer.h"
#include "Component/Combat/PlayerCombatComponent.h"
#include "Component/Equipment/RPGAuthoritativeEquipmentComponent.h"
#include "Components/CapsuleComponent.h"
#include "Controller/RPGPlayerController.h"
#include "DataAsset/Input/DataAsset_InputConfig.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "GameMode/RPGBossBattleGameMode.h"
#include "GameState/RPGBossBattleGameState.h"
#include "InputActionValue.h"
#include "Item/Definition/RPGItemDefinition.h"
#include "Item/Weapon/RPGPlayerWeapon.h"
#include "Manager/DataManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "RPGGameplayTags.h"
#include "UObject/UnrealType.h"

namespace
{
    float Health(const AActor* Actor)
    {
        const auto* Player = Cast<ARPGBaseCharacter>(Actor);
        const auto* ASC = IsValid(Player) ? Player->GetAbilitySystemComponent() : nullptr;
        return ASC ? ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()) : 0.f;
    }
}

bool URPGSoloRaidSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if UE_BUILD_SHIPPING
    return false;
#else
    const UWorld* World = Cast<UWorld>(Outer);
    // Require both explicit flags, an isolated control file, and no backend assignment.
    FString Path;
    if (!World || World->WorldType != EWorldType::Game ||
        !FParse::Param(FCommandLine::Get(), TEXT("RPGSoloRaid")) ||
        !FParse::Param(FCommandLine::Get(), TEXT("RPGNetTestMode")) ||
        !FParse::Value(FCommandLine::Get(), TEXT("RPGSoloRaidControl="), Path) ||
        !FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_DUNGEON_SESSION_ID")).IsEmpty() ||
        !FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN")).IsEmpty()) return false;
    return FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Path),
        FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Saved/SoloRaid")));
#endif
}

void URPGSoloRaidSubsystem::OnWorldBeginPlay(UWorld& World)
{
    Super::OnWorldBeginPlay(World);
    FParse::Value(FCommandLine::Get(), TEXT("RPGSoloRaidControl="), ControlPath);
    FParse::Value(FCommandLine::Get(), TEXT("RPGSoloRaidBot="), BotIndex);
    if (auto* GameMode = World.GetAuthGameMode<ARPGBossBattleGameMode>())
    {
        // Instance-only: cold clients must not consume the authored 30-second timer.
        if (auto* Time = FindFProperty<FFloatProperty>(GameMode->GetClass(), TEXT("BattleTimeLimit")))
            Time->SetPropertyValue_InContainer(GameMode, 1800.f);
    }
    UE_LOG(LogTemp, Display, TEXT("SOLO_RAID_WORLD Bot=%d NetMode=%d"), BotIndex, int32(World.GetNetMode()));
}

void URPGSoloRaidSubsystem::ReleaseInput()
{
    if (Input.IsValid())
    {
        if (MoveAction.IsValid()) Input->StopContinuousInputInjectionForAction(MoveAction.Get());
        if (AttackAction.IsValid()) Input->StopContinuousInputInjectionForAction(AttackAction.Get());
    }
    Input.Reset(); MoveAction.Reset(); AttackAction.Reset();
}

void URPGSoloRaidSubsystem::Deinitialize()
{
    ReleaseInput();
    Super::Deinitialize();
}

void URPGSoloRaidSubsystem::Tick(float DeltaTime)
{
    const double Now = FPlatformTime::Seconds();
    if (Now >= NextPoll)
    {
        NextPoll = Now + .2;
        FString Command;
        if (FFileHelper::LoadFileToString(Command, *ControlPath))
        {
            Command.TrimStartAndEndInline();
            if (Command != LastCommand)
            {
                FString Verb, Serial;
                Command.Split(TEXT(" "), &Verb, &Serial);
                if (Verb == TEXT("fight") || Verb == TEXT("hold") || Verb == TEXT("spread") ||
                    Verb == TEXT("prepare") || Verb == TEXT("restart"))
                {
                    LastCommand = Command;
                    Mode = Verb;
                    ReleaseInput();
                    if (Verb == TEXT("restart"))
                    {
                        if (auto* GM = GetWorld()->GetAuthGameMode<ARPGBossBattleGameMode>())
                        {
                            if (GM->GetCurrentBossBattleState() == EBossBattleState::InProgress) GM->Defeated();
                            GM->RestartBattle();
                            UE_LOG(LogTemp, Display, TEXT("SOLO_RAID_RESTART State=%d"), int32(GM->GetCurrentBossBattleState()));
                        }
                    }
                    UE_LOG(LogTemp, Display, TEXT("SOLO_RAID_MODE %s"), *Mode);
                }
            }
        }
        else { Mode = TEXT("prepare"); ReleaseInput(); }
    }
    if (GetWorld()->GetNetMode() != NM_Client) TickServer();
    else TickPlayer();
}

void URPGSoloRaidSubsystem::TickServer()
{
    auto* State = GetWorld()->GetGameState<ARPGBossBattleGameState>();
    if (!State) return;
    auto* Boss = State->GetBattleSnapshot().Boss.Get();
    if (Boss != HeldBoss.Get()) { HeldBoss = Boss; bBrainHeld = false; }
    if (auto* AI = IsValid(Boss) ? Cast<AAIController>(Boss->GetController()) : nullptr)
    {
        if (auto* Brain = AI->GetBrainComponent())
        {
            const bool Hold = Mode == TEXT("prepare") || Mode == TEXT("restart");
            if (Hold && Brain->IsRunning()) { Brain->StopLogic(TEXT("Solo raid preparation")); bBrainHeld = true; }
            else if (!Hold && bBrainHeld) { Brain->RestartLogic(); bBrainHeld = false; }
        }
    }
    auto* Catalog = GetWorld()->GetGameInstance()->GetSubsystem<UDataManager>();
    const auto* Definition = LoadObject<URPGItemDefinition>(nullptr,
        TEXT("/Game/Blueprints/GameData/Item/Weapons/DA_Item_GreatSword.DA_Item_GreatSword"));
    if (!Catalog || !Definition) return;
    int32 Players = 0;
    for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        auto* Player = Cast<ARPGPlayer>(It->Get()->GetPawn());
        auto* Equipment = Player ? Player->FindComponentByClass<URPGAuthoritativeEquipmentComponent>() : nullptr;
        if (!Equipment) continue;
        ++Players;
        if (!Equipment->HasWeaponSnapshots() && Equipment->NumAppliedItems() == 0 && Health(Player) > 0)
        {
            FRPGItemOwnerRef Owner;
            Owner.Type = ERPGItemOwnerType::Character;
            Owner.OwnerId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
            FRPGItemLocation Location;
            Location.ContainerType = ERPGItemContainerType::Equipment;
            Location.ContainerId = Owner.OwnerId;
            Location.SlotIndex = int32(EEquipmentSlotType::Weapon_Primary_R);
            FRPGItemInstanceState Instance;
            FRPGItemRecord Record;
            FString Error;
            if (FRPGItemInstanceState::TryRestore(FGuid::NewGuid(), 0, 1, {}, {}, Instance) &&
                FRPGItemRecord::TryRestore(Definition->GetPrimaryAssetId(), Definition->GetDefinitionVersion(),
                    Owner, Location, Instance, 1, ERPGItemLifecycleState::Active, {}, Record) &&
                Equipment->Reconcile(Owner, {Record}, *Catalog, &Error))
                UE_LOG(LogTemp, Display, TEXT("SOLO_RAID_EQUIPPED %s MemoryOnly=1"), *Player->GetName());
        }
    }
    if (FPlatformTime::Seconds() >= NextReport)
    {
        NextReport = FPlatformTime::Seconds() + 5;
        UE_LOG(LogTemp, Display, TEXT("SOLO_RAID_STATUS Players=%d BossHP=%g State=%d"),
            Players, Health(Boss), int32(State->GetBattleSnapshot().BattleState));
    }
}

void URPGSoloRaidSubsystem::TickPlayer()
{
    auto* PC = Cast<ARPGPlayerController>(GetWorld()->GetFirstPlayerController());
    auto* Player = PC ? Cast<ARPGPlayer>(PC->GetPawn()) : nullptr;
    auto* State = GetWorld()->GetGameState<ARPGBossBattleGameState>();
    auto* Boss = State ? State->GetBattleSnapshot().Boss.Get() : nullptr;
    auto* Weapon = Player ? Player->GetPlayerCombatComponent()->GetPlayerCurrentEquippedWeapon() : nullptr;
    const auto* Config = PC ? PC->GetInputConfig() : nullptr;
    auto* Subsystem = PC && PC->GetLocalPlayer() ?
        ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()) : nullptr;
    if (!IsValid(Boss) || !Weapon || !Config || !Subsystem ||
        !Subsystem->HasMappingContext(Weapon->PlayerWeaponData.WeaponInputMappingContext)) { ReleaseInput(); return; }
    if (!bReadyReported)
    {
        bReadyReported = true;
        UE_LOG(LogTemp, Display, TEXT("SOLO_RAID_CLIENT_READY Bot=%d Weapon=1 Mapping=1"), BotIndex);
    }
    if (BotIndex < 0) return; // Human inputs are never injected or released by this subsystem.
    if (Health(Player) <= 0 || Health(Boss) <= 0 ||
        State->GetBattleSnapshot().BattleState != EBossBattleState::InProgress ||
        (Mode != TEXT("fight") && Mode != TEXT("spread"))) { ReleaseInput(); return; }

    const auto* Move = Config->FindNativeInputActionByTag(RPGGameplayTags::InputTag_Move);
    const auto* Attack = Config->FindAbilityInputActionByTag(RPGGameplayTags::InputTag_LightAttack_GreatSword);
    if (!Move || !Attack) { ReleaseInput(); return; }
    if (Input.Get() != Subsystem || MoveAction.Get() != Move || AttackAction.Get() != Attack) ReleaseInput();
    Input = Subsystem; MoveAction = Move; AttackAction = Attack;
    const float Angle = BotIndex * 2.399963f;
    const FVector Offset(FMath::Cos(Angle), FMath::Sin(Angle), 0);
    const float Reach = Player->GetCapsuleComponent()->GetScaledCapsuleRadius() + Boss->GetCapsuleComponent()->GetScaledCapsuleRadius() + 15.f;
    const FVector Goal = Boss->GetActorLocation() + (Mode == TEXT("spread") ? Offset * 600.f : FVector::ZeroVector);
    const FVector Toward = (Goal - Player->GetActorLocation()).GetSafeNormal2D();
    const float Distance = FVector::Dist2D(Goal, Player->GetActorLocation());
    const float StopDistance = Mode == TEXT("spread") ? 50.f : Reach;
    PC->SetControlRotation(FRotator(0, (Boss->GetActorLocation() - Player->GetActorLocation()).Rotation().Yaw, 0));
    const FRotationMatrix Basis(FRotator(0, PC->GetControlRotation().Yaw, 0));
    const FVector2D Value(FVector::DotProduct(Toward, Basis.GetUnitAxis(EAxis::Y)), FVector::DotProduct(Toward, Basis.GetUnitAxis(EAxis::X)));
    if (Distance > StopDistance)
        Subsystem->StartContinuousInputInjectionForAction(Move, FInputActionValue(Value), {}, {});
    else Subsystem->StopContinuousInputInjectionForAction(Move);
    const bool AttackNow = Mode == TEXT("fight") && Distance <= Reach + 35.f &&
        FMath::Fmod(GetWorld()->GetTimeSeconds() + BotIndex * .17f, 1.2f) < .25f;
    if (AttackNow) Subsystem->StartContinuousInputInjectionForAction(Attack, FInputActionValue(true), {}, {});
    else Subsystem->StopContinuousInputInjectionForAction(Attack);
    if (FPlatformTime::Seconds() >= NextReport)
    {
        NextReport = FPlatformTime::Seconds() + 5;
        UE_LOG(LogTemp, Display, TEXT("SOLO_RAID_BOT Index=%d Mode=%s Distance=%g HP=%g BossHP=%g"),
            BotIndex, *Mode, Distance, Health(Player), Health(Boss));
    }
}
