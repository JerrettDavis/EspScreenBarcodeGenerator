using System.Buffers.Binary;

namespace EspBarcode.Protocol;

public sealed record HopFrameHeader
{
    public byte Major { get; init; } = 2;
    public byte Minor { get; init; }
    public FrameType FrameType { get; init; } = FrameType.Data;
    public byte Flags { get; init; }
    public TrafficClass TrafficClass { get; init; } = TrafficClass.Control;
    public CarrierProfileId ProfileId { get; init; } = CarrierProfileId.Unspecified;
    public ushort RouteId { get; init; }
    public uint LinkSessionId { get; init; }
    public uint LinkMessageId { get; init; }
    public uint LinkCorrelationId { get; init; }
    public ushort FragmentIndex { get; init; }
    public ushort FragmentCount { get; init; } = 1;

    public const int HeaderSize = 32;
    public const int Overhead = 36; // header(32) + crc32 trailer(4)

    public static bool TryEncode(HopFrameHeader header, ReadOnlySpan<byte> payload, out byte[] frame, out CodecError error)
    {
        if (payload.Length > ushort.MaxValue) { frame = []; error = CodecError.PayloadTooLarge; return false; }
        frame = new byte[Overhead + payload.Length];
        var span = frame.AsSpan();
        span[0] = (byte)'E';
        span[1] = (byte)'L';
        span[2] = header.Major;
        span[3] = header.Minor;
        span[4] = (byte)header.FrameType;
        span[5] = header.Flags;
        span[6] = (byte)header.TrafficClass;
        span[7] = (byte)header.ProfileId;
        BinaryPrimitives.WriteUInt16LittleEndian(span[8..], header.RouteId);
        BinaryPrimitives.WriteUInt16LittleEndian(span[10..], HeaderSize);
        BinaryPrimitives.WriteUInt32LittleEndian(span[12..], header.LinkSessionId);
        BinaryPrimitives.WriteUInt32LittleEndian(span[16..], header.LinkMessageId);
        BinaryPrimitives.WriteUInt32LittleEndian(span[20..], header.LinkCorrelationId);
        BinaryPrimitives.WriteUInt16LittleEndian(span[24..], header.FragmentIndex);
        BinaryPrimitives.WriteUInt16LittleEndian(span[26..], header.FragmentCount);
        BinaryPrimitives.WriteUInt16LittleEndian(span[28..], (ushort)payload.Length);
        BinaryPrimitives.WriteUInt16LittleEndian(span[30..], 0);
        payload.CopyTo(span[HeaderSize..]);

        uint crc = Crc32.Compute(span[..(HeaderSize + payload.Length)]);
        BinaryPrimitives.WriteUInt32LittleEndian(span[(HeaderSize + payload.Length)..], crc);
        error = CodecError.None;
        return true;
    }

    public static bool TryDecode(ReadOnlySpan<byte> bytes, out HopFrameHeader header, out byte[] payload, out CodecError error)
    {
        header = new HopFrameHeader();
        payload = [];
        if (bytes.Length < Overhead) { error = CodecError.TooShort; return false; }
        if (bytes[0] != (byte)'E' || bytes[1] != (byte)'L') { error = CodecError.BadMagic; return false; }
        byte major = bytes[2];
        if (major != 2) { error = CodecError.UnsupportedMajorVersion; return false; }
        ushort headerLength = BinaryPrimitives.ReadUInt16LittleEndian(bytes[10..]);
        if (headerLength != HeaderSize) { error = CodecError.BadHeaderLength; return false; }
        ushort payloadLength = BinaryPrimitives.ReadUInt16LittleEndian(bytes[28..]);
        ushort reserved = BinaryPrimitives.ReadUInt16LittleEndian(bytes[30..]);
        if (reserved != 0) { error = CodecError.ReservedFieldNonZero; return false; }
        ushort fragmentIndex = BinaryPrimitives.ReadUInt16LittleEndian(bytes[24..]);
        ushort fragmentCount = BinaryPrimitives.ReadUInt16LittleEndian(bytes[26..]);
        if (fragmentCount == 0 || fragmentIndex >= fragmentCount) { error = CodecError.InvalidFragmentIndex; return false; }

        int rawLength = HeaderSize + payloadLength + 4;
        if (bytes.Length < rawLength) { error = CodecError.TooShort; return false; }

        uint expectedCrc = BinaryPrimitives.ReadUInt32LittleEndian(bytes[(HeaderSize + payloadLength)..]);
        uint actualCrc = Crc32.Compute(bytes[..(HeaderSize + payloadLength)]);
        if (expectedCrc != actualCrc) { error = CodecError.CrcMismatch; return false; }

        header = new HopFrameHeader
        {
            Major = major,
            Minor = bytes[3],
            FrameType = (FrameType)bytes[4],
            Flags = bytes[5],
            TrafficClass = (TrafficClass)bytes[6],
            ProfileId = (CarrierProfileId)bytes[7],
            RouteId = BinaryPrimitives.ReadUInt16LittleEndian(bytes[8..]),
            LinkSessionId = BinaryPrimitives.ReadUInt32LittleEndian(bytes[12..]),
            LinkMessageId = BinaryPrimitives.ReadUInt32LittleEndian(bytes[16..]),
            LinkCorrelationId = BinaryPrimitives.ReadUInt32LittleEndian(bytes[20..]),
            FragmentIndex = fragmentIndex,
            FragmentCount = fragmentCount,
        };
        payload = bytes.Slice(HeaderSize, payloadLength).ToArray();
        error = CodecError.None;
        return true;
    }
}
