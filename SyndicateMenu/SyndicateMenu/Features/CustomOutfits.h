#pragma once

// Season 3: registers the Syndicate custom outfits (Spitfire: Phantom) so the locker shows them. Render thread,
// every frame; cheap once done. See CustomOutfits.cpp.
void TickCustomOutfits();

// Season 3, game thread (HUD / ProcessEvent hook): gives Spitfire: Phantom its preview hero and its downloaded locker
// icon (see CustomOutfits.cpp, "own locker icon").
void TickPhantomLook();
