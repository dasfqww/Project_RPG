using Microsoft.AspNetCore.Components;
using UnrealAgent.Backend.Agent;
using UnrealAgent.Backend.Chat;

namespace UnrealAgent.Frontend.Page;

public partial class Chat : IAsyncDisposable
{
    /// <summary>에이전트 실행 서비스입니다.</summary>
    [Inject] private AgentRunner AgentRunner { get; set; } = null!;
    
    /// <summary>설정 패널 표시 여부입니다.</summary>
    private bool bShowSettings;

    /// <summary>설정 패널을 토글합니다.</summary>
    private void ToggleSettings() => bShowSettings = !bShowSettings;

    /// <summary>플랜 사용량 패널 표시 여부입니다.</summary>
    private bool bShowUsage;

    /// <summary>플랜 사용량 패널을 토글합니다.</summary>
    private void ToggleUsage() => bShowUsage = !bShowUsage;

    protected override void OnInitialized()
    {
        AgentRunner.OnChatEvent = OnChatEvent;
    }

    public async ValueTask DisposeAsync()
    {
        if(AgentRunner.OnChatEvent==OnChatEvent)
            AgentRunner.OnChatEvent =null;
        
        await ValueTask.CompletedTask;
    }
    
    /// <summary>
    /// AgentRunner의 ChatEvent를 UI 스레드에서 처리합니다.
    /// Store 수정과 렌더링이 같은 스레드에서 실행되어 스레드 안전성을 보장합니다.
    /// </summary>
    private Task OnChatEvent(ChatEvent Evt) => InvokeAsync(() =>
    {
        AgentRunner.Store.Process(Evt);
        
        // 변경된 상태를 Blazor에 렌더링 요청
        StateHasChanged();
    });
}