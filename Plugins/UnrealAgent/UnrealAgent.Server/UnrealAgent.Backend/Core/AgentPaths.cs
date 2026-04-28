namespace UnrealAgent.Backend.Core;

/// <summary>
/// 프로젝트 경로를 제공하는 정적 클래스입니다.
/// </summary>
public static class AgentPaths
{
    /// <summary>~User/.unrealagent 사용자 설정 디렉터리 경로입니다.</summary>
    public static readonly string UserConfigDir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), ".unrealagent");
}