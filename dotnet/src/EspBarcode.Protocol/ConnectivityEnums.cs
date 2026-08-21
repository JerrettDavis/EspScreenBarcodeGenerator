namespace EspBarcode.Protocol;

public enum MessageKind : byte { Command = 0, Result = 1, Event = 2, Error = 3, Transfer = 4 }

public enum ServiceId : byte
{
    System = 0, Barcode = 1, Preset = 2, Transfer = 3, Device = 4,
    Connectivity = 5, Trust = 6, Gateway = 7, Diagnostics = 8,
}

public enum CodecId : byte { Json = 0, Binary = 1 }

public enum FrameType : byte { Data = 0, Ack = 1, Nack = 2, KeepAlive = 3, Close = 4, Reset = 5 }

public enum TrafficClass : byte { Control = 0, Metadata = 1, Bulk = 2, Critical = 3, Event = 4 }

public enum CarrierProfileId : byte
{
    Unspecified = 0, EspNowV1 = 1, EspNowV2 = 2, StreamSmall = 3,
    StreamStandard = 4, StreamLarge = 5, TcpStandard = 6, TcpLarge = 7,
    BleGattV1 = 8,
}

public enum CodecError
{
    None, TooShort, BadMagic, UnsupportedMajorVersion, BodyLengthMismatch,
    CrcMismatch, PayloadTooLarge, ReservedFieldNonZero, BadHeaderLength,
    InvalidFragmentIndex,
}
