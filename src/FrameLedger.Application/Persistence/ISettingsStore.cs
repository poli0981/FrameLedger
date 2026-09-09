namespace FrameLedger.Application.Persistence;

/// <summary>The <c>settings</c> key/value table. The key list itself is <c>20_OPEN_QUESTIONS</c> §G's "Settings registry", still open.</summary>
public interface ISettingsStore
{
    ValueTask<string?> GetAsync(string key, CancellationToken ct = default);

    ValueTask SetAsync(string key, string value, CancellationToken ct = default);
}
