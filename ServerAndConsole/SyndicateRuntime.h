#pragma once
#include "framework.h"
#include <cmath>
#include <mutex>
#include <random>
namespace Syndicate
{
inline float Now()
{
    auto W = UWorld::GetWorld();
    return W ? UGameplayStatics::GetTimeSeconds(W) : 0.f;
}
inline bool Finite(const FVector &V)
{
    return std::isfinite(V.X) && std::isfinite(V.Y) && std::isfinite(V.Z);
}
inline float Distance2D(const FVector &A, const FVector &B)
{
    return std::hypot(A.X - B.X, A.Y - B.Y);
}
inline std::mt19937 &Random()
{
    static std::mt19937 R(std::random_device{}());
    return R;
}
inline float Between(float A, float B)
{
    return std::uniform_real_distribution<float>(A, B)(Random());
}
inline int Pick(int N)
{
    return N > 0 ? std::uniform_int_distribution<int>(0, N - 1)(Random()) : -1;
}
// Capture registry identity once; validate it before dereferencing cached objects.
template <class T> struct Ref
{
    T *Pointer = nullptr;
    int Index = -1;
    mutable int Serial = 0;
    FName Name{};
    Ref() = default;
    Ref(T *P) { Set(P); }
    void Set(T *P)
    {
        Pointer = P;
        Index = P ? P->Index : -1;
        Name = P ? P->Name : FName{};
        auto Item = P && UObject::GObjects ? UObject::GObjects->GetItemByIndex(Index) : nullptr;
        Serial = Item ? Item->SerialNumber : 0;
    }
    T *Get() const
    {
        if (!Pointer || !UObject::GObjects || UObject::GObjects->GetByIndex(Index) != Pointer)
            return nullptr;
        auto Item = UObject::GObjects->GetItemByIndex(Index);
        if (!Item || (Item->Flags & ((1 << 28) | (1 << 29))))
            return nullptr;
        // Native slots start at serial zero. Their first weak handle assigns a
        // positive serial without replacing the object; adopt that first value.
        if (!Serial)
            Serial = Item->SerialNumber;
        if (Item->SerialNumber != Serial)
            return nullptr;
        if (Pointer->Name != Name || !UKismetSystemLibrary::IsValid(Pointer))
            return nullptr;
        return Pointer;
    }
    void Clear()
    {
        Pointer = nullptr;
        Index = -1;
        Serial = 0;
    }
};
template <class T> inline T *As(UObject *P)
{
    return P && P->IsA(T::StaticClass()) ? static_cast<T *>(P) : nullptr;
}
template <class T> inline std::vector<Ref<T>> ActorsOf(UClass *C = T::StaticClass())
{
    std::vector<Ref<T>> Out;
    TArray<AActor *> A;
    if (UWorld::GetWorld() && C)
        UGameplayStatics::GetAllActorsOfClass(UWorld::GetWorld(), C, &A);
    for (auto P : A)
        if (P && !P->bActorIsBeingDestroyed)
            Out.emplace_back(static_cast<T *>(P));
    A.Free();
    return Out;
}
inline bool Project(const FVector &G, FVector &Out)
{
    return Finite(G) && UWorld::GetWorld() && UWorld::GetWorld()->NavigationSystem &&
           UNavigationSystemV1::K2_ProjectPointToNavigation(UWorld::GetWorld(), G, &Out, nullptr, nullptr,
                                                            FVector(160, 160, 600));
}
} // namespace Syndicate
