namespace FrameLedger.Domain.Metrics;

/// <summary>Which ratio a refusal is about — the text differs, the facts are the same.</summary>
public enum FgRefusalSubject
{
    /// <summary><c>presents / Σ fgEvaluations</c>, the factor.</summary>
    Factor = 0,

    /// <summary><c>presents / batches</c>, the proxy.</summary>
    PresentsPerBatch,
}
