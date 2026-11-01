# API rate limiting

The backend applies two fail-fast fixed-window limits to every `/api` request.
Health endpoints are excluded.

1. Before authentication, requests are limited by the direct peer IP address.
   Steam ticket authentication has a smaller independent budget. This bounds
   invalid-token database lookups and login abuse.
2. After successful authentication, requests are limited independently by
   player Steam ID, game-server/session identity, or the administrator role.

Bearer tokens are never used as partition keys or written to rate-limit
responses. Rejected requests return HTTP 429, `Retry-After`, `Cache-Control:
no-store`, and this stable JSON body:

```json
{"error":"rate_limit_exceeded"}
```

## Configuration

Defaults are stored in `ProjectRpg.Backend/appsettings.json`:

```json
{
  "RateLimits": {
    "Enabled": true,
    "WindowSeconds": 60,
    "AuthenticationPermitLimit": 60,
    "IngressPermitLimit": 6000,
    "PlayerPermitLimit": 600,
    "GameServerPermitLimit": 6000,
    "AdministratorPermitLimit": 3000
  }
}
```

Normal ASP.NET Core configuration overrides apply. For example,
`RateLimits__PlayerPermitLimit` changes the player budget through an environment
variable. Invalid enabled limits fail during backend startup.

These counters are local to one backend process. A multi-instance deployment
still needs a coordinated edge, gateway, WAF, or distributed limiter. The
ingress key uses the direct TCP peer and does not trust `X-Forwarded-For` by
default. When deploying behind a reverse proxy, configure forwarded headers
with explicit known proxies before the ingress limiter.

## Verification

Run the focused HTTP pipeline tests:

```powershell
dotnet run --project `
  Backend/ProjectRpg.Backend.RateLimitTests/ProjectRpg.Backend.RateLimitTests.csproj
```

They verify login limiting, pre-auth invalid credentials, independent player,
game-server and administrator budgets, health bypass, 429 response shape, and
window replenishment.
