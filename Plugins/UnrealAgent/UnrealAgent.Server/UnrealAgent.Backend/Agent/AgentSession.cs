using UnrealAgent.Backend.Chat;
using UnrealAgent.Backend.Conversation;
using UnrealAgent.Backend.Core;
using UnrealAgent.Backend.Model;

namespace UnrealAgent.Backend.Agent;

/// <summary>
/// 에이전트 세션입니다.
/// 프로세스 아이덴티티, 대화 상태, 미들웨어 파이프라인을 통합합니다.
/// </summary>
public sealed class AgentSession(AgentLoop Loop)
{
    /// <summary>이 세션의 대화 히스토리입니다.</summary>
    public Conversation.Conversation Conversation { get; } = new();
    
    /// <summary>
    /// 사용자 메시지를 처리합니다.
    /// </summary>
    public IAsyncEnumerable<ChatEvent> ProcessMessage(UserInput Input) => Loop.RunAsync(Input, this);
}