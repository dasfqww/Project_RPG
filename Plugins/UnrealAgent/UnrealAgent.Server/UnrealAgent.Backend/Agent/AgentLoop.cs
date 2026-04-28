using Anthropic.Models.Messages;
using UnrealAgent.Backend.Auth;
using UnrealAgent.Backend.Chat;
using UnrealAgent.Backend.Conversation;
using UnrealAgent.Backend.Core;
using UnrealAgent.Backend.Prompt;
using UnrealAgent.Backend.Tool;
using Block = UnrealAgent.Backend.Core.Block;

namespace UnrealAgent.Backend.Agent;

/// <summary>
/// 에이전트 루프입니다.
/// Claude API 스트리밍 → 도구 실행을 반복하며, 모든 지능은 모델에 위임합니다.
/// </summary>
public sealed class AgentLoop(PromptBuilder PromptBuilder, ToolExecutor ToolExecutor, AuthConfig Auth)
{
    /// <summary>
    /// 사용자 메시지 1건에 대한 에이전트 루프를 실행합니다.
    /// 호출 1회가 MessageSpan 1개에 대응하며, 모델이 도구를 사용할 때마다 내부에서 API를 반복 호출합니다.
    /// </summary>
    public async IAsyncEnumerable<ChatEvent> RunAsync(UserInput Input, AgentSession Session)
    {
        // 대화 히스토리에 사용자 입력 추가
        MessageSpan CurrentMessageSpan = Session.Conversation.AddMessageSpan(Input);
        
        // 에이전트 루프: 도구 실행이 필요하면 API를 반복 호출
        while (true)
        {
            // API 요청 파라미터 구성
            MessageCreateParams Parameters = PromptBuilder.Build(Session);

            // 스트리밍 응답 수신 및 출력
            ApiStreamSpan ApiStreamSpan = new ApiStreamSpan();
            await foreach (RawMessageStreamEvent Event in Auth.Client!.Messages.CreateStreaming(Parameters))
            {
                if (ApiStreamSpan.Process(Event) is { } Evt)
                {
                    yield return Evt;
                }
            }

            // 완료된 응답을 대화 히스토리에 저장
            switch (ApiStreamSpan.Complete())
            {
                // 정상적으로 완료된 경우
                case ApiStreamSpan.Result.EndSpan { CompletedSpan: { } AssistantSpan }:
                {
                    CurrentMessageSpan.AssistantSpans.Add(AssistantSpan);

                    yield return new ChatEvent.Done();
                    yield break;
                }

                // 도구 결과를 포함하여 다음 API 호출로 이어감
                case ApiStreamSpan.Result.ExecuteTools { CompletedSpan: { } AssistantSpan, ToolCalls: { } ToolCalls }:
                {
                    CurrentMessageSpan.AssistantSpans.Add(AssistantSpan);

                    // 도구 실행
                    foreach (Block.ToolUse ToolCall in ToolCalls)
                    {
                        await foreach (ChatEvent Evt in ToolExecutor.ExecuteAsync(ToolCall, AssistantSpan, Session))
                        {
                            yield return Evt;
                        }
                    }
                    
                    break;
                }

                // 잘린 응답을 이어서 생성
                case ApiStreamSpan.Result.Continue { CompletedSpan: { } AssistantSpan }:
                {
                    CurrentMessageSpan.AssistantSpans.Add(AssistantSpan);
                    break;
                }
            }
        }
    }
}