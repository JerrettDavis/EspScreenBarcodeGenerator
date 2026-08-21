namespace EspBarcode.Protocol;

public enum AssemblyOutcome { Incomplete, Complete, DuplicateIgnored, Conflict }

public sealed class FrameAssembler(int maxConcurrentMessages = 2)
{
    private readonly record struct Key(uint LinkSessionId, uint LinkMessageId, ushort RouteId);

    private sealed class Partial
    {
        public Key Key;
        public ushort FragmentCount;
        public byte[]?[] Fragments = [];
        public int ReceivedCount;
    }

    private readonly List<Partial> _partial = [];

    public AssemblyOutcome AddFragment(HopFrameHeader header, byte[] payload, out byte[] assembled)
    {
        var key = new Key(header.LinkSessionId, header.LinkMessageId, header.RouteId);
        var entry = _partial.Find(p => p.Key.Equals(key));

        if (entry is null)
        {
            if (_partial.Count >= maxConcurrentMessages) _partial.RemoveAt(0);
            entry = new Partial { Key = key, FragmentCount = header.FragmentCount, Fragments = new byte[header.FragmentCount][] };
            _partial.Add(entry);
        }

        if (header.FragmentCount != entry.FragmentCount || header.FragmentIndex >= entry.Fragments.Length)
        {
            assembled = [];
            return AssemblyOutcome.Conflict;
        }

        var existing = entry.Fragments[header.FragmentIndex];
        if (existing is not null)
        {
            assembled = [];
            return existing.AsSpan().SequenceEqual(payload) ? AssemblyOutcome.DuplicateIgnored : AssemblyOutcome.Conflict;
        }
        entry.Fragments[header.FragmentIndex] = payload;
        entry.ReceivedCount++;

        if (entry.ReceivedCount < entry.FragmentCount) { assembled = []; return AssemblyOutcome.Incomplete; }

        var combined = new List<byte>();
        foreach (var fragment in entry.Fragments) combined.AddRange(fragment!);
        assembled = [.. combined];
        _partial.Remove(entry);
        return AssemblyOutcome.Complete;
    }
}
