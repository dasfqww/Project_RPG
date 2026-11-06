namespace ProjectRpg.Backend.Configuration;

public sealed class TradingOptions
{
    public bool Enabled { get; set; }
    public string CurrencyCode { get; set; } = "Gold";

    public void Validate()
    {
        if (string.IsNullOrWhiteSpace(CurrencyCode) || CurrencyCode.Length > 64
            || !CurrencyCode.All(c => char.IsAsciiLetterOrDigit(c) || c is '.' or '_' or '-'))
            throw new InvalidOperationException("Trading:CurrencyCode must be a valid currency code.");
    }
}
