#pragma once

#include "../Common/d3dUtil.h"

struct Keyframe
{
	Keyframe();
	~Keyframe();

	float TimePos{ 0.0f };
	DirectX::XMFLOAT3 Translation{};
	DirectX::XMFLOAT3 Scale{};
	DirectX::XMFLOAT4 RotationQuat{};
};

struct BoneAnimation
{
	void SetKeyFrame(float timePos, DirectX::XMFLOAT3&& translation, DirectX::XMFLOAT3&& scale, DirectX::XMVECTOR& quat);

	float GetStartTime() const;
	float GetEndTime() const;

	void Interpolate(float t, DirectX::XMFLOAT4X4& M) const;

	std::vector<Keyframe> Keyframes;
};