using Microsoft.AspNetCore.Mvc.RazorPages;
using Microsoft.AspNetCore.WebUtilities;
using ProjectRpg.Backend.Operations.Web;
using ProjectRpg.Operations.Core.Characters;
using ProjectRpg.Operations.Core.Common;
using ProjectRpg.Operations.Core.Inventory;
using ProjectRpg.Operations.Core.Sessions;
using ProjectRpg.Operations.Core.Wallets;

namespace ProjectRpg.Backend.Pages.Ops;

public sealed class CharacterModel(OperationsReadFacade reads) : PageModel
{
    public CharacterHeader? Character { get; private set; }
    public WalletSnapshot? Wallet { get; private set; }
    public WebPage<ItemSummary>? Inventory { get; private set; }
    public WebPage<SessionEvidence>? Sessions { get; private set; }
    public Failure? Error { get; private set; }
    public string? NextLink { get; private set; }
    public async Task OnGetAsync(string id)
    {
        if (!OperationsInput.Id(id, out Guid characterId)) { Error = Failures.InvalidQuery; Response.StatusCode = 400; return; }
        var header = await reads.CharacterAsync(characterId, HttpContext.RequestAborted);
        if (!header.IsSuccess) { Error = header.Error; Response.StatusCode = OperationsHttp.Status(Error!); return; }
        Character = header.Value;
        string panel = Request.Query["panel"].FirstOrDefault() ?? "wallet";
        string? cursor = null;
        switch (panel)
        {
            case "wallet":
                if (!OperationsInput.KnownQuery(HttpContext, "panel")) { Error = Failures.InvalidQuery; break; }
                var wallet = await reads.WalletAsync(characterId, HttpContext.RequestAborted);
                if (wallet.IsSuccess) Wallet = wallet.Value; else Error = wallet.Error;
                break;
            case "inventory":
                var inventory = await reads.InventoryAsync(HttpContext, characterId);
                if (inventory.IsSuccess) { Inventory = inventory.Value; cursor = Inventory.NextCursor; } else Error = inventory.Error;
                break;
            case "sessions":
                var sessions = await reads.SessionsAsync(HttpContext, characterId);
                if (sessions.IsSuccess) { Sessions = sessions.Value; cursor = Sessions.NextCursor; } else Error = sessions.Error;
                break;
            default: Error = Failures.InvalidQuery; break;
        }
        if (Error is not null) Response.StatusCode = OperationsHttp.Status(Error);
        if (cursor is not null) NextLink = QueryHelpers.AddQueryString(Request.Path, new Dictionary<string,string?>
            { ["panel"] = panel, ["container"] = Request.Query["container"].FirstOrDefault(), ["limit"] = Request.Query["limit"].FirstOrDefault(), ["cursor"] = cursor });
    }
}
