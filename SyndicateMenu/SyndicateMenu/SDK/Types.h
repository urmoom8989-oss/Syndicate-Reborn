#pragma once

#include <cstdint>
#include <string>

struct Vec2 {
    float x, y;
};

struct Vec3 {
    float x, y, z;
    Vec3 operator-(const Vec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
    Vec3 operator+(const Vec3& o) const { return { x + o.x, y + o.y, z + o.z }; }
    float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
};

struct FRotator {
    float Pitch, Yaw, Roll;
};

struct FQuat {
    float x, y, z, w;
};

struct FMatrix {
    float M[4][4];
};

struct FTransform {
    FQuat Rotation;
    Vec3 Translation;
    float pad0;
    Vec3 Scale3D;
    float pad1;
    FMatrix ToMatrixWithScale() const;
};

FMatrix MulMatrix(const FMatrix& a, const FMatrix& b);
Vec3 MatrixOrigin(const FMatrix& m);

struct FBoxSphereBounds {
    Vec3 Origin;
    Vec3 BoxExtent;
    float SphereRadius;
};

struct FName {
    int32_t ComparisonIndex;
    int32_t Number;
    bool operator==(const FName& o) const { return ComparisonIndex == o.ComparisonIndex; }
    bool operator!=(const FName& o) const { return ComparisonIndex != o.ComparisonIndex; }
};

struct FString {
    wchar_t* Data;
    int32_t Num;
    int32_t Max;
};

struct TArrayPtr {
    uintptr_t Data;
    int32_t Num;
    int32_t Max;
    bool Valid() const { return Data && Num > 0 && Num <= Max && Max < 200000; }
};

struct PropInfo {
    bool found;
    int32_t offset;
    uint8_t mask;
};

struct Camera {
    Vec3 loc;
    FRotator rot;
    float fov;
    bool valid;
};

struct BoneSet {
    int Head = -1, Neck = -1, Chest = -1, Pelvis = -1;
    int LUpper = -1, LLower = -1, LHand = -1;
    int RUpper = -1, RLower = -1, RHand = -1;
    int LThigh = -1, LCalf = -1, LFoot = -1;
    int RThigh = -1, RCalf = -1, RFoot = -1;
    bool Found() const { return Head > 0 || Pelvis > 0; }
};

struct Target {
    uintptr_t pawn;
    uintptr_t ps;
    uintptr_t mesh;
    std::string name;
    int team;
    bool visible;
    float dist;
    Vec2 screen;
    Vec3 aimPos;
    BoneSet bones;
    Target() : pawn(0), ps(0), mesh(0), team(0), visible(true), dist(0.f) {
        screen.x = 0.f;
        screen.y = 0.f;
        aimPos.x = 0.f;
        aimPos.y = 0.f;
        aimPos.z = 0.f;
    }
};

Vec3 QuatRotate(const FQuat& q, const Vec3& v);
Vec3 TransformPosition(const FTransform& t, const Vec3& v);
