using FrameLedger.Infrastructure.Telemetry;

namespace FrameLedger.Infrastructure.Tests.Telemetry;

/// <summary>A scripted <see cref="INvapiBridge"/>: what Init answers, what each read answers.</summary>
internal sealed class FakeNvapiBridge : INvapiBridge
{
    public int InitResult { get; set; }

    public Func<NvapiSample>? Sample { get; set; }

    public Func<int, NvapiNgxWords>? Ngx { get; set; }

    public uint Driver { get; set; } = 61664;

    public int Inits { get; private set; }

    public int Shutdowns { get; private set; }

    public int Reads { get; private set; }

    public bool Disposed { get; private set; }

    public int Init()
    {
        Inits++;
        return InitResult;
    }

    public int ReadSample(out NvapiSample sample)
    {
        Reads++;
        sample = Sample is null ? default : Sample();
        return Sample is null ? -1 : 0;
    }

    public int NgxState(uint pid, out NvapiNgxWords words)
    {
        words = Ngx is null ? new NvapiNgxWords { Status = NvapiNgxWords.Unanswered, NvapiStatus = -121 } : Ngx((int)pid);
        return 0;
    }

    public int DriverVersion(out uint version, out string branch)
    {
        version = Driver;
        branch = "r616_00";
        return 0;
    }

    public void Shutdown() => Shutdowns++;

    public void Dispose() => Disposed = true;

    public static NvapiSample Full() => new()
    {
        Present = (uint)(NvapiField.TempCore | NvapiField.Load | NvapiField.CoreClock | NvapiField.MemClock | NvapiField.Fan | NvapiField.Throttle | NvapiField.PcieWidth | NvapiField.Name),
        TempCoreC = 62,
        LoadPct = 57,
        CoreClockMhz = 2610,
        MemClockMhz = 15001,
        FanRpm = 1200,
        ThrottleReasons = 0x2,
        PcieWidth = 16,
        Name = "NVIDIA GeForce RTX 5080",
    };
}
