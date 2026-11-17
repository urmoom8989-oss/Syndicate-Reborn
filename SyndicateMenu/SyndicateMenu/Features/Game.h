#pragma once

#include <cstdint>
#include <vector>

#include "../SDK/Types.h"

extern uintptr_t world;
extern uintptr_t localPC;
extern uintptr_t localPawn;
extern uintptr_t camMgr;
extern uintptr_t localPS;
extern Camera gCam;
extern std::vector<Target> targets;

Camera GetCamera();
// The camera ESP and aim use: the first of several sources that agrees with your pawn and your aim
// (see PickCamera in Game.cpp). CameraSourceName says which one, for the debug lines.
Camera PickCamera();
const char* CameraSourceName();
// Called from the HUD / ProcessEvent hook on the game thread: reads the camera there too.
void SampleCameraOnGameThread();
bool WorldToScreen(const Vec3& wpos, Vec2& out, int w, int h);
Vec3 GetBoneWorld(uintptr_t mesh, int idx);
Vec3 GetMeshWorld(uintptr_t mesh);
// The mesh's transform in the world, from the offset checked against the game (or asked from
// the game when no offset matched).
FTransform ReadComponentToWorld(uintptr_t mesh);
Vec3 GetStableHead(uintptr_t mesh);
bool MeshPoseAlive(uintptr_t mesh);
const BoneSet& BonesFor(uintptr_t mesh);
int AimBoneIndex(const BoneSet& b, int menuBone);
void CollectTargets(int w, int h, int aimBone);
// Game thread only: resolves bot names through the game's GetPlayerName for the ESP labels.
void ResolveNamesOnGameThread();
// Syndicate rule: ESP, aim, silent aim, projectile redirect and chams only ever
// see bots (PlayerState.bIsABot), never a real player.
bool IsBotState(uintptr_t playerState);
bool IsBotPawn(uintptr_t pawn);
// True while a real player other than us has a pawn in the match (not a bot, not a spectator).
// Weapon mods that change how hard we hit pause while this is true.
bool OtherHumansInMatch();
// One line for the debug overlay: the mesh position and bone array offsets and whether they
// were checked against the running game.
void DescribeOffsetChecks(char* out, int size);
void TickGame();
int CopyPointerArray(uintptr_t base, int offset, uintptr_t* out, int capacity, int start = 0);
void ForEachLevelActor(void (*visit)(uintptr_t actor, void* user), void* user);
// Debug: a feature's current state in one line. Logged as "[feature] name: text" when it
// changes; the debug overlay shows the lines reported in the last 3 seconds.
void FeatureStatus(const char* feature, const char* fmt, ...);
int FeatureStatusLines(char (*out)[160], int max);
