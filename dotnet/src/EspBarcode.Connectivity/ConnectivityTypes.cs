namespace EspBarcode.Connectivity;

public enum RunMode { Standalone, Auto, UsbV1, UsbV2, Bluetooth, WifiDirect, EspNowGateway, Adaptive }

public enum TransportKind { UsbV1, UsbV2, Bluetooth, WifiDirect, EspNowGateway }

public enum CapabilityState
{
    CompiledOut, UnsupportedHardware, Disabled, Blocked, Unavailable,
    Available, Degraded, Experimental, Qualified,
}

public enum OptimizationGoal { LowLatency, HighThroughput, Balanced, MinimalHostDependencies, Deterministic, MinimumPower }

public enum SideEffectPermission { Forbidden, Allowed, Required }

public sealed record ConnectionRequirement
{
    public bool WirelessRequired { get; init; }
    public SideEffectPermission ExternalHardware { get; init; } = SideEffectPermission.Allowed;
    public SideEffectPermission IpInterface { get; init; } = SideEffectPermission.Allowed;
    public SideEffectPermission MultiDevice { get; init; } = SideEffectPermission.Allowed;
}

public sealed record TransportCapabilities
{
    public required TransportKind Kind { get; init; }
    public required CapabilityState State { get; init; }
    public bool Wireless { get; init; }
    public bool RequiresExternalHardware { get; init; }
    public bool CreatesIpInterface { get; init; }
    public bool SupportsMultipleDevices { get; init; }
}

public sealed record SelectionPolicyConfig
{
    public RunMode Mode { get; init; } = RunMode.Auto;
    public OptimizationGoal Goal { get; init; } = OptimizationGoal.Balanced;
    public ConnectionRequirement Requirement { get; init; } = new();
    public IReadOnlyList<TransportKind> Allow { get; init; } = [];
    public IReadOnlyList<TransportKind> Deny { get; init; } = [];
}

public sealed record CandidateScore
{
    public required TransportKind Kind { get; init; }
    public bool Eligible { get; init; }
    public int Score { get; init; }
    public string Reason { get; init; } = "";
}

public sealed record SelectionDecision
{
    public TransportKind? Selected { get; init; }
    public IReadOnlyList<CandidateScore> Candidates { get; init; } = [];
}
