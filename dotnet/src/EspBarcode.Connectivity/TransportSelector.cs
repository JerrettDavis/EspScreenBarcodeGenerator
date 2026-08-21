namespace EspBarcode.Connectivity;

public static class TransportSelector
{
    // [OptimizationGoal][TransportKind], index order: UsbV1, UsbV2, Bluetooth, WifiDirect, EspNowGateway.
    private static readonly int[,] BaseScore =
    {
        /* LowLatency */      { 50, 90, 60, 80, 100 },
        /* HighThroughput */  { 40, 90, 50, 100, 85 },
        /* Balanced */        { 50, 80, 60, 70, 75 },
        /* MinimalHostDeps */ { 85, 85, 90, 90, 40 },
        /* Deterministic */   { 95, 100, 50, 40, 70 },
        /* MinimumPower */    { 70, 70, 80, 40, 85 },
    };

    private static int StatePenalty(CapabilityState state) => state switch
    {
        CapabilityState.Experimental => 15,
        CapabilityState.Degraded => 30,
        _ => 0,
    };

    public static SelectionDecision Evaluate(SelectionPolicyConfig policy, IReadOnlyList<TransportCapabilities> candidates)
    {
        var scored = new List<CandidateScore>(candidates.Count);

        foreach (var candidate in candidates)
        {
            bool eligible = false;
            int score = 0;
            string reason;

            if (policy.Deny.Contains(candidate.Kind))
            {
                reason = "denied by policy";
            }
            else if (policy.Allow.Count > 0 && !policy.Allow.Contains(candidate.Kind))
            {
                reason = "not in allow list";
            }
            else if (candidate.State == CapabilityState.CompiledOut) reason = "compiled out";
            else if (candidate.State == CapabilityState.UnsupportedHardware) reason = "unsupported hardware";
            else if (candidate.State == CapabilityState.Disabled) reason = "disabled";
            else if (candidate.State == CapabilityState.Blocked) reason = "blocked";
            else if (candidate.State == CapabilityState.Unavailable) reason = "unavailable";
            else if (policy.Requirement.WirelessRequired && !candidate.Wireless) reason = "wireless required";
            else if (policy.Requirement.ExternalHardware == SideEffectPermission.Forbidden && candidate.RequiresExternalHardware)
                reason = "external hardware forbidden by policy";
            else if (policy.Requirement.ExternalHardware == SideEffectPermission.Required && !candidate.RequiresExternalHardware)
                reason = "external hardware required by policy";
            else if (policy.Requirement.IpInterface == SideEffectPermission.Forbidden && candidate.CreatesIpInterface)
                reason = "IP interface forbidden by policy";
            else if (policy.Requirement.IpInterface == SideEffectPermission.Required && !candidate.CreatesIpInterface)
                reason = "IP interface required by policy";
            else if (policy.Requirement.MultiDevice == SideEffectPermission.Required && !candidate.SupportsMultipleDevices)
                reason = "multi-device support required by policy";
            else
            {
                eligible = true;
                score = BaseScore[(int)policy.Goal, (int)candidate.Kind] - StatePenalty(candidate.State);
                reason = "eligible";
            }

            scored.Add(new CandidateScore { Kind = candidate.Kind, Eligible = eligible, Score = score, Reason = reason });
        }

        TransportKind? selected = null;
        int bestScore = 0;
        foreach (var c in scored)
        {
            if (!c.Eligible) continue;
            if (selected is null || c.Score > bestScore) { selected = c.Kind; bestScore = c.Score; }
        }

        return new SelectionDecision { Selected = selected, Candidates = scored };
    }
}
