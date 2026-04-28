namespace UnrealAgent.Backend.Core;

/// <summary>
/// 어시스턴트 응답의 콘텐츠 블록입니다.
/// Anthropic SDK 타입 대신 도메인 전체에서 사용합니다.
/// </summary>
public abstract record Block
{
    /// <summary>텍스트 응답 블록입니다.</summary>
    public sealed record Text(string Content) : Block;

    /// <summary>사고 과정(Extended Thinking) 블록입니다.</summary>
    public sealed record Thinking(string Content, string? Signature) : Block;
    
    /// <summary>도구 호출 블록입니다.</summary>
    public sealed record ToolUse(string Id, string Name, string InputJson) : Block;
}