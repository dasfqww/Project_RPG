#include "Online/RPGServiceStatusSubsystem.h"
#include "HttpModule.h"
#include "HAL/PlatformTime.h"

bool URPGServiceStatusSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{ return Super::ShouldCreateSubsystem(Outer) && !IsRunningDedicatedServer(); }
FString URPGServiceStatusSubsystem::Endpoint(const FString& ApiUrl)
{
	FString Base = ApiUrl; Base.RemoveFromEnd(TEXT("/"));
	if ((!Base.StartsWith(TEXT("http://")) && !Base.StartsWith(TEXT("https://"))) || Base.Contains(TEXT("?")) || Base.Contains(TEXT("#")) || Base.Contains(TEXT("@"))) return FString();
	return Base / TEXT("service-status");
}
bool URPGServiceStatusSubsystem::MatchesBackend(const FString& ApiUrl) const { return !StatusUrl.IsEmpty() && StatusUrl == Endpoint(ApiUrl); }
void URPGServiceStatusSubsystem::Cancel()
{
	++Generation;
	if (Active) { Active->OnProcessRequestComplete().Unbind(); Active->CancelRequest(); Active.Reset(); }
}
void URPGServiceStatusSubsystem::Deinitialize() { Cancel(); State = {}; StatusUrl.Reset(); OnChanged.Clear(); Super::Deinitialize(); }
bool URPGServiceStatusSubsystem::Refresh(const FString& ApiUrl, bool bForce)
{
	const FString Url = Endpoint(ApiUrl); const double Now = FPlatformTime::Seconds();
	if (Url != StatusUrl) { Cancel(); StatusUrl = Url; State = {}; NextReadAt = 0; OnChanged.Broadcast(); }
	if (Url.IsEmpty() || Active || (!bForce && Now < NextReadAt)) return false;
	NextReadAt = Now + 15.0;
	Active = FHttpModule::Get().CreateRequest();
	Active->SetURL(Url); Active->SetVerb(TEXT("GET")); Active->SetTimeout(8.f);
	// Deliberately no common/player/server headers and no authentication mutation.
	Active->OnProcessRequestComplete().BindUObject(this, &ThisClass::Complete, Generation);
	if (Active->ProcessRequest()) return true;
	Active.Reset(); State.bReadFailed = true; OnChanged.Broadcast(); return false;
}
void URPGServiceStatusSubsystem::ObserveDraining(const FString& ApiUrl)
{
	const FString Url = Endpoint(ApiUrl); if (Url.IsEmpty()) return;
	if (StatusUrl != Url) { State = {}; StatusUrl = Url; }
	// Fence an earlier Open GET before publishing the authoritative refusal hint.
	Cancel(); State.bRefused = true; State.bReadFailed = true; NextReadAt = 0; OnChanged.Broadcast();
}
void URPGServiceStatusSubsystem::Complete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess, uint64 RequestGeneration)
{
	if (RequestGeneration != Generation || !Request || Request != Active || Request->GetURL() != StatusUrl) return;
	Active.Reset(); FRPGServiceStatusSnapshot Snapshot;
	if (bSuccess && Response && Response->GetResponseCode() == 200 && FRPGServiceStatusSnapshot::Decode(Response->GetContentAsString(), Snapshot))
		State.Accept(Snapshot, FPlatformTime::Seconds());
	else State.bReadFailed = true;
	UE_LOG(LogTemp, Log, TEXT("ServiceStatus observation: %s (HTTP %d)."), State.IsStale(FPlatformTime::Seconds()) ? TEXT("Unknown") : *State.Snapshot.Mode, Response ? Response->GetResponseCode() : 0);
	OnChanged.Broadcast();
}
