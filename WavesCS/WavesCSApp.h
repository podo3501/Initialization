#ifdef _DEBUG
	#pragma comment(lib, "../Lib/Common_d.lib")
#else
	#pragma comment(lib, "../Lib/Common.lib")
#endif

#include "../Common/d3dApp.h"
#include "../Common/MathHelper.h"
#include <map>

class Waves;
struct FrameResource;

struct RenderItem
{
	RenderItem() = default;

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
};

enum class RenderLayer : int
{
	Opaque = 0,
	Transparent,
	AlphaTested,
	TexAnim,
	Count
};

enum class GraphicsPSO : int
{
	Opaque,
	AlphaTested,
	Transparent,
	TransparentCS,
	Count
};

constexpr std::array<GraphicsPSO, static_cast<size_t>(GraphicsPSO::Count)> GraphicsPSO_ALL
{
	GraphicsPSO::Opaque,
	GraphicsPSO::AlphaTested,
	GraphicsPSO::Transparent,
	GraphicsPSO::TransparentCS,
};

class WavesCSApp : public D3DApp
{
public:
	WavesCSApp(HINSTANCE hInstance);
	WavesCSApp(const WavesCSApp& rhs) = delete;
	WavesCSApp& operator=(const WavesCSApp& rhs) = delete;
	~WavesCSApp();

	virtual bool Initialize() override;

private:
	virtual void OnResize() override;
	virtual void Update(const GameTimer& gt) override;
	virtual void Draw(const GameTimer& gt) override;

	virtual void OnMouseDown(WPARAM btnState, int x, int y) override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y) override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y) override;

	void OnKeyboardInput(const GameTimer& gt);
	void UpdateCamera(const GameTimer& gt);
	void AnimateMaterials(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMaterialCBs(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);
	void UpdateWaves(const GameTimer& gt);

	void LoadTextures();
	void BuildRootSignature();
	UINT GetNumDescriptors();
	void BuildDescriptorHeaps();
	void BuildShadersAndInputLayout();
	template<typename T>
	void MakeGeometry(const std::string&& geoName, const std::string&& smName,
		std::vector<T>& vertices, const std::vector<std::uint16_t>& indices);
	void BuildLandGeometry();
	void BuildWavesGeometry();
	void BuildBoxGeometry();
	void BuildFrameResources();
	void BuildMaterials();
	D3D12_SHADER_BYTECODE GetShaderBytecode(const std::string&& shaderName);
	void MakeOpaqueDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc);
	void MakeAlphaTestedDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc);
	void MakeTransparentDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc);
	void MakePSOPipelineState(GraphicsPSO psoType);
	void BuildPSOs();
	void BuildRenderItems();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList,
		const std::vector<RenderItem*>& ritems);

	float GetHillsHeight(float x, float z) const;
	DirectX::XMFLOAT3 GetHillsNormal(float x, float z) const;

private:
	void MakeWavesCS(const GameTimer& gt);

private:
	std::unique_ptr<Waves> mWaves = nullptr;

private:
	std::vector<std::unique_ptr<Texture>> mTextures;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mDescriptorHeap = nullptr;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	
	std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3DBlob>> mShaders;
	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::map<RenderLayer, std::vector<RenderItem*>> mRitemLayer;
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;
	RenderItem* mWavesRitem = nullptr;
	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	std::unordered_map<GraphicsPSO, Microsoft::WRL::ComPtr<ID3D12PipelineState>> mGraphicsPSOs;

	int mCurFrameResIdx = 0;
	FrameResource* mCurFrameRes = nullptr;

	DirectX::XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT4X4 mView = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 mProj = MathHelper::Identity4x4();

	float mTheta = 1.5f * DirectX::XM_PI;
	float mPhi = DirectX::XM_PIDIV2 - 0.1f;
	float mRadius = 50.0f;

	float mSunTheta = 1.25f * DirectX::XM_PI;
	float mSunPhi = DirectX::XM_PIDIV4;

	POINT mLastMousePos;
};