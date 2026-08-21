using System.Buffers.Binary;

namespace EspBarcode.Protocol;

public sealed record MessageEnvelope
{
    public byte Major { get; init; } = 2;
    public byte Minor { get; init; }
    public MessageKind Kind { get; init; } = MessageKind.Command;
    public byte Flags { get; init; }
    public ServiceId ServiceId { get; init; } = ServiceId.System;
    public CodecId CodecId { get; init; } = CodecId.Json;
    public uint ControlSessionId { get; init; }
    public uint BodyLength { get; init; }
    public ulong OperationId { get; init; }
    public ulong CorrelationId { get; init; }

    public const int HeaderSize = 32;

    public static bool TryEncode(MessageEnvelope envelope, ReadOnlySpan<byte> body, out byte[] message, out CodecError error)
    {
        message = new byte[HeaderSize + body.Length];
        var span = message.AsSpan();
        span[0] = (byte)'E';
        span[1] = (byte)'M';
        span[2] = envelope.Major;
        span[3] = envelope.Minor;
        span[4] = (byte)envelope.Kind;
        span[5] = envelope.Flags;
        span[6] = (byte)envelope.ServiceId;
        span[7] = (byte)envelope.CodecId;
        BinaryPrimitives.WriteUInt32LittleEndian(span[8..], envelope.ControlSessionId);
        BinaryPrimitives.WriteUInt32LittleEndian(span[12..], (uint)body.Length);
        BinaryPrimitives.WriteUInt64LittleEndian(span[16..], envelope.OperationId);
        BinaryPrimitives.WriteUInt64LittleEndian(span[24..], envelope.CorrelationId);
        body.CopyTo(span[HeaderSize..]);
        error = CodecError.None;
        return true;
    }

    public static bool TryDecode(ReadOnlySpan<byte> bytes, out MessageEnvelope envelope, out byte[] body, out CodecError error)
    {
        envelope = new MessageEnvelope();
        body = [];
        if (bytes.Length < HeaderSize) { error = CodecError.TooShort; return false; }
        if (bytes[0] != (byte)'E' || bytes[1] != (byte)'M') { error = CodecError.BadMagic; return false; }
        byte major = bytes[2];
        if (major != 2) { error = CodecError.UnsupportedMajorVersion; return false; }

        uint bodyLength = BinaryPrimitives.ReadUInt32LittleEndian(bytes[12..]);
        if (bytes.Length - HeaderSize < bodyLength) { error = CodecError.BodyLengthMismatch; return false; }

        envelope = new MessageEnvelope
        {
            Major = major,
            Minor = bytes[3],
            Kind = (MessageKind)bytes[4],
            Flags = bytes[5],
            ServiceId = (ServiceId)bytes[6],
            CodecId = (CodecId)bytes[7],
            ControlSessionId = BinaryPrimitives.ReadUInt32LittleEndian(bytes[8..]),
            BodyLength = bodyLength,
            OperationId = BinaryPrimitives.ReadUInt64LittleEndian(bytes[16..]),
            CorrelationId = BinaryPrimitives.ReadUInt64LittleEndian(bytes[24..]),
        };
        body = bytes.Slice(HeaderSize, (int)bodyLength).ToArray();
        error = CodecError.None;
        return true;
    }
}
