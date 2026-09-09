namespace FrameLedger.Infrastructure.Persistence;

/// <summary>The ledger's schema is one this build refuses to read — newer than any script it carries.</summary>
public sealed class LedgerSchemaException : Exception
{
    public LedgerSchemaException()
    {
    }

    public LedgerSchemaException(string message)
        : base(message)
    {
    }

    public LedgerSchemaException(string message, Exception innerException)
        : base(message, innerException)
    {
    }
}
