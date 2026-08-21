#pragma once

#include <cstdint>

namespace esplink {

struct OperationId {
    uint64_t value = 0;
    friend bool operator==(OperationId a, OperationId b) { return a.value == b.value; }
    friend bool operator!=(OperationId a, OperationId b) { return !(a == b); }
};

struct CorrelationId {
    uint64_t value = 0;
    friend bool operator==(CorrelationId a, CorrelationId b) { return a.value == b.value; }
};

struct ControlSessionId {
    uint32_t value = 0;
    friend bool operator==(ControlSessionId a, ControlSessionId b) { return a.value == b.value; }
    friend bool operator!=(ControlSessionId a, ControlSessionId b) { return !(a == b); }
};

struct ControllerId {
    uint32_t value = 0;
    friend bool operator==(ControllerId a, ControllerId b) { return a.value == b.value; }
};

struct TransferId {
    uint32_t value = 0;
    friend bool operator==(TransferId a, TransferId b) { return a.value == b.value; }
};

}  // namespace esplink
