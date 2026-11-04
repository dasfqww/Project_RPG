using System.Security.Cryptography;
using System.Text;
using ProjectRpg.Backend.Domain;
using ProjectRpg.Backend.Operations.Hosting;
using ProjectRpg.Operations.Core.Compensation;

namespace ProjectRpg.Backend.Operations.Infrastructure.Compensation;

public sealed class CompensationOptions
{
    public bool Enabled { get; set; }
    public string[] AllowedDefinitions { get; set; } = [];
}
/// <summary>Uses the existing first-slice +0 equipment/material creation recipe. No client supplied stats or seed.</summary>
public sealed class CompensationCatalog : ICompensationPolicy
{
    public static string Key(string name) => $"{FirstSliceEquipmentDefinitions.DefinitionType}:{name}:1";
    private static readonly IReadOnlyDictionary<string, string> supported = FirstSliceEquipmentDefinitions.All.Select(d => d.DefinitionName)
        .Concat([FirstSliceEquipmentDefinitions.OreDefinitionName, FirstSliceEquipmentDefinitions.TokenDefinitionName])
        .ToDictionary(Key, n => n, StringComparer.Ordinal);
    public string Version { get; }
    public IReadOnlyList<string> AllowedDefinitions { get; }
    public CompensationCatalog(OperationsOptions options)
    {
        string[] definitions = options.Compensation.AllowedDefinitions;
        if (definitions is null || definitions.Length > supported.Count || definitions.Distinct(StringComparer.Ordinal).Count() != definitions.Length
            || definitions.Any(d => d is null || !supported.ContainsKey(d)))
            throw new InvalidOperationException("Operations compensation contains an unsupported server definition/version.");
        AllowedDefinitions = Array.AsReadOnly(options.Compensation.Enabled ? definitions.Order(StringComparer.Ordinal).ToArray() : []);
        Version = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes("first-slice-recipe-v1\n" + string.Join('\n', AllowedDefinitions))));
    }
    public bool Allows(string key) => AllowedDefinitions.Contains(key, StringComparer.Ordinal);
    internal static ItemRepositoryCommitRequest Create(CompensationCommand command)
    {
        string name = supported[command.Preview.Intent.DefinitionKey];
        var owner = new ItemOwnerRef(ItemOwnerType.Character, command.Preview.Intent.CharacterId.ToString("D"));
        bool equipment = FirstSliceEquipmentDefinitions.TryResolve(FirstSliceEquipmentDefinitions.DefinitionType, name, 1, out _);
        ItemRecord item = new(FirstSliceEquipmentDefinitions.DefinitionType, name, 1, owner,
            new(ItemContainerType.Mail, "OpsCompensation." + command.RequestId.ToString("D"), 0),
            new(Guid.NewGuid(), 0, 1, [], [], equipment ? FirstSliceEquipmentProgression.Create() : null), 0,
            ItemLifecycleState.Active, new(ItemBindState.CharacterBound, new(0, 0), null, CompensationIntent.Template, false));
        ItemRepositoryCommitRequest grant = new(command.RequestId, CompensationIntent.Template, command.InputHash, owner, 1, [new(0, item)]);
        if (!ItemRecordRules.TryValidateCommit(grant, out _)) throw new InvalidOperationException("Invalid compensation recipe.");
        return grant;
    }
    internal static OperationAction Receipt(CompensationCommand command, ItemRepositoryCommitResult item)
    {
        if (item.Status != ItemRepositoryCommitStatus.Committed || item.Records.Count != 1)
            throw new InvalidOperationException("Compensation item commit did not produce one new item.");
        var record = item.Records[0];
        return new(command.RequestId, CompensationIntent.Template, item.CommittedAt, command.Preview.OperatorId,
            new("Character", command.Preview.Intent.CharacterId), command.Preview.Intent.CaseId, command.Preview.Intent.Reason,
            new(record.ItemId, command.Preview.Intent.DefinitionKey, 1,
                "Mail/" + record.Location.ContainerId + "/0", command.RequestId), command.OriginHttpRequestId, command.OriginTraceId);
    }
    internal static bool Consistent(OperationAction action, string hash, ItemRepositoryCommitResult? item) =>
        item is not null && item.RequestId == action.RequestId && item.Operation == action.Operation && item.CommandFingerprint == hash
        && action.Operation == CompensationIntent.Template && action.Target.Scope == "Character"
        && action.Result.Quantity == 1 && action.Result.ItemReceiptRequestId == action.RequestId
        && action.Result.Location == "Mail/OpsCompensation." + action.RequestId.ToString("D") + "/0"
        && item.Actor == new ItemOwnerRef(ItemOwnerType.Character, action.Target.Id.ToString("D"))
        && item.Records.Count == 1 && item.Records[0].ItemId == action.Result.ItemId && item.Records[0].State.Quantity == 1
        && item.Records[0].DefinitionType == FirstSliceEquipmentDefinitions.DefinitionType && item.Records[0].DefinitionVersion == 1
        && Key(item.Records[0].DefinitionName) == action.Result.DefinitionKey
        && item.Records[0].Location == new ItemLocation(ItemContainerType.Mail, "OpsCompensation." + action.RequestId.ToString("D"), 0)
        && item.CommittedAt == action.CommittedAtUtc;
}
