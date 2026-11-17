#pragma once

namespace Hooks {
    bool Initialize();
    void Shutdown();
    bool Ready();
    void SetSdkReady(bool ready);
    // True once the render thread has released the input lock and reset your pawn after End.
    bool UnloadCleaned();
}
