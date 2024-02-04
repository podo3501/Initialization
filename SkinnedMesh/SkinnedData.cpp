#include "SkinnedData.h"
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

float AnimationClip::GetClipStartTime() const
{
	float t = MathHelper::Infinity;
	for (UINT i = 0; i < BoneAnimations.size(); ++i)
	{
		t = MathHelper::Min(t, BoneAnimations[i].GetStartTime());
	}

	return t;
}

float AnimationClip::GetClipEndTime() const
{
	float t = 0.0f;
	for (UINT i = 0; i < BoneAnimations.size(); ++i)
	{
		t = MathHelper::Max(t, BoneAnimations[i].GetEndTime());
	}

	return t;
}

void AnimationClip::Interpolate(float t, std::vector<XMFLOAT4X4>& boneTransforms) const
{
	for (UINT i = 0; i < BoneAnimations.size(); ++i)
	{
		BoneAnimations[i].Interpolate(t, boneTransforms[i]);
	}
}

float SkinnedData::GetClipStartTime(const std::string& clipName) const
{
	auto clip = mAnimations.find(clipName);
	return clip->second.GetClipStartTime();
}

float SkinnedData::GetClipEndTime(const std::string& clipName) const
{
	auto clip = mAnimations.find(clipName);
	return clip->second.GetClipEndTime();
}

UINT SkinnedData::BoneCount() const
{
	return static_cast<UINT>(mBoneHierarchy.size());
}

void SkinnedData::Set(
	std::vector<int>& boneHierarchy,
	std::vector<XMFLOAT4X4>& boneOffsets,
	std::unordered_map<std::string, AnimationClip>& animations)
{
	mBoneHierarchy = boneHierarchy;
	mBoneOffsets = boneOffsets;
	mAnimations = animations;
}

void SkinnedData::GetFinalTransforms(
	const std::string& clipName, float timePos, std::vector<XMFLOAT4X4>& finalTransforms) const
{
	UINT numBones = static_cast<UINT>(mBoneOffsets.size());

	std::vector<XMFLOAT4X4> toParentTransforms(numBones);

	auto clip = mAnimations.find(clipName);
	clip->second.Interpolate(timePos, toParentTransforms);

	std::vector<XMFLOAT4X4> toRootTransforms(numBones);
	toRootTransforms[0] = toParentTransforms[0];

	for (auto i : Range(1, numBones))
	{
		XMMATRIX toParent = XMLoadFloat4x4(&toParentTransforms[i]);
		int parentIndex = mBoneHierarchy[i];
		XMMATRIX parentToRoot = XMLoadFloat4x4(&toRootTransforms[parentIndex]);
		XMMATRIX toRoot = XMMatrixMultiply(toParent, parentToRoot);

		XMStoreFloat4x4(&toRootTransforms[i], toRoot);
	}

	for (auto i : Range(0, numBones))
	{
		XMMATRIX offset = XMLoadFloat4x4(&mBoneOffsets[i]);
		XMMATRIX toRoot = XMLoadFloat4x4(&toRootTransforms[i]);
		XMMATRIX finalTransform = XMMatrixMultiply(offset, toRoot);
		XMStoreFloat4x4(&finalTransforms[i], XMMatrixTranspose(finalTransform));
	}
}
