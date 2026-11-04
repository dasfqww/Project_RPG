namespace ProjectRpg.Operations.Core.Common;

public enum FailureKind { InvalidInput, Unauthenticated, Forbidden, NotFound, Unavailable, CursorExpired, Conflict, Internal }

public sealed record Failure(FailureKind Kind, string Code);

public sealed class Result<T> where T : notnull
{
    private readonly T? value;
    public Failure? Error { get; }
    public bool IsSuccess => Error is null;
    public T Value => IsSuccess ? value! : throw new InvalidOperationException("A failed result has no value.");
    private Result(T value) { ArgumentNullException.ThrowIfNull(value); this.value = value; }
    private Result(Failure error) => Error = error;
    public static Result<T> Success(T value) => new(value);
    public static Result<T> Fail(Failure error) => new(error ?? throw new ArgumentNullException(nameof(error)));
}

public static class Failures
{
    public static readonly Failure InvalidQuery = new(FailureKind.InvalidInput, "ops.invalid_query");
    public static readonly Failure InvalidCursor = new(FailureKind.InvalidInput, "ops.invalid_cursor");
    public static readonly Failure CursorExpired = new(FailureKind.CursorExpired, "ops.cursor_expired");
    public static readonly Failure Unauthenticated = new(FailureKind.Unauthenticated, "ops.authentication_required");
    public static readonly Failure Forbidden = new(FailureKind.Forbidden, "ops.access_denied");
    public static readonly Failure CharacterMissing = new(FailureKind.NotFound, "ops.character_not_found");
    public static readonly Failure StorageUnavailable = new(FailureKind.Unavailable, "ops.storage_unavailable");
    public static readonly Failure LogsUnavailable = new(FailureKind.Unavailable, "ops.log_source_unavailable");
}

public sealed record PagePosition(Guid Id, DateTimeOffset? Timestamp = null);
public sealed record PageRequest(int Limit = 50, PagePosition? After = null)
{
    public bool IsValid(int maximum = 200) => Limit >= 1 && Limit <= maximum && (After is null || After.Id != Guid.Empty);
}
public sealed record Page<T>(IReadOnlyList<T> Items, bool HasMore, PagePosition? Next);
