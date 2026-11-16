#include "Development/RPGSoloRaidControl.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Development/RPGSoloRaidSubsystem.h"
#include "Development/RPGSoloRaidBot.h"
#include "Development/RPGSoloRaidPanel.h"
#include "Character/RPGBossCharacter.h"
#include "Character/RPGPlayer.h"
#include "AI/RPGBossSummonObjectiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameState/RPGBossBattleGameState.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

bool RPGSoloRaidControl::IsMode(const FString& Mode)
{
    return Mode == TEXT("prepare") || Mode == TEXT("fight") || Mode == TEXT("hold") ||
        Mode == TEXT("gather") || Mode == TEXT("spread") || Mode == TEXT("restart") || Mode == TEXT("quit");
}
bool RPGSoloRaidControl::IsPolicy(const FString& Policy)
{
    return Policy == TEXT("assist") || Policy == TEXT("hold") || Policy == TEXT("ignore-mechanic") || Policy == TEXT("no-avoid");
}
bool RPGSoloRaidControl::ReadCommand(const FString& Path, FString& Mode, FString& Identity)
{
    const int64 Size = IFileManager::Get().FileSize(*Path);
    FString Text;
    if (Size < 1 || Size > 256 || !FFileHelper::LoadFileToString(Text, *Path)) return false;
    TArray<FString> Parts;
    Text.ParseIntoArrayWS(Parts);
    if (Parts.Num() != 2 || !IsMode(Parts[0]) || Parts[1].Len() > 64) return false;
    Mode = Parts[0]; Identity = Text;
    return true;
}
bool RPGSoloRaidControl::WriteAtomic(const FString& Path, const FString& Text)
{
    const FString Temp = Path + TEXT(".") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".tmp");
    if (!FFileHelper::SaveStringToFile(Text, *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) return false;
    const bool Written = IFileManager::Get().Move(*Path, *Temp, true, true, false, true);
    if (!Written) IFileManager::Get().Delete(*Temp);
    return Written;
}
bool RPGSoloRaidControl::WriteCommand(const FString& Path, const FString& Mode)
{
    return IsMode(Mode) && WriteAtomic(Path, Mode + TEXT(" ") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
}
FString RPGSoloRaidControl::ReadPolicy(const FString& Directory, const int32 Slot)
{
    if (Slot < 0 || Slot > 3) return TEXT("hold");
    const FString Path = Directory / FString::Printf(TEXT("bot%d.txt"), Slot);
    if (!IFileManager::Get().FileExists(*Path)) return TEXT("assist");
    FString Value;
    if (IFileManager::Get().FileSize(*Path) > 64 || !FFileHelper::LoadFileToString(Value, *Path)) return TEXT("hold");
    Value.TrimStartAndEndInline();
    return IsPolicy(Value) ? Value : TEXT("hold");
}
bool RPGSoloRaidControl::WritePolicy(const FString& Directory, const int32 Slot, const FString& Policy)
{
    return Slot >= 0 && Slot <= 3 && IsPolicy(Policy) &&
        WriteAtomic(Directory / FString::Printf(TEXT("bot%d.txt"), Slot), Policy);
}

bool URPGSoloRaidSubsystem::RequestMode(const FString& NewMode)
{
    if (!IsHumanLeader() || !bReadyReported || !RPGSoloRaidControl::IsMode(NewMode)) return false;
    const bool Sent = RPGSoloRaidControl::WriteCommand(ControlPath, NewMode);
    ControlError = Sent ? FString() : TEXT("명령 전달 실패. 다시 눌러주세요.");
    UE_LOG(LogTemp, Display, TEXT("SOLO_RAID_UI_COMMAND Mode=%s Sent=%d"), *NewMode, int32(Sent));
    return Sent;
}
bool URPGSoloRaidSubsystem::SetBotPolicy(int32 Slot, const FString& Value)
{
    if (!IsHumanLeader() || Slot <= 0 || Slot >= PartySize || !RPGSoloRaidControl::IsPolicy(Value)) return false;
    const bool Sent = RPGSoloRaidControl::WritePolicy(FPaths::GetPath(ControlPath), Slot, Value);
    if (Sent && SlotPolicies.IsValidIndex(Slot)) SlotPolicies[Slot] = Value;
    ControlError = Sent ? FString() : TEXT("봇 설정 전달 실패. 다시 눌러주세요.");
    UE_LOG(LogTemp, Display, TEXT("SOLO_RAID_UI_POLICY Slot=%d Policy=%s Sent=%d"), Slot, *Value, int32(Sent));
    return Sent;
}
FString URPGSoloRaidSubsystem::GetBotPolicy(int32 Slot) const
{
    return SlotPolicies.IsValidIndex(Slot) ? SlotPolicies[Slot] : TEXT("hold");
}
FString URPGSoloRaidSubsystem::GetSlotStatus(int32 Slot) const
{
    return SlotStatuses.IsValidIndex(Slot) ? SlotStatuses[Slot] : TEXT("접속 대기");
}
void URPGSoloRaidSubsystem::UpdateObservation()
{
    const auto* PC = GetWorld()->GetFirstPlayerController();
    const auto* Player = PC ? Cast<ARPGPlayer>(PC->GetPawn()) : nullptr;
    const double Now = double(FDateTime::UtcNow().ToUnixTimestamp());
    TSharedRef<FJsonObject> Status = MakeShared<FJsonObject>();
    Status->SetNumberField(TEXT("slot"), PlayerSlot);
    Status->SetBoolField(TEXT("bot"), BotIndex >= 0);
    Status->SetBoolField(TEXT("ready"), bPlayerReady);
    Status->SetStringField(TEXT("pawn"), GetNameSafe(Player));
    Status->SetNumberField(TEXT("playerId"), Player && Player->GetPlayerState() ? Player->GetPlayerState()->GetPlayerId() : INDEX_NONE);
    Status->SetNumberField(TEXT("hp"), RPGSoloRaidBot::Health(Player));
    Status->SetNumberField(TEXT("time"), Now);
    Status->SetStringField(TEXT("activity"), Activity);
    Status->SetStringField(TEXT("mode"), Mode);
    Status->SetStringField(TEXT("policy"), Policy);
    Status->SetBoolField(TEXT("panel"), Panel && Panel->IsInViewport());
    Status->SetBoolField(TEXT("moveLocked"), PC && PC->IsMoveInputIgnored());
    if (Player)
    {
        Status->SetNumberField(TEXT("x"), Player->GetActorLocation().X);
        Status->SetNumberField(TEXT("y"), Player->GetActorLocation().Y);
    }
    FString Text;
    FJsonSerializer::Serialize(Status, TJsonWriterFactory<>::Create(&Text));
    const FString Directory = FPaths::GetPath(ControlPath);
    RPGSoloRaidControl::WriteAtomic(Directory / FString::Printf(TEXT("status%d.json"), PlayerSlot), Text);
    LeaderPlayerId = INDEX_NONE;
    for (int32 Slot = 0; Slot < PartySize; ++Slot)
    {
        SlotPolicies[Slot] = RPGSoloRaidControl::ReadPolicy(Directory, Slot);
        const FString Name = Slot == 0 ? TEXT("내 캐릭터") : FString::Printf(TEXT("봇 %d"), Slot);
        SlotStatuses[Slot] = Name + TEXT(": 접속 대기 / 응답 없음");
        const FString Path = Directory / FString::Printf(TEXT("status%d.json"), Slot);
        FString InputText;
        const int64 Size = IFileManager::Get().FileSize(*Path);
        TSharedPtr<FJsonObject> Observed;
        if (Size < 1 || Size > 4096 || !FFileHelper::LoadFileToString(InputText, *Path) ||
            !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(InputText), Observed) || !Observed) continue;
        double Time = 0, HP = 0, ReadSlot = -1;
        FString Action;
        if (!Observed->TryGetNumberField(TEXT("time"), Time) || !FMath::IsFinite(Time) || Time > Now + 1 || Now - Time > 5 ||
            !Observed->TryGetNumberField(TEXT("hp"), HP) || !FMath::IsFinite(HP) ||
            !Observed->TryGetNumberField(TEXT("slot"), ReadSlot) || ReadSlot != Slot ||
            !Observed->TryGetStringField(TEXT("activity"), Action)) continue;
        SlotStatuses[Slot] = FString::Printf(TEXT("%s: HP %.0f | %s"), *Name, HP, *Action.Left(64));
        double ObservedId = -1;
        if (Slot == 0 && Observed->TryGetNumberField(TEXT("playerId"), ObservedId) && FMath::IsFinite(ObservedId) &&
            ObservedId >= 0 && ObservedId <= MAX_int32 && FMath::FloorToDouble(ObservedId) == ObservedId)
            LeaderPlayerId = int32(ObservedId);
    }
    const auto* State = GetWorld()->GetGameState<ARPGBossBattleGameState>();
    auto* Boss = State ? State->GetBattleSnapshot().Boss.Get() : nullptr;
    Overview = TEXT("파티 준비 중. 준비가 끝나면 공격을 눌러 시작하세요.");
    if (IsValid(Boss))
    {
        const auto Battle = State->GetBattleSnapshot().BattleState;
        const TCHAR* StateName = Battle == EBossBattleState::Victory ? TEXT("승리") :
            (Battle == EBossBattleState::Defeated ? TEXT("실패 — 재시작 가능") : TEXT("진행 / 준비"));
        Overview = FString::Printf(TEXT("보스 HP %.0f | %s | 지시: %s"), RPGSoloRaidBot::Health(Boss), StateName, *Mode);
        const auto Objective = Boss->BossSummonObjectiveComponent->GetObjectiveState();
        const auto BossState = Boss->BossAIComponent->GetBossState();
        if (Objective.bActive && Objective.EncounterId == BossState.EncounterId && Objective.PhaseIndex == BossState.PhaseIndex)
            Overview += FString::Printf(TEXT("\n소환 기믹 %d/%d | 남은 시간 %.0f초"), Objective.DefeatedCount, Objective.RequiredCount,
                FMath::Max(0., BossState.PhaseEndServerTime - State->GetServerWorldTimeSeconds()));
    }
    if (!ControlError.IsEmpty()) Overview += TEXT("\n") + ControlError;
}
