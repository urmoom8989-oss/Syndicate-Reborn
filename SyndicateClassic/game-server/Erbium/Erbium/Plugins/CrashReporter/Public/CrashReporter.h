#pragma once
#include "../../../../pch.h"

class FCrashReporter
{
public:
    static void Register();
};

// Set (per thread) while Erbium reads memory that may legitimately fault, inside its own __try/__except.
// The crash reporter is a vectored handler, so it sees those faults first; while this is set it leaves
// them to the __except instead of killing the server (Season X: the pak-platform scan read a page another
// thread had just freed and [HOST-ISLAND-01] ended a background server while it loaded).
inline thread_local int ErbiumProbing = 0;
struct FErbiumProbeScope { FErbiumProbeScope() { ++ErbiumProbing; } ~FErbiumProbeScope() { --ErbiumProbing; } };
