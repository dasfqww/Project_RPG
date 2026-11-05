namespace ProjectRpg.Backend.Domain;

public sealed class AccountRestrictedException : Exception
{
    public AccountRestrictedException():base("account_restricted") { }
}
