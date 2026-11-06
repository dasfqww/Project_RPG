#include "Online/RPGServiceStatus.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	bool Utc(const FString& Value, FDateTime& Out)
	{
		return (Value.EndsWith(TEXT("Z")) || Value.EndsWith(TEXT("+00:00")))
			&& FDateTime::ParseIso8601(*Value, Out);
	}
	bool OptionalTime(const FJsonObject& Object, const TCHAR* Name, TOptional<FDateTime>& Out)
	{
		const auto* Field = Object.Values.Find(Name);
		if (!Field) return false;
		if ((*Field)->IsNull()) { Out.Reset(); return true; }
		FString Value; FDateTime Time;
		if (!(*Field)->TryGetString(Value) || !Utc(Value, Time)) return false;
		Out = Time; return true;
	}
}
bool FRPGServiceStatusSnapshot::Decode(const FString& Body, FRPGServiceStatusSnapshot& Out)
{
	if (Body.Len() > 16384) return false;
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Root) || !Root.IsValid()) return false;
	double Version = 0; const TSharedPtr<FJsonObject>* Data = nullptr;
	if (!Root->TryGetNumberField(TEXT("contractVersion"), Version) || Version != 1
		|| !Root->TryGetObjectField(TEXT("data"), Data) || !Data || !Data->IsValid()) return false;
	FRPGServiceStatusSnapshot Parsed; FString Observed; bool Visible = false;
	if (!(*Data)->TryGetStringField(TEXT("mode"), Parsed.Mode) || (Parsed.Mode != TEXT("Open") && Parsed.Mode != TEXT("Drain"))
		|| !(*Data)->TryGetStringField(TEXT("noticeText"), Parsed.Notice) || Parsed.Notice.Len() > 1000
		|| !(*Data)->TryGetStringField(TEXT("observedAtUtc"), Observed) || !Utc(Observed, Parsed.ObservedAt)
		|| !(*Data)->TryGetBoolField(TEXT("noticeVisible"), Visible)
		|| !OptionalTime(**Data, TEXT("noticeStartsAtUtc"), Parsed.NoticeStartsAt)
		|| !OptionalTime(**Data, TEXT("noticeEndsAtUtc"), Parsed.NoticeEndsAt)) return false;
	if (Parsed.NoticeStartsAt.IsSet() && Parsed.NoticeEndsAt.IsSet() && Parsed.NoticeStartsAt.GetValue() >= Parsed.NoticeEndsAt.GetValue()) return false;
	if (Visible != Parsed.IsNoticeVisible(Parsed.ObservedAt)) return false;
	Out = MoveTemp(Parsed); return true;
}
bool FRPGServiceStatusSnapshot::IsNoticeVisible(FDateTime Now) const
{
	return !Notice.IsEmpty() && (!NoticeStartsAt.IsSet() || NoticeStartsAt.GetValue() <= Now)
		&& (!NoticeEndsAt.IsSet() || Now < NoticeEndsAt.GetValue());
}
bool FRPGServiceStatusSnapshot::IsDrainResponse(int32 Code, const FString& Body)
{
	if (Code != 503 || Body.Len() > 4096) return false;
	TSharedPtr<FJsonObject> Value; FString Error;
	return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Value) && Value.IsValid()
		&& Value->TryGetStringField(TEXT("error"), Error) && Error == TEXT("service_draining");
}
void FRPGServiceStatusState::Accept(const FRPGServiceStatusSnapshot& Value, double Now)
{ Snapshot = Value; ReceivedAt = Now; bKnown = true; bReadFailed = bRefused = false; }
bool FRPGServiceStatusState::IsStale(double Now) const { return !bKnown || bReadFailed || Now < ReceivedAt || Now - ReceivedAt > 45.0; }
FText FRPGServiceStatusState::Text(double Now) const
{
	FString Result;
	if (IsDraining()) Result = NSLOCTEXT("RPGServiceStatus", "Drain", "점검 준비 중입니다. 신규 로그인·파티 입장·출발은 잠시 기다려 주세요. 기존 전투와 보상 처리는 유지됩니다.").ToString();
	if (IsStale(Now))
	{
		if (!Result.IsEmpty()) Result += TEXT("\n");
		Result += NSLOCTEXT("RPGServiceStatus", "Unknown", "최신 서비스 상태를 확인하지 못했습니다. 상태를 다시 확인 중입니다.").ToString();
	}
	else if (Snapshot.IsNoticeVisible(Snapshot.ObservedAt + FTimespan::FromSeconds(FMath::Max(0.0, Now - ReceivedAt))))
	{
		if (!Result.IsEmpty()) Result += TEXT("\n");
		Result += Snapshot.Notice;
	}
	return FText::FromString(Result);
}
bool FRPGServiceStatusState::IsNewAdmissionAction(FName Name)
{
	static const TSet<FName> Names = {TEXT("LoginSteamButton"), TEXT("LoginDevelopmentButton"), TEXT("CreateDungeonSessionButton"),
		TEXT("EnterDungeonButton"), TEXT("RequestDungeonDepartureButton"), TEXT("JoinDevelopmentSessionButton"),
		TEXT("CreateSteamPartyButton"), TEXT("JoinSteamPartyButton"), TEXT("AcceptSteamInviteButton"), TEXT("PublishSteamServerButton"), TEXT("StartTutorialButton")};
	return Names.Contains(Name);
}
