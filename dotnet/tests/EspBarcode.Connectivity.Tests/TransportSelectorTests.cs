using EspBarcode.Connectivity;

namespace EspBarcode.Connectivity.Tests;

public class TransportSelectorTests
{
    private static TransportCapabilities Available(TransportKind kind, bool wireless = false, bool hw = false, bool ip = false, bool multi = false) =>
        new() { Kind = kind, State = CapabilityState.Available, Wireless = wireless, RequiresExternalHardware = hw, CreatesIpInterface = ip, SupportsMultipleDevices = multi };

    [Fact]
    public void NoRequirement_PicksHighestScore_RegardlessOfWireless()
    {
        var policy = new SelectionPolicyConfig { Goal = OptimizationGoal.LowLatency };
        var candidates = new[] { Available(TransportKind.UsbV2), Available(TransportKind.Bluetooth, wireless: true) };
        var decision = TransportSelector.Evaluate(policy, candidates);
        Assert.Equal(TransportKind.UsbV2, decision.Selected);
    }

    [Fact]
    public void WirelessRequired_ExcludesUsb()
    {
        var policy = new SelectionPolicyConfig { Goal = OptimizationGoal.LowLatency, Requirement = new() { WirelessRequired = true } };
        var candidates = new[] { Available(TransportKind.UsbV2), Available(TransportKind.Bluetooth, wireless: true) };
        var decision = TransportSelector.Evaluate(policy, candidates);
        Assert.Equal(TransportKind.Bluetooth, decision.Selected);
        Assert.False(decision.Candidates[0].Eligible);
        Assert.Equal("wireless required", decision.Candidates[0].Reason);
    }

    [Fact]
    public void DenyList_RemovesBestScoringCandidate()
    {
        var policy = new SelectionPolicyConfig
        {
            Goal = OptimizationGoal.HighThroughput,
            Requirement = new() { WirelessRequired = true },
            Deny = [TransportKind.WifiDirect],
        };
        var candidates = new[] { Available(TransportKind.WifiDirect, wireless: true), Available(TransportKind.Bluetooth, wireless: true) };
        var decision = TransportSelector.Evaluate(policy, candidates);
        Assert.Equal(TransportKind.Bluetooth, decision.Selected);
        Assert.Equal("denied by policy", decision.Candidates[0].Reason);
    }

    [Fact]
    public void IpInterfaceForbidden_ExcludesWifiDirect()
    {
        var policy = new SelectionPolicyConfig
        {
            Goal = OptimizationGoal.Balanced,
            Requirement = new() { WirelessRequired = true, IpInterface = SideEffectPermission.Forbidden },
        };
        var candidates = new[]
        {
            Available(TransportKind.WifiDirect, wireless: true, ip: true),
            Available(TransportKind.EspNowGateway, wireless: true, hw: true),
        };
        var decision = TransportSelector.Evaluate(policy, candidates);
        Assert.Equal(TransportKind.EspNowGateway, decision.Selected);
    }

    [Fact]
    public void ExternalHardwareForbidden_ExcludesGateway()
    {
        var policy = new SelectionPolicyConfig
        {
            Goal = OptimizationGoal.Balanced,
            Requirement = new() { WirelessRequired = true, ExternalHardware = SideEffectPermission.Forbidden },
        };
        var candidates = new[] { Available(TransportKind.EspNowGateway, wireless: true, hw: true), Available(TransportKind.Bluetooth, wireless: true) };
        var decision = TransportSelector.Evaluate(policy, candidates);
        Assert.Equal(TransportKind.Bluetooth, decision.Selected);
    }

    [Fact]
    public void AllCandidatesIneligible_YieldsNoSelectionWithReasons()
    {
        var policy = new SelectionPolicyConfig();
        var candidates = new[]
        {
            new TransportCapabilities { Kind = TransportKind.Bluetooth, State = CapabilityState.CompiledOut },
            new TransportCapabilities { Kind = TransportKind.WifiDirect, State = CapabilityState.Blocked },
        };
        var decision = TransportSelector.Evaluate(policy, candidates);
        Assert.Null(decision.Selected);
        Assert.Equal("compiled out", decision.Candidates[0].Reason);
        Assert.Equal("blocked", decision.Candidates[1].Reason);
    }

    [Fact]
    public void AllowList_RestrictsToNamedKinds()
    {
        var policy = new SelectionPolicyConfig { Allow = [TransportKind.UsbV2] };
        var candidates = new[] { Available(TransportKind.UsbV2), Available(TransportKind.Bluetooth, wireless: true) };
        var decision = TransportSelector.Evaluate(policy, candidates);
        Assert.Equal(TransportKind.UsbV2, decision.Selected);
        Assert.Equal("not in allow list", decision.Candidates[1].Reason);
    }

    [Fact]
    public void Decision_IsDeterministicAcrossRepeatedEvaluation()
    {
        var policy = new SelectionPolicyConfig { Goal = OptimizationGoal.Deterministic };
        var candidates = new[] { Available(TransportKind.UsbV1), Available(TransportKind.UsbV2) };
        var first = TransportSelector.Evaluate(policy, candidates);
        var second = TransportSelector.Evaluate(policy, candidates);
        Assert.Equal(first.Selected, second.Selected);
        Assert.Equal(TransportKind.UsbV2, first.Selected);
    }
}
