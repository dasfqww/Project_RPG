[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [string]$BaseUrl = 'http://127.0.0.1:3000',
    [string]$AdminToken = $env:PROJECT_RPG_BACKEND_ADMIN_TOKEN
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($AdminToken)) {
    throw 'PROJECT_RPG_BACKEND_ADMIN_TOKEN or -AdminToken is required.'
}

$definitions = @(
    @{
        currencyCode = 'RosterGold'
        displayName = 'Gold'
        scope = 'Roster'
        maxBalance = 1000000000
        enabled = $true
    }
)
$headers = @{ Authorization = "Bearer $AdminToken" }
$normalizedBaseUrl = $BaseUrl.TrimEnd('/')

foreach ($definition in $definitions) {
    $uri = "$normalizedBaseUrl/api/economy/currency-definitions/$($definition.currencyCode)"
    if (-not $PSCmdlet.ShouldProcess(
        $uri,
        "Create or update currency definition $($definition.currencyCode)")) {
        continue
    }

    $result = Invoke-RestMethod `
        -Method Put `
        -Uri $uri `
        -Headers $headers `
        -ContentType 'application/json' `
        -Body ($definition | ConvertTo-Json -Compress)

    [pscustomobject]@{
        CurrencyCode = $result.currencyCode
        DisplayName = $result.displayName
        Scope = $result.scope
        MaxBalance = $result.maxBalance
        Enabled = $result.enabled
    }
}
