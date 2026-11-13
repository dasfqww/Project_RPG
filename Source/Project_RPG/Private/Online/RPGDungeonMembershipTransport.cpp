#include "Online/RPGDungeonMembershipTransport.h"

#include "GenericPlatform/GenericPlatformHttp.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "UObject/Object.h"
#include "UObject/WeakObjectPtr.h"

FRPGDungeonMembershipTransport::~FRPGDungeonMembershipTransport() { Cancel(); }

void FRPGDungeonMembershipTransport::Cancel()
{
	++RequestGeneration;
	if (!ActiveRequest.IsValid()) return;
	ActiveRequest->OnProcessRequestComplete().Unbind();
	ActiveRequest->CancelRequest();
	ActiveRequest.Reset();
}

FHttpRequestPtr FRPGDungeonMembershipTransport::CreateBoundRequest(const FRPGDungeonMembershipTransportConfig& Config,
	const TCHAR* Action, const TCHAR* Verb, TFunction<void(FHttpResponsePtr, bool, bool)> Completion)
{
	FRPGDungeonMembershipMonitor Scope;
	if (bShutdown || IsActive() || !Config.bServerConfigured || !Scope.Configure(Config.SessionId, Config.ServerId)
		|| !Host.HasValidMembershipCredential()) return nullptr;
	const uint64 Generation = ++RequestGeneration;
	const FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
	const TWeakPtr<FRPGDungeonMembershipTransport> WeakSelf = AsShared();
	ActiveRequest = Request;
	Request->OnProcessRequestComplete().BindWeakLambda(&Host.GetMembershipTransportOwner(),
		[WeakSelf, Generation, ExpectedSession = Config.SessionId, ExpectedServer = Config.ServerId,
			Completion = MoveTemp(Completion)](FHttpRequestPtr Completed, FHttpResponsePtr Response, bool bTransport) mutable
		{
			const auto Self = WeakSelf.Pin();
			if (!Self || Self->bShutdown || Generation != Self->RequestGeneration || Completed != Self->ActiveRequest) return;
			Self->ActiveRequest.Reset();
			const auto Current = Self->Host.GetMembershipTransportConfig();
			const bool bSameAssignment = Current.SessionId == ExpectedSession && Current.ServerId == ExpectedServer;
			Completion(Response, bTransport, bSameAssignment);
		});
	Request->SetURL(Config.ApiUrl / FString::Printf(TEXT("dungeon-sessions/%s/%s"),
		*FGenericPlatformHttp::UrlEncode(Config.SessionId), Action));
	Request->SetVerb(Verb);
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Request->SetHeader(TEXT("Cache-Control"), TEXT("no-cache"));
	// Never use the common player-preferred headers at this authority boundary.
	Host.ApplyMembershipAuthorization(Request);
	Request->SetTimeout(FMath::IsFinite(Config.TimeoutSeconds) ? FMath::Clamp(Config.TimeoutSeconds, 1.f, 30.f) : 10.f);
	return Request;
}

FHttpRequestPtr FRPGDungeonMembershipTransport::CreateSnapshotRequest(
	TFunction<void(FRPGDungeonMembershipSnapshotResult)> Completion)
{
	const auto Config = Host.GetMembershipTransportConfig();
	return CreateBoundRequest(Config, TEXT("membership-snapshot"), TEXT("GET"),
		[ExpectedSession = Config.SessionId, ExpectedServer = Config.ServerId, Completion = MoveTemp(Completion)]
		(FHttpResponsePtr Response, bool bTransport, bool bSameAssignment) mutable
		{
			FRPGDungeonMembershipSnapshotResult Result;
			Result.HttpStatus = Response.IsValid() ? Response->GetResponseCode() : 0;
			if (bSameAssignment && bTransport && Response.IsValid() && Response->GetContentLength() <= 64 * 1024
				&& Response->GetContent().Num() <= 64 * 1024)
				Result.bSuccess = FRPGDungeonMembershipSnapshotCodec::Decode(Response->GetContentAsString(),
					Result.HttpStatus, ExpectedSession, ExpectedServer, Result.Snapshot);
			if (Completion) Completion(MoveTemp(Result));
		});
}

FHttpRequestPtr FRPGDungeonMembershipTransport::CreateExpirationRequest(TFunction<void()> Completion)
{
	return CreateBoundRequest(Host.GetMembershipTransportConfig(), TEXT("disconnection-expirations"), TEXT("POST"),
		[Completion = MoveTemp(Completion)](FHttpResponsePtr, bool, bool bSameAssignment) mutable
		{
			// Any POST outcome still requires the grace-window GET. A POST is never ejection evidence.
			if (bSameAssignment && Completion) Completion();
		});
}

bool FRPGDungeonMembershipTransport::Dispatch(const FHttpRequestPtr& Request)
{
	if (!Request.IsValid()) return false;
	if (Request->ProcessRequest()) return true;
	if (ActiveRequest == Request) Cancel();
	return false;
}

bool FRPGDungeonMembershipTransport::LoadSnapshot(TFunction<void(FRPGDungeonMembershipSnapshotResult)> Completion)
{
	if (!Host.GetMembershipTransportConfig().bServerRuntime) return false;
	return Dispatch(CreateSnapshotRequest(MoveTemp(Completion)));
}

bool FRPGDungeonMembershipTransport::ProcessExpirations(TFunction<void()> Completion)
{
	if (!Host.GetMembershipTransportConfig().bServerRuntime) return false;
	return Dispatch(CreateExpirationRequest(MoveTemp(Completion)));
}
