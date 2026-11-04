using Microsoft.AspNetCore.Authentication;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;
using ProjectRpg.Backend.Operations.Hosting;

namespace ProjectRpg.Backend.Pages.Ops;

public sealed class LoginModel : PageModel
{
    public async Task<IActionResult> OnGetAsync()
    {
        var authentication = await HttpContext.AuthenticateAsync(OperationsAuthentication.CookieScheme);
        return authentication.Succeeded ? Redirect("/ops") : Page();
    }
}
