using Microsoft.AspNetCore.Components;
using UnrealAgent.Backend.Conversation;

namespace UnrealAgent.Frontend.UI.Input;

public partial class ChatInput
{
    /// <summary>메시지 전송 콜백입니다.</summary>
    [Parameter] public EventCallback<UserInput> OnSend { get; set; }

    /// <summary>현재 입력 텍스트입니다.</summary>
    private string InputText = "";

    /// <summary>폼 제출 시 메시지를 전송합니다.</summary>
    private async Task HandleSubmit()
    {
        string Trimmed = InputText.Trim();

        if (string.IsNullOrEmpty(Trimmed))
            return;

        InputText = "";
        await OnSend.InvokeAsync(Trimmed);
    }
}