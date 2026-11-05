using ProjectRpg.Operations.Core.Common;

namespace ProjectRpg.Operations.Core.Access;

public enum OpsCapability { ReadCharacter, ReadDiagnostics, ReadEvidence, Compensate, RestrictAccount }
public sealed record OperatorActor(string Id);
public interface IOperatorAccess
{
    Task<Result<OperatorActor>> AuthorizeAsync(OpsCapability capability, CancellationToken cancellationToken);
}
