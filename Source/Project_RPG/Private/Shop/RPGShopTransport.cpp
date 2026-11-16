#include "Shop/RPGShopTransport.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

FRPGHttpShopTransport::FRPGHttpShopTransport(FContext C,FValidate V,float Timeout)
    :Context(MoveTemp(C)),Validate(MoveTemp(V)),TimeoutSeconds(FMath::IsFinite(Timeout)?FMath::Clamp(Timeout,1.f,30.f):10.f){}
FRPGHttpShopTransport::~FRPGHttpShopTransport(){Cancel();}
bool FRPGHttpShopTransport::GetContext(FString& Endpoint,FString& Account) const
{
    FRPGShopHttpContext C;if(!IsInGameThread()||!Context||!Context(C)||C.Endpoint.IsEmpty()||C.Account.IsEmpty())return false;
    Endpoint=C.Endpoint;Account=C.Account;return true;
}
bool FRPGHttpShopTransport::CanSend() const
{FRPGShopHttpContext C;return IsInGameThread()&&!Active&&Context&&Validate&&Context(C)&&!C.Endpoint.IsEmpty()&&!C.Account.IsEmpty()&&!C.Authorization.IsEmpty();}
bool FRPGHttpShopTransport::ReadShop(FGuid C,FCompletion Done){return Send(C,TEXT(""),TEXT(""),256*1024,MoveTemp(Done));}
bool FRPGHttpShopTransport::SendIntent(const FRPGShopIntent& I,FCompletion Done)
{return I.IsValid()&&Send(I.Scope.Character,TEXT("/transactions"),I.Body,32768,MoveTemp(Done),&I.Scope);}
bool FRPGHttpShopTransport::ReadReceipt(const FRPGShopIntent& I,FCompletion Done)
{return I.IsValid()&&Send(I.Scope.Character,TEXT("/transactions/")+I.RequestId.ToString(EGuidFormats::DigitsWithHyphensLower),TEXT(""),32768,MoveTemp(Done),&I.Scope);}
bool FRPGHttpShopTransport::ReadAssets(const FRPGShopIntent& I,FCompletion Done)
{return I.IsValid()&&Send(I.Scope.Character,TEXT("/transactions/")+I.RequestId.ToString(EGuidFormats::DigitsWithHyphensLower)+TEXT("/assets"),TEXT(""),4*1024*1024,MoveTemp(Done),&I.Scope);}
bool FRPGHttpShopTransport::Send(FGuid Character,const FString& Suffix,const FString& Body,int32 MaximumBytes,FCompletion Done,const FRPGTradeIntentScope* Expected)
{
    FRPGShopHttpContext C;FRPGTradeIntentScope Scope;
    if(!IsInGameThread()||Active||!Done||!Context||!Validate||!Context(C)||C.Authorization.IsEmpty()
        ||!FRPGTradeIntentScope::Create(C.Endpoint,C.Account,Character,Scope)||(Expected&&!(Scope==*Expected)))return false;
    auto Request=FHttpModule::Get().CreateRequest();Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
    Request->SetURL(Scope.Endpoint/TEXT("characters")/Character.ToString(EGuidFormats::DigitsWithHyphensLower)/(TEXT("shop")+Suffix));
    Request->SetVerb(Body.IsEmpty()?TEXT("GET"):TEXT("POST"));Request->SetHeader(TEXT("Authorization"),C.Authorization);
    Request->SetHeader(TEXT("Accept"),TEXT("application/json"));Request->SetHeader(TEXT("Cache-Control"),TEXT("no-cache, no-store"));Request->SetTimeout(TimeoutSeconds);
    if(!Body.IsEmpty()){Request->SetHeader(TEXT("Content-Type"),TEXT("application/json"));Request->SetContentAsString(Body);}
    const uint64 Ticket=++Generation;TWeakPtr<FRPGHttpShopTransport> Weak=AsShared();
    Request->OnProcessRequestComplete().BindLambda([Weak,Ticket,C,MaximumBytes,Done=MoveTemp(Done)](FHttpRequestPtr,FHttpResponsePtr Response,bool Success)mutable
    {
        const auto Self=Weak.Pin();if(!Self||Self->Generation!=Ticket)return;Self->Active.Reset();FRPGShopHttpResult R;
        const bool Received=Success&&Response.IsValid();const int32 Code=Received?Response->GetResponseCode():0;
        R.bCurrentPlayerContext=Self->Validate(C,Code);
        if(R.bCurrentPlayerContext)
        {
            R.bTransportSuccessful=Received;R.StatusCode=Code;
            if(Received&&Response->GetContent().Num()<=MaximumBytes)R.Body=Response->GetContentAsString();
            if(Received&&(Code==429||Code==503))
            {
                const FString Delay=Response->GetHeader(TEXT("Retry-After"));int32 Seconds=0;bool Valid=!Delay.IsEmpty()&&Delay.Len()<=4;
                if(Valid)for(TCHAR Ch:Delay){if(Ch<'0'||Ch>'9'){Valid=false;break;}Seconds=Seconds*10+Ch-'0';}
                R.RetryAfterSeconds=Valid?FMath::Clamp(Seconds,1,3600):60;
            }
        }
        Done(MoveTemp(R));
    });
    Active=Request;if(!Request->ProcessRequest()&&Active==Request)Request->OnProcessRequestComplete().ExecuteIfBound(Request,nullptr,false);return true;
}
void FRPGHttpShopTransport::Cancel()
{++Generation;if(Active){Active->OnProcessRequestComplete().Unbind();Active->CancelRequest();Active.Reset();}}
