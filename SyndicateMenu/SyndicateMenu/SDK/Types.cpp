#include "Types.h"
#include "../Core/Math.h"

FMatrix FTransform::ToMatrixWithScale() const {
    FMatrix m{};
    m.M[3][0] = Translation.x;
    m.M[3][1] = Translation.y;
    m.M[3][2] = Translation.z;

    float x2 = Rotation.x + Rotation.x;
    float y2 = Rotation.y + Rotation.y;
    float z2 = Rotation.z + Rotation.z;
    float xx2 = Rotation.x * x2;
    float yy2 = Rotation.y * y2;
    float zz2 = Rotation.z * z2;
    m.M[0][0] = (1.f - (yy2 + zz2)) * Scale3D.x;
    m.M[1][1] = (1.f - (xx2 + zz2)) * Scale3D.y;
    m.M[2][2] = (1.f - (xx2 + yy2)) * Scale3D.z;

    float yz2 = Rotation.y * z2;
    float wx2 = Rotation.w * x2;
    m.M[2][1] = (yz2 - wx2) * Scale3D.z;
    m.M[1][2] = (yz2 + wx2) * Scale3D.y;

    float xy2 = Rotation.x * y2;
    float wz2 = Rotation.w * z2;
    m.M[1][0] = (xy2 - wz2) * Scale3D.y;
    m.M[0][1] = (xy2 + wz2) * Scale3D.x;

    float xz2 = Rotation.x * z2;
    float wy2 = Rotation.w * y2;
    m.M[2][0] = (xz2 + wy2) * Scale3D.z;
    m.M[0][2] = (xz2 - wy2) * Scale3D.x;

    m.M[0][3] = 0.f;
    m.M[1][3] = 0.f;
    m.M[2][3] = 0.f;
    m.M[3][3] = 1.f;
    return m;
}

FMatrix MulMatrix(const FMatrix& a, const FMatrix& b) {
    FMatrix out{};
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            out.M[r][c] = a.M[r][0] * b.M[0][c] + a.M[r][1] * b.M[1][c] +
                          a.M[r][2] * b.M[2][c] + a.M[r][3] * b.M[3][c];
        }
    }
    return out;
}

Vec3 MatrixOrigin(const FMatrix& m) {
    return { m.M[3][0], m.M[3][1], m.M[3][2] };
}

Vec3 QuatRotate(const FQuat& q, const Vec3& v) {
    Vec3 qv{ q.x, q.y, q.z };
    Vec3 t{ qv.y * v.z - qv.z * v.y, qv.z * v.x - qv.x * v.z, qv.x * v.y - qv.y * v.x };
    t = { t.x * 2.f, t.y * 2.f, t.z * 2.f };
    Vec3 r{ qv.y * t.z - qv.z * t.y, qv.z * t.x - qv.x * t.z, qv.x * t.y - qv.y * t.x };
    return { v.x + q.w * t.x + r.x, v.y + q.w * t.y + r.y, v.z + q.w * t.z + r.z };
}

Vec3 TransformPosition(const FTransform& t, const Vec3& v) {
    Vec3 r = QuatRotate(t.Rotation, v);
    return { r.x * t.Scale3D.x + t.Translation.x,
             r.y * t.Scale3D.y + t.Translation.y,
             r.z * t.Scale3D.z + t.Translation.z };
}
