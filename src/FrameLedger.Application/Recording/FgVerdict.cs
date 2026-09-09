namespace FrameLedger.Application.Recording;

/// <summary>
/// What the frame-generation ladder concluded (<c>03_METRICS</c> §Frame Generation, rungs 0/1/3/4, and the
/// withhold rule). The name is a token; the report and the row each spell it their own way.
/// </summary>
public enum FgVerdict
{
    /// <summary>Rung 0: no record claimed a frame-generation measurement. N/A, never <c>none</c>.</summary>
    NotMeasured = 0,

    /// <summary>Rung 4: the count said 1.0 with no identity — the counted negative.</summary>
    None,

    /// <summary>The count said 1.0 while DLSS-G inputs were tagged through Streamline: <c>none</c>, with the inputs noted.</summary>
    NoneInputsTagged,

    /// <summary>The count said 1.0 on the one shape where 1.0 cannot be told from generation (§H5): N/A, with the reason.</summary>
    NoneWithheld,

    /// <summary>Rung 3: the cadence says frames are generated and no hooked identity names the technology.</summary>
    ActiveUnidentified,

    /// <summary>Rung 1: a hooked identity named the technology.</summary>
    Named,
}
