#include "AnimationHelper.h"
#include "../Common/Util.h"

using namespace DirectX;

Keyframe::Keyframe()
	: TimePos(0.0f),
	Translation(0.0f, 0.0f, 0.0f),
	Scale(1.0f, 1.0f, 1.0f),
	RotationQuat(0.0f, 0.0f, 0.0f, 1.0f)
{}

Keyframe::~Keyframe()
{}

void BoneAnimation::SetKeyFrame(float timePos, XMFLOAT3&& translation, XMFLOAT3&& scale, XMVECTOR& quat)
{
	Keyframe keyframe;
	keyframe.TimePos = timePos;
	keyframe.Translation = translation;
	keyframe.Scale = scale;
	XMStoreFloat4(&keyframe.RotationQuat, quat);

	Keyframes.emplace_back(keyframe);
}

float BoneAnimation::GetStartTime() const
{
	return Keyframes.front().TimePos;
}

float BoneAnimation::GetEndTime() const
{
	return Keyframes.back().TimePos;
}

void BoneAnimation::Interpolate(float t, XMFLOAT4X4& M) const
{
	auto AffineTransformation = [](const Keyframe& keyframe) {
		XMVECTOR S = XMLoadFloat3(&keyframe.Scale);
		XMVECTOR P = XMLoadFloat3(&keyframe.Translation);
		XMVECTOR Q = XMLoadFloat4(&keyframe.RotationQuat);
		XMVECTOR zero = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
		return XMMatrixAffineTransformation(S, zero, Q, P); };

	if (t <= Keyframes.front().TimePos)
		XMStoreFloat4x4(&M, AffineTransformation(Keyframes.front()));
	else if (t >= Keyframes.back().TimePos)
		XMStoreFloat4x4(&M, AffineTransformation(Keyframes.back()));
	else
	{
		for (auto i : Range(0, (int)Keyframes.size() - 1))
		{
			if (Keyframes[i].TimePos > t) continue;
			if (Keyframes[i + 1].TimePos < t) continue;

			float lerpPercent = (t - Keyframes[i].TimePos) / (Keyframes[i + 1].TimePos - Keyframes[i].TimePos);

			XMVECTOR s0 = XMLoadFloat3(&Keyframes[i].Scale);
			XMVECTOR s1 = XMLoadFloat3(&Keyframes[i + 1].Scale);

			XMVECTOR p0 = XMLoadFloat3(&Keyframes[i].Translation);
			XMVECTOR p1 = XMLoadFloat3(&Keyframes[i + 1].Translation);

			XMVECTOR q0 = XMLoadFloat4(&Keyframes[i].RotationQuat);
			XMVECTOR q1 = XMLoadFloat4(&Keyframes[i + 1].RotationQuat);

			XMVECTOR S = XMVectorLerp(s0, s1, lerpPercent);
			XMVECTOR P = XMVectorLerp(p0, p1, lerpPercent);
			XMVECTOR Q = XMQuaternionSlerp(q0, q1, lerpPercent);
			XMVECTOR zero = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
			XMStoreFloat4x4(&M, XMMatrixAffineTransformation(S, zero, Q, P));

			break;
		}
	}
}