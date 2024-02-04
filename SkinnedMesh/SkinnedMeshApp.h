#ifdef _DEBUG
	#pragma comment(lib, "../Lib/Common_d.lib")
#else
	#pragma comment(lib, "../Lib/Common.lib")
#endif

#include "../Common/d3dApp.h"
#include "../Common/MathHelper.h"
#include "../Common/Camera.h"
#include "FrameResource.h"
#include "SkinnedData.h"
#include "LoadM3d.h"
#include <map>

class ShadowMap;
class Ssao;
struct FrameResource;

struct SkinnedModelInstance
{
	SkinnedData* SkinnedInfo = nullptr;
	std::vector<DirectX::XMFLOAT4X4> FinalTransforms;
	std::string ClipName;
	float TimePos{ 0.0f };

	void UpdateSkinnedAnimation(float dt)
	{
		TimePos += dt;

		if (TimePos > SkinnedInfo->GetClipEndTime(ClipName))
			TimePos = 0.0f;

		SkinnedInfo->GetFinalTransforms(ClipName, TimePos, FinalTransforms);
	}
};

struct RenderItem
{
	RenderItem() = default;
	RenderItem(const RenderItem& rhs) = delete;

	DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

	int NumFramesDirty = gNumFrameResources;
	UINT ObjCBIndex = -1;

	Material* Mat = nullptr;
	MeshGeometry* Geo = nullptr;

	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;
	DirectX::BoundingBox BBounds{};
	DirectX::BoundingSphere BSphere{};

	bool Visible = true;

	UINT SkinnedCBIndex = -1;

	SkinnedModelInstance* SkinnedModelInst = nullptr;
};

enum class RenderLayer : int
{
	Opaque = 0,
	SkinnedOpaque,
	Debug,
	Sky,
	Count
};

enum class GraphicsPSO : int
{
	Opaque = 0,
	ShadowOpaque,
	Debug,
	DrawNormals,
	Ssao,
	SsaoBlur,
	Sky,
	Count
};

constexpr std::array<GraphicsPSO, static_cast<size_t>(GraphicsPSO::Count)> GraphicsPSO_ALL
{
	GraphicsPSO::Opaque,
	GraphicsPSO::ShadowOpaque,
	GraphicsPSO::Debug,
	GraphicsPSO::DrawNormals,
	GraphicsPSO::Ssao,
	GraphicsPSO::SsaoBlur,
	GraphicsPSO::Sky,
};

class SkinnedMeshApp : public D3DApp
{
public:
	SkinnedMeshApp(HINSTANCE hInstance);
	SkinnedMeshApp(const SkinnedMeshApp& rhs) = delete;
	SkinnedMeshApp& operator=(const SkinnedMeshApp& rhs) = delete;
	~SkinnedMeshApp();

	virtual bool Initialize() override;

private:
	virtual void CreateRtvAndDsvDescriptorHeaps() override;
	virtual void OnResize() override;
	virtual void Update(const GameTimer& gt) override;
	virtual void Draw(const GameTimer& gt) override;

	virtual void OnMouseDown(WPARAM btnState, int x, int y) override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y) override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y) override;

	void OnKeyboardInput(const GameTimer& gt);
	void AnimateMaterials(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMaterialBuffer(const GameTimer& gt);
	void UpdateShadowTransform(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);
	void UpdateShadowPassCB(const GameTimer& gt);
	void UpdateSsaoCB(const GameTimer& gt);

	void LoadSkinnedModel();
	void LoadTextures();
	void BuildRootSignature();
	void BuildSsaoRootSignature();
	void BuildDescriptorHeaps();
	void BuildShadersAndInputLayout();
	void BuildShapeGeometry();
	void BuildSkullGeometry();
	void BuildFrameResources();
	void BuildMaterials();
	void MakeBaseDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc);
	void MakeOpaqueDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc);
	void MakeShadowOpaqueDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc);
	void MakeDebugDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc);
	void MakeDrawNormals(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc);
	void MakeSsao(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc);
	void MakeSsaoBlur(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc);
	void MakeSkyDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc);
	void MakePSOPipelineState(GraphicsPSO psoType);
	void BuildPSOs();
	void BuildRenderItems();
	void DrawRenderItems(
		ID3D12GraphicsCommandList* cmdList,
		const std::vector<RenderItem*> ritems);
	void DrawSceneToShadowMap();
	void DrawNormalsAndDepth();

	CD3DX12_CPU_DESCRIPTOR_HANDLE GetCpuSrv(int index) const;
	CD3DX12_GPU_DESCRIPTOR_HANDLE GetGpuSrv(int index) const;
	CD3DX12_CPU_DESCRIPTOR_HANDLE GetDsv(int index) const;
	CD3DX12_CPU_DESCRIPTOR_HANDLE GetRtv(int index) const;

private:
	DirectX::BoundingSphere mSceneBounds{};

	std::unique_ptr<Ssao> mSsao{ nullptr };

	std::unique_ptr<ShadowMap> mShadowMap{ nullptr };
	UINT mShadowMapHeapIndex{ 0 };
	UINT mNullCubeSrvIndex{ 0 };
	UINT mNullTexSrvIndex{ 0 };
	CD3DX12_GPU_DESCRIPTOR_HANDLE mNullSrv{};

	std::vector<std::unique_ptr<Texture>> mTextures;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> mSsaoRootSignature = nullptr;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

	std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3DBlob>> mShaders;
	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;
	//std::vector<RenderItem*> mOpaqueRitems;
	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	std::unordered_map<GraphicsPSO, Microsoft::WRL::ComPtr<ID3D12PipelineState>> mPSOs;
	std::unordered_map<RenderLayer, std::vector<RenderItem*>> mRitemLayer;
	RenderItem* mPickedRitem = nullptr;
	FrameResource* mCurFrameRes = nullptr;

	PassConstants mMainPassCB{};

	UINT mSkyTexHeapIndex = 0;
	UINT mFrameResIdx = 0;

	float mLightNearZ = 0.0f;
	float mLightFarZ = 0.0f;
	DirectX::XMFLOAT3 mLightPosW;
	DirectX::XMFLOAT4X4 mLightView = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 mLightProj = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 mShadowTransform = MathHelper::Identity4x4();

	float mLightRotationAngle = 0.0f;
	DirectX::XMFLOAT3 mBaseLightDirections[3] = {
		DirectX::XMFLOAT3(0.57735f, -0.57735f, 0.57735f),
		DirectX::XMFLOAT3(-0.57735f, -0.57735f, 0.57735f),
		DirectX::XMFLOAT3(0.0f, -0.707f, -0.707f)
	};
	DirectX::XMFLOAT3 mRotatedLightDirections[3]{};
	POINT mLastMousePos;

	Camera mCamera;

	UINT mSkinnedSrvHeapStart = 0;
	std::string mSkinnedModelFilename{ "Models/soldier.m3d" };
	std::unique_ptr<SkinnedModelInstance> mSkinnedModelInst{ nullptr };
	SkinnedData mSkinnedInfo{};
	std::vector<M3DLoader::Subset> mSkinnedSubsets;
	std::vector<M3DLoader::M3dMaterial> mSkinnedMats;
	std::vector<std::string> mSkinnedTextureNames;
	
};
