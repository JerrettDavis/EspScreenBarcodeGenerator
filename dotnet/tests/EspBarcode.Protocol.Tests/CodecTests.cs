using EspBarcode.Protocol;

namespace EspBarcode.Protocol.Tests;

public class CodecTests
{
    // Vector A: Layer 3 envelope wrapping {"schema":"esbg.control/2.0","name":"system.ping","body":{}}
    private const string VectorAHex =
        "454d020000000000000000003c000000010000000000000000000000000000007b22736368656d61223a22" +
        "657362672e636f6e74726f6c2f322e30222c226e616d65223a2273797374656d2e70696e67222c22626f6479" +
        "223a7b7d7d";

    private static byte[] FromHex(string hex) => Convert.FromHexString(hex);

    [Fact]
    public void Envelope_Encode_MatchesVectorA()
    {
        var body = FromHex(
            "7b22736368656d61223a22657362672e636f6e74726f6c2f322e30222c226e616d65223a2273797374656d" +
            "2e70696e67222c22626f6479223a7b7d7d");
        var envelope = new MessageEnvelope { OperationId = 1 };

        Assert.True(MessageEnvelope.TryEncode(envelope, body, out var message, out var error));
        Assert.Equal(CodecError.None, error);
        Assert.Equal(FromHex(VectorAHex), message);
    }

    [Fact]
    public void Envelope_Decode_RoundTripsVectorA()
    {
        var bytes = FromHex(VectorAHex);
        Assert.True(MessageEnvelope.TryDecode(bytes, out var envelope, out var body, out var error));
        Assert.Equal(CodecError.None, error);
        Assert.Equal(2, envelope.Major);
        Assert.Equal(MessageKind.Command, envelope.Kind);
        Assert.Equal(ServiceId.System, envelope.ServiceId);
        Assert.Equal(1ul, envelope.OperationId);
        Assert.Equal(60, body.Length);
    }

    [Fact]
    public void Envelope_Decode_RejectsTruncatedInput()
    {
        var bytes = FromHex(VectorAHex)[..20];
        Assert.False(MessageEnvelope.TryDecode(bytes, out _, out _, out var error));
        Assert.Equal(CodecError.TooShort, error);
    }

    // Vector B: single hop frame (stream-standard, linkSessionId=1001, linkMessageId=1)
    // wrapping Vector A's message.
    private const string VectorBHex =
        "454c02000001000400002000e90300000100000000000000000001005c000000454d0200000000000000000" +
        "03c000000010000000000000000000000000000007b22736368656d61223a22657362672e636f6e74726f6c2f" +
        "322e30222c226e616d65223a2273797374656d2e70696e67222c22626f6479223a7b7d7dee2c40c3";

    [Fact]
    public void HopFrame_Encode_MatchesVectorB()
    {
        var header = new HopFrameHeader
        {
            Flags = 0x01,
            ProfileId = CarrierProfileId.StreamStandard,
            LinkSessionId = 1001,
            LinkMessageId = 1,
        };
        var payload = FromHex(VectorAHex);

        Assert.True(HopFrameHeader.TryEncode(header, payload, out var frame, out var error));
        Assert.Equal(CodecError.None, error);
        Assert.Equal(FromHex(VectorBHex), frame);
        Assert.Equal(36 + payload.Length, frame.Length);
    }

    [Fact]
    public void HopFrame_Decode_ValidatesCrcAndRecoversPayload()
    {
        var raw = FromHex(VectorBHex);
        Assert.True(HopFrameHeader.TryDecode(raw, out var header, out var payload, out var error));
        Assert.Equal(CodecError.None, error);
        Assert.Equal(CarrierProfileId.StreamStandard, header.ProfileId);
        Assert.Equal(0, header.FragmentIndex);
        Assert.Equal(1, header.FragmentCount);
        Assert.Equal(FromHex(VectorAHex), payload);
    }

    [Fact]
    public void HopFrame_Decode_RejectsCorruptedPayload()
    {
        var raw = FromHex(VectorBHex);
        raw[^5] ^= 0xFF;
        Assert.False(HopFrameHeader.TryDecode(raw, out _, out _, out var error));
        Assert.Equal(CodecError.CrcMismatch, error);
    }

    [Fact]
    public void Cobs_RoundTrips_FrameContainingZeroBytes()
    {
        var raw = FromHex(VectorBHex);
        var encoded = Cobs.Encode(raw);
        Assert.DoesNotContain((byte)0x00, encoded);
        Assert.True(Cobs.TryDecode(encoded, out var decoded));
        Assert.Equal(raw, decoded);
    }

    [Fact]
    public void Cobs_Rejects_LiteralZeroInsideBlock()
    {
        byte[] corrupt = [0x03, 0x01, 0x00, 0x02];
        Assert.False(Cobs.TryDecode(corrupt, out _));
    }

    [Fact]
    public void Cobs_RoundTrips_DataEndingInZero()
    {
        byte[] raw = [0x41, 0x00];
        var encoded = Cobs.Encode(raw);
        Assert.DoesNotContain((byte)0x00, encoded);
        Assert.True(Cobs.TryDecode(encoded, out var decoded));
        Assert.Equal(raw, decoded);
    }

    [Fact]
    public void Cobs_RoundTrips_ALoneZeroByte()
    {
        byte[] raw = [0x00];
        var encoded = Cobs.Encode(raw);
        Assert.DoesNotContain((byte)0x00, encoded);
        Assert.True(Cobs.TryDecode(encoded, out var decoded));
        Assert.Equal(raw, decoded);
    }

    [Fact]
    public void Cobs_RoundTrips_254ByteRunFollowedByZero()
    {
        var raw = new byte[256];
        for (int i = 0; i < 254; i++) raw[i] = 0x41;
        raw[254] = 0x00;
        raw[255] = 0x42;
        var encoded = Cobs.Encode(raw);
        Assert.DoesNotContain((byte)0x00, encoded);
        Assert.True(Cobs.TryDecode(encoded, out var decoded));
        Assert.Equal(raw, decoded);
    }

    [Fact]
    public void FrameAssembler_ReassemblesTwoFragmentsInOrder()
    {
        var envelope = new MessageEnvelope { ServiceId = ServiceId.Barcode, OperationId = 7 };
        var data220 = new string('A', 220);
        var bodyStr = $"{{\"schema\":\"esbg.control/2.0\",\"name\":\"barcode.generate\",\"body\":{{\"type\":\"qr\",\"data\":\"{data220}\",\"display\":true}}}}";
        var body = System.Text.Encoding.UTF8.GetBytes(bodyStr);
        Assert.True(MessageEnvelope.TryEncode(envelope, body, out var message, out _));
        Assert.Equal(353, message.Length);

        const int maxPayload = 214;
        var frag0Payload = message[..maxPayload];
        var frag1Payload = message[maxPayload..];

        var h0 = new HopFrameHeader { TrafficClass = TrafficClass.Critical, ProfileId = CarrierProfileId.EspNowV1, LinkSessionId = 2002, LinkMessageId = 5, FragmentIndex = 0, FragmentCount = 2 };
        var h1 = h0 with { FragmentIndex = 1 };
        Assert.True(HopFrameHeader.TryEncode(h0, frag0Payload, out var frame0, out _));
        Assert.True(HopFrameHeader.TryEncode(h1, frag1Payload, out var frame1, out _));

        Assert.True(HopFrameHeader.TryDecode(frame0, out var dh0, out var p0, out _));
        Assert.True(HopFrameHeader.TryDecode(frame1, out var dh1, out var p1, out _));

        var assembler = new FrameAssembler();
        Assert.Equal(AssemblyOutcome.Incomplete, assembler.AddFragment(dh0, p0, out _));
        Assert.Equal(AssemblyOutcome.Complete, assembler.AddFragment(dh1, p1, out var assembled));
        Assert.Equal(message, assembled);
    }

    [Fact]
    public void FrameAssembler_IgnoresExactDuplicateFragment()
    {
        var h = new HopFrameHeader { LinkSessionId = 1, LinkMessageId = 1, FragmentIndex = 0, FragmentCount = 2 };
        byte[] payload = [1, 2, 3];
        var assembler = new FrameAssembler();
        Assert.Equal(AssemblyOutcome.Incomplete, assembler.AddFragment(h, payload, out _));
        Assert.Equal(AssemblyOutcome.DuplicateIgnored, assembler.AddFragment(h, payload, out _));
    }

    [Fact]
    public void FrameAssembler_FlagsConflictingDuplicateFragment()
    {
        var h = new HopFrameHeader { LinkSessionId = 1, LinkMessageId = 2, FragmentIndex = 0, FragmentCount = 2 };
        var assembler = new FrameAssembler();
        Assert.Equal(AssemblyOutcome.Incomplete, assembler.AddFragment(h, [1, 2, 3], out _));
        Assert.Equal(AssemblyOutcome.Conflict, assembler.AddFragment(h, [9, 9, 9], out _));
    }

    [Fact]
    public void FrameAssembler_BoundsConcurrentMessages()
    {
        var assembler = new FrameAssembler(maxConcurrentMessages: 2);
        var h1 = new HopFrameHeader { LinkSessionId = 1, LinkMessageId = 1, FragmentIndex = 0, FragmentCount = 2 };
        var h2 = new HopFrameHeader { LinkSessionId = 1, LinkMessageId = 2, FragmentIndex = 0, FragmentCount = 2 };
        var h3 = new HopFrameHeader { LinkSessionId = 1, LinkMessageId = 3, FragmentIndex = 0, FragmentCount = 2 };
        Assert.Equal(AssemblyOutcome.Incomplete, assembler.AddFragment(h1, [1], out _));
        Assert.Equal(AssemblyOutcome.Incomplete, assembler.AddFragment(h2, [2], out _));
        Assert.Equal(AssemblyOutcome.Incomplete, assembler.AddFragment(h3, [3], out _));
        var h1b = h1 with { FragmentIndex = 1 };
        Assert.Equal(AssemblyOutcome.Incomplete, assembler.AddFragment(h1b, [1, 1], out _));
    }
}
