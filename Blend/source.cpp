#include "header.h"
#include "../Common/d3dApp.h"
#include "../Common/MathHelper.h"
#include "../Common/UploadBuffer.h"
#include "../Common/GeometryGenerator.h"
#include "FrameResource.h"
#include "Waves.h"
#include "../Common/Util.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

const int gNumFrameResources = 3;

struct RenderItem
{
	RenderItem() = default;

	XMFLOAT4X4 World = MathHelper::Identity4x4();
	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

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
	Screen,
	Count
};

class BlendApp : public D3DApp
{
public:
	BlendApp(HINSTANCE hInstance);
	BlendApp(const BlendApp& rhs) = delete;
	BlendApp& operator=(const BlendApp& rhs) = delete;
	~BlendApp();

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
	void BuildDescriptorHeaps();
	void BuildShadersAndInputLayout();
	void BuildLandGeometry();
	void BuildWavesGeometry();
	void BuildBoxGeometry();
	void BuildCylinderGeometry();
	void BuildScreenGridGeometry();
	void BuildPSOs();
	void BuildFrameResources();
	void BuildMaterials();
	void BuildRenderItems();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, 
		const std::vector<RenderItem*>& ritems, const GameTimer& gt);
	
	float GetHillsHeight(float x, float z) const;
	XMFLOAT3 GetHillsNormal(float x, float z) const;
	
private:
	UINT mCbvSrvDescriptorSize = 0;
	std::unique_ptr<Waves> mWaves;

private:
	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	ComPtr<ID3D12DescriptorHeap> mDescriptorHeap = nullptr;
	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
	
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
	//std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
	std::vector<std::unique_ptr<Texture>> mTextures;
	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;

	std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count]{};
	RenderItem* mWavesRitem = nullptr;
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;
	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

	int nFrameResourceIndex = 0;
	FrameResource* mCurrFrameResource = nullptr;
	PassConstants mPassConstants{};

	XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();

	float mTheta = 1.5f * XM_PI;
	float mPhi = XM_PIDIV2 - 0.1f;
	float mRadius = 50.0f;

	float mSunTheta = 1.25f * XM_PI;
	float mSunPhi = XM_PIDIV4;

	POINT mLastMousePos;
};

BlendApp::BlendApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
}

BlendApp::~BlendApp()
{
	if (md3dDevice != nullptr)
		FlushCommandQueue();
}

bool BlendApp::Initialize()
{
	if (!D3DApp::Initialize())
		return false;

	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	mWaves = std::make_unique<Waves>(128, 128, 1.0f, 0.03f, 4.0f, 0.2f);

	LoadTextures();
	BuildRootSignature();
	BuildDescriptorHeaps();
	BuildShadersAndInputLayout();
	BuildLandGeometry();
	BuildWavesGeometry();
	BuildBoxGeometry();
	BuildCylinderGeometry();
	BuildScreenGridGeometry();
	BuildMaterials();
	BuildRenderItems();
	BuildFrameResources();
	BuildPSOs();

	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	FlushCommandQueue();

	return true;
}

void BlendApp::LoadTextures()
{
	auto CreateTexture = [&](std::wstring&& filename)
	{
		auto texture = std::make_unique<Texture>();
		ThrowIfFailed(d3dUtil::LoadTextureFromFile(md3dDevice.Get(), mCommandList.Get(),
			filename.c_str(), texture->Resource, texture->UploadHeap));
		//mTextures[name] = std::move(texture);
		mTextures.emplace_back(std::move(texture));
	};
	CreateTexture(L"../Textures/grass.dds");
	CreateTexture(L"../Textures/water1.dds");
	CreateTexture(L"../Textures/WireFence.dds");
	for (auto i : Range(1, 61))
	{
		std::string sNum;
		if (0 <= i && i < 10) sNum = "0";
		std::string name = "Bolt0" + sNum + std::to_string(i);

		std::wstring wName;
		wName.assign(name.begin(), name.end()); 
		std::wstring filename = L"../Textures/BoltAnim/" + wName + L".bmp";
		CreateTexture(filename.c_str());
	}
}

std::array<const D3D12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers()
{
	const CD3DX12_STATIC_SAMPLER_DESC pointWrap{ 
		0, D3D12_FILTER_MIN_MAG_MIP_POINT,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP };

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp{
		1, D3D12_FILTER_MIN_MAG_MIP_POINT,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP };

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap{
		2, D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP };

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp{
		3, D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP };

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap{
		4, D3D12_FILTER_ANISOTROPIC,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP, 0.0f, 8 };

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp{
		5, D3D12_FILTER_ANISOTROPIC,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 0.0f, 8 };
	
	return{ pointWrap, pointClamp, 
		linearWrap, linearClamp, 
		anisotropicWrap, anisotropicClamp };
}

void BlendApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0 };

	CD3DX12_ROOT_PARAMETER rootParameter[4];
	rootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);
	rootParameter[1].InitAsConstantBufferView(0);
	rootParameter[2].InitAsConstantBufferView(1);
	rootParameter[3].InitAsConstantBufferView(2);

	auto staticSamplers = GetStaticSamplers();

	CD3DX12_ROOT_SIGNATURE_DESC desc{ 4, rootParameter, (UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT };

	ComPtr<ID3DBlob> serializedSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, 
		serializedSig.GetAddressOf(), errorBlob.GetAddressOf());
	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(0, 
		serializedSig->GetBufferPointer(), serializedSig->GetBufferSize(), IID_PPV_ARGS(&mRootSignature)));
}

void BlendApp::BuildDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc{ 0 };
	heapDesc.NumDescriptors = (UINT)mTextures.size();
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mDescriptorHeap)));

	auto CreateShaderResourceView = [&, count{ 0 }](ComPtr<ID3D12Resource> texRes) mutable
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE hDesc;
		hDesc.InitOffsetted(mDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), count++, mCbvSrvDescriptorSize);

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{ 0 };
		srvDesc.Format = texRes->GetDesc().Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MipLevels = texRes->GetDesc().MipLevels;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
		md3dDevice->CreateShaderResourceView(texRes.Get(), &srvDesc, hDesc);
	};

	for_each(mTextures.begin(), mTextures.end(), [&](auto& tex) {
		CreateShaderResourceView(tex->Resource); });
}

D3D_SHADER_MACRO macroScreen[] = { "SCREEN", "1", NULL, NULL };
D3D_SHADER_MACRO macroFog[] = { "FOaG", "1", NULL, NULL };
D3D_SHADER_MACRO macroAlphaAndFog[] = { "FOaG", "1", "ALPHA_TEST", "1", NULL, NULL };

void BlendApp::BuildShadersAndInputLayout()
{
	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders/Default.hlsl", nullptr, "VS", "vs_5_0");
	mShaders["screenVS"] = d3dUtil::CompileShader(L"Shaders/Default.hlsl", macroScreen, "VS", "vs_5_0");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders/Default.hlsl", macroFog, "PS", "ps_5_0");
	mShaders["alphaTestPS"] = d3dUtil::CompileShader(L"Shaders/Default.hlsl", macroAlphaAndFog, "PS", "ps_5_0");
	mShaders["screenPS"] = d3dUtil::CompileShader(L"Shaders/Default.hlsl", nullptr, "PS_SCREEN", "ps_5_0");

	mInputLayout =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}

float BlendApp::GetHillsHeight(float x, float z) const
{
	return 0.3f * (z * sinf(0.1f * x) + x * cosf(0.1f * z));
}

XMFLOAT3 BlendApp::GetHillsNormal(float x, float z) const
{
	XMFLOAT3 n(
		-0.03f * z * cosf(0.1f * x) - 0.3f * cosf(0.1f * z),
		1.0f,
		-0.3f * sinf(0.1f * x) + 0.03f * x * sinf(0.1f * z));
	
	XMVECTOR uintNormal = XMVector3Normalize(XMLoadFloat3(&n));
	XMStoreFloat3(&n, uintNormal);

	return n;
}

void BlendApp::BuildLandGeometry()
{
	GeometryGenerator gen;
	GeometryGenerator::MeshData grid = gen.CreateGrid(160.0f, 160.0f, 50, 50);

	UINT totalVCnt = static_cast<UINT>(grid.Vertices.size());
	UINT vertexBufferByteSize = totalVCnt * sizeof(Vertex);
	std::vector<Vertex> vertices(totalVCnt);
	for (auto i : Range(0, totalVCnt))
	{
		vertices[i].Pos = grid.Vertices[i].Position;
		vertices[i].Pos.y = GetHillsHeight(vertices[i].Pos.x, vertices[i].Pos.z);
		vertices[i].Normal = GetHillsNormal(vertices[i].Pos.x, vertices[i].Pos.z);
		vertices[i].TexC = grid.Vertices[i].TexC;
	}
	
	UINT totalICnt = static_cast<UINT>(grid.Indices32.size());
	UINT indexBufferByteSize = totalICnt * sizeof(std::uint16_t);
	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), grid.GetIndices16().begin(), grid.GetIndices16().end());
	
	auto mgLand = std::make_unique<MeshGeometry>();
	mgLand->Name = "landGeo";
	mgLand->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
		vertices.data(), vertexBufferByteSize, mgLand->VertexBufferUploader);
	mgLand->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
		indices.data(), indexBufferByteSize, mgLand->IndexBufferUploader);	
	mgLand->DrawArgs["grid"] = std::move(SubmeshGeometry{ totalICnt, 0, 0, {} });
	mgLand->IndexBufferByteSize = indexBufferByteSize;
	mgLand->VertexBufferByteSize = vertexBufferByteSize;
	mgLand->IndexFormat = DXGI_FORMAT_R16_UINT;
	mgLand->VertexByteStride = sizeof(Vertex);

	mGeometries[mgLand->Name] = std::move(mgLand);
}

void BlendApp::BuildWavesGeometry()
{
	UINT totalICnt = 3 * mWaves->TriangleCount();
	UINT indexBufferByteSize = totalICnt * sizeof(std::uint16_t);
	UINT totalVCnt = mWaves->VertexCount();
	UINT vertexBufferByteSize = totalVCnt * sizeof(Vertex);
	std::vector<std::uint16_t> indices(totalICnt); // 3 indices per face
	assert(totalVCnt < 0x0000ffff);

	// Iterate over each quad.
	int m = mWaves->RowCount();
	int n = mWaves->ColumnCount();
	int k = 0;
	for (int i = 0; i < m - 1; ++i)
	{
		for (int j = 0; j < n - 1; ++j)
		{
			indices[k] = i * n + j;
			indices[k + 1] = i * n + j + 1;
			indices[k + 2] = (i + 1) * n + j;

			indices[k + 3] = (i + 1) * n + j;
			indices[k + 4] = i * n + j + 1;
			indices[k + 5] = (i + 1) * n + j + 1;

			k += 6; // next quad
		}
	}

	auto mgWaves = std::make_unique<MeshGeometry>();
	mgWaves->VertexBufferCPU = nullptr;
	mgWaves->VertexBufferGPU = nullptr;
	mgWaves->Name = "waterGeo";
	mgWaves->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
		indices.data(), indexBufferByteSize, mgWaves->IndexBufferUploader);
	mgWaves->DrawArgs["grid"] = std::move(SubmeshGeometry{totalICnt, 0, 0, {}});
	mgWaves->IndexFormat = DXGI_FORMAT_R16_UINT;
	mgWaves->IndexBufferByteSize = indexBufferByteSize;
	mgWaves->VertexByteStride = sizeof(Vertex);
	mgWaves->VertexBufferByteSize = vertexBufferByteSize;
	mGeometries[mgWaves->Name] = std::move(mgWaves);
}

void BlendApp::BuildBoxGeometry()
{
	GeometryGenerator gen;
	GeometryGenerator::MeshData box = gen.CreateBox(8.0f, 8.0f, 8.0f, 3);

	UINT totalVCnt = static_cast<UINT>(box.Vertices.size());
	UINT totalICnt = static_cast<UINT>(box.GetIndices16().size());
	UINT vertexBufferByteSize = totalVCnt * sizeof(Vertex);
	UINT indexBufferByteSize = totalICnt * sizeof(std::uint16_t);

	std::vector<Vertex> vertices(totalVCnt);
	for (auto i : Range(0, totalVCnt))
	{
		vertices[i].Pos = box.Vertices[i].Position;
		vertices[i].Normal = box.Vertices[i].Normal;
		vertices[i].TexC = box.Vertices[i].TexC;
	}
	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), box.GetIndices16().begin(), box.GetIndices16().end());

	auto mgBox = std::make_unique<MeshGeometry>();
	mgBox->Name = "boxGeo";
	mgBox->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
		vertices.data(), vertexBufferByteSize, mgBox->VertexBufferUploader);
	mgBox->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
		indices.data(), indexBufferByteSize, mgBox->IndexBufferUploader);
	mgBox->DrawArgs["box"] = std::move(SubmeshGeometry{ totalICnt, 0, 0, {} });
	mgBox->IndexBufferByteSize = indexBufferByteSize;
	mgBox->IndexFormat = DXGI_FORMAT_R16_UINT;
	mgBox->VertexBufferByteSize = vertexBufferByteSize;
	mgBox->VertexByteStride = sizeof(Vertex);

	mGeometries[mgBox->Name] = std::move(mgBox);
}

void BlendApp::BuildCylinderGeometry()
{
	GeometryGenerator gen;
	GeometryGenerator::MeshData cylinder = gen.CreateCylinder(7.0f, 7.0f, 9.0f, 30, 10);

	UINT totalVCnt = static_cast<UINT>(cylinder.Vertices.size());
	UINT totalICnt = static_cast<UINT>(cylinder.GetIndices16().size());
	UINT vertexBufferByteSize = totalVCnt * sizeof(Vertex);
	UINT indexBufferByteSize = totalICnt * sizeof(std::uint16_t);

	std::vector<Vertex> vertices(totalVCnt);
	for (auto i : Range(0, totalVCnt))
	{
		vertices[i].Pos = cylinder.Vertices[i].Position;
		vertices[i].Normal = cylinder.Vertices[i].Normal;
		vertices[i].TexC = cylinder.Vertices[i].TexC;
	}
	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), cylinder.GetIndices16().begin(), cylinder.GetIndices16().end());

	auto mgCylinder = std::make_unique<MeshGeometry>();
	mgCylinder->Name = "cylinderGeo";
	mgCylinder->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
		vertices.data(), vertexBufferByteSize, mgCylinder->VertexBufferUploader);
	mgCylinder->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
		indices.data(), indexBufferByteSize, mgCylinder->IndexBufferUploader);
	mgCylinder->DrawArgs["cylinder"] = std::move(SubmeshGeometry{ totalICnt, 0, 0, {} });
	mgCylinder->IndexBufferByteSize = indexBufferByteSize;
	mgCylinder->IndexFormat = DXGI_FORMAT_R16_UINT;
	mgCylinder->VertexBufferByteSize = vertexBufferByteSize;
	mgCylinder->VertexByteStride = sizeof(Vertex);

	mGeometries[mgCylinder->Name] = std::move(mgCylinder);
}

void BlendApp::BuildScreenGridGeometry()
{
	GeometryGenerator gen;
	GeometryGenerator::MeshData screenGrid = gen.CreateGrid((float)mClientWidth, (float)mClientHeight, 2, 2);

	std::vector<Vertex> vertices(4);
	vertices[0].Pos = { -1.0f, 1.0f, 0.0f };
	vertices[1].Pos = { 1.0f, 1.0f, 0.0f };
	vertices[2].Pos = { 1.0f, -1.0f, 0.0f };
	vertices[3].Pos = { -1.0f, -1.0f, 0.0f };

	std::vector<std::uint16_t> indices;
	indices = { 0, 1, 2, 0, 2, 3 };

	UINT totalVCnt = 4;
	UINT totalICnt = 6;
	UINT vertexBufferByteSize = totalVCnt * sizeof(Vertex);
	UINT indexBufferByteSize = totalICnt * sizeof(std::uint16_t);

	auto mgScreenGrid = std::make_unique<MeshGeometry>();
	mgScreenGrid->Name = "screenGeo";
	mgScreenGrid->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
		vertices.data(), vertexBufferByteSize, mgScreenGrid->VertexBufferUploader);
	mgScreenGrid->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
		indices.data(), indexBufferByteSize, mgScreenGrid->IndexBufferUploader);
	mgScreenGrid->DrawArgs["screen"] = std::move(SubmeshGeometry{ totalICnt, 0, 0, {} });
	mgScreenGrid->IndexBufferByteSize = indexBufferByteSize;
	mgScreenGrid->IndexFormat = DXGI_FORMAT_R16_UINT;
	mgScreenGrid->VertexBufferByteSize = vertexBufferByteSize;
	mgScreenGrid->VertexByteStride = sizeof(Vertex);

	mGeometries[mgScreenGrid->Name] = std::move(mgScreenGrid);
}

void BlendApp::BuildMaterials()
{
	auto grass = std::make_unique<Material>();
	grass->Name = "grass";
	grass->MatCBIndex = 0;
	grass->DiffuseSrvHeapIndex = 0;
	grass->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	grass->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
	grass->Roughness = 0.125f;

	// This is not a good water material definition, but we do not have all the rendering
	// tools we need (transparency, environment reflection), so we fake it for now.
	auto water = std::make_unique<Material>();
	water->Name = "water";
	water->MatCBIndex = 1;
	water->DiffuseSrvHeapIndex = 1;
	water->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.5f);
	water->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	water->Roughness = 0.0f;

	auto wirefence = std::make_unique<Material>();
	wirefence->Name = "wirefence";
	wirefence->MatCBIndex = 2;
	wirefence->DiffuseSrvHeapIndex = 2;
	wirefence->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	wirefence->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	wirefence->Roughness = 0.25f;

	auto cylinder = std::make_unique<Material>();
	cylinder->Name = "cylinder";
	cylinder->MatCBIndex = 3;
	cylinder->DiffuseSrvHeapIndex = 3;
	cylinder->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	cylinder->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	cylinder->Roughness = 0.25f;

	auto screen0 = std::make_unique<Material>();
	screen0->Name = "screen0";
	screen0->MatCBIndex = 4;
	screen0->DiffuseSrvHeapIndex = 0;
	screen0->DiffuseAlbedo = XMFLOAT4(1.0f, 0.0f, 0.0f, 0.5f);
	screen0->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	screen0->Roughness = 0.25f;

	auto screen1 = std::make_unique<Material>();
	screen1->Name = "screen1";
	screen1->MatCBIndex = 5;
	screen1->DiffuseSrvHeapIndex = 0;
	screen1->DiffuseAlbedo = XMFLOAT4(0.9f, 0.0f, 0.0f, 0.5f);
	screen1->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	screen1->Roughness = 0.25f;

	auto screen2 = std::make_unique<Material>();
	screen2->Name = "screen2";
	screen2->MatCBIndex = 6;
	screen2->DiffuseSrvHeapIndex = 0;
	screen2->DiffuseAlbedo = XMFLOAT4(0.8f, 0.0f, 0.0f, 0.5f);
	screen2->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	screen2->Roughness = 0.25f;

	auto screen3 = std::make_unique<Material>();
	screen3->Name = "screen3";
	screen3->MatCBIndex = 7;
	screen3->DiffuseSrvHeapIndex = 0;
	screen3->DiffuseAlbedo = XMFLOAT4(0.7f, 0.0f, 0.0f, 0.5f);
	screen3->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	screen3->Roughness = 0.25f;

	auto screen4 = std::make_unique<Material>();
	screen4->Name = "screen4";
	screen4->MatCBIndex = 8;
	screen4->DiffuseSrvHeapIndex = 0;
	screen4->DiffuseAlbedo = XMFLOAT4(0.6f, 0.0f, 0.0f, 0.5f);
	screen4->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	screen4->Roughness = 0.25f;

	auto screen5 = std::make_unique<Material>();
	screen5->Name = "screen5";
	screen5->MatCBIndex = 9;
	screen5->DiffuseSrvHeapIndex = 0;
	screen5->DiffuseAlbedo = XMFLOAT4(0.5f, 0.0f, 0.0f, 0.5f);
	screen5->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	screen5->Roughness = 0.25f;

	auto screen6 = std::make_unique<Material>();
	screen6->Name = "screen6";
	screen6->MatCBIndex = 10;
	screen6->DiffuseSrvHeapIndex = 0;
	screen6->DiffuseAlbedo = XMFLOAT4(0.4f, 0.0f, 0.0f, 0.5f);
	screen6->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	screen6->Roughness = 0.25f;

	auto screen7 = std::make_unique<Material>();
	screen7->Name = "screen7";
	screen7->MatCBIndex = 11;
	screen7->DiffuseSrvHeapIndex = 0;
	screen7->DiffuseAlbedo = XMFLOAT4(0.3f, 0.0f, 0.0f, 0.5f);
	screen7->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	screen7->Roughness = 0.25f;

	auto screen8 = std::make_unique<Material>();
	screen8->Name = "screen8";
	screen8->MatCBIndex = 12;
	screen8->DiffuseSrvHeapIndex = 0;
	screen8->DiffuseAlbedo = XMFLOAT4(0.2f, 0.0f, 0.0f, 0.5f);
	screen8->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	screen8->Roughness = 0.25f;

	auto screen9 = std::make_unique<Material>();
	screen9->Name = "screen9";
	screen9->MatCBIndex = 13;
	screen9->DiffuseSrvHeapIndex = 0;
	screen9->DiffuseAlbedo = XMFLOAT4(0.1f, 0.0f, 0.0f, 0.5f);
	screen9->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	screen9->Roughness = 0.25f;

	mMaterials["grass"] = std::move(grass);
	mMaterials["water"] = std::move(water);
	mMaterials["wirefence"] = std::move(wirefence);
	mMaterials["cylinder"] = std::move(cylinder);
	mMaterials["screen0"] = std::move(screen0);
	mMaterials["screen1"] = std::move(screen1);
	mMaterials["screen2"] = std::move(screen2);
	mMaterials["screen3"] = std::move(screen3);
	mMaterials["screen4"] = std::move(screen4);
	mMaterials["screen5"] = std::move(screen5);
	mMaterials["screen6"] = std::move(screen6);
	mMaterials["screen7"] = std::move(screen7);
	mMaterials["screen8"] = std::move(screen8);
	mMaterials["screen9"] = std::move(screen9);
}

void BlendApp::BuildRenderItems()
{
	auto wavesRitem = std::make_unique<RenderItem>();
	wavesRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&wavesRitem->TexTransform, XMMatrixScaling(5.0f, 5.0f, 1.0f));
	wavesRitem->ObjCBIndex = 0;
	wavesRitem->Mat = mMaterials["water"].get();
	wavesRitem->Geo = mGeometries["waterGeo"].get();
	wavesRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wavesRitem->IndexCount = wavesRitem->Geo->DrawArgs["grid"].IndexCount;
	wavesRitem->StartIndexLocation = wavesRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	wavesRitem->BaseVertexLocation = wavesRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

	mWavesRitem = wavesRitem.get();

	mRitemLayer[(int)RenderLayer::Transparent].push_back(wavesRitem.get());

	auto gridRitem = std::make_unique<RenderItem>();
	gridRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&gridRitem->TexTransform, XMMatrixScaling(5.0f, 5.0f, 1.0f));
	gridRitem->ObjCBIndex = 1;
	gridRitem->Mat = mMaterials["grass"].get();
	gridRitem->Geo = mGeometries["landGeo"].get();
	gridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
	gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::Opaque].push_back(gridRitem.get());

	auto boxRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&boxRitem->World, XMMatrixTranslation(3.0f, 2.0f, -9.0f));
	boxRitem->ObjCBIndex = 2;
	boxRitem->Mat = mMaterials["wirefence"].get();
	boxRitem->Geo = mGeometries["boxGeo"].get();
	boxRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
	boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::AlphaTested].push_back(boxRitem.get());

	auto cylinderRitem = std::make_unique<RenderItem>();
	cylinderRitem->World = MathHelper::Identity4x4();
	//XMStoreFloat4x4(&cylinderRitem->World, XMMatrixTranslation(3.0f, 2.0f, -9.0f));
	cylinderRitem->ObjCBIndex = 3;
	cylinderRitem->Mat = mMaterials["cylinder"].get();
	cylinderRitem->Geo = mGeometries["cylinderGeo"].get();
	cylinderRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	cylinderRitem->IndexCount = cylinderRitem->Geo->DrawArgs["cylinder"].IndexCount;
	cylinderRitem->StartIndexLocation = cylinderRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
	cylinderRitem->BaseVertexLocation = cylinderRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::TexAnim].push_back(cylinderRitem.get());

	auto screenRitem = std::make_unique<RenderItem>();
	screenRitem->World = MathHelper::Identity4x4();
	//XMStoreFloat4x4(&cylinderRitem->World, XMMatrixTranslation(3.0f, 2.0f, -9.0f));
	screenRitem->ObjCBIndex = 4;
	screenRitem->Mat = mMaterials["screen0"].get();
	screenRitem->Geo = mGeometries["screenGeo"].get();
	screenRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	screenRitem->IndexCount = screenRitem->Geo->DrawArgs["screen"].IndexCount;
	screenRitem->StartIndexLocation = screenRitem->Geo->DrawArgs["screen"].StartIndexLocation;
	screenRitem->BaseVertexLocation = screenRitem->Geo->DrawArgs["screen"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::Screen].push_back(screenRitem.get());

	mAllRitems.push_back(std::move(wavesRitem));
	mAllRitems.push_back(std::move(gridRitem));
	mAllRitems.push_back(std::move(boxRitem));
	mAllRitems.push_back(std::move(cylinderRitem));
	mAllRitems.push_back(std::move(screenRitem));
}

void BlendApp::BuildFrameResources()
{
	for (auto i : Range(0, gNumFrameResources))
	{
		auto frameRes = std::make_unique<FrameResource>(md3dDevice.Get(), 1, 
			(UINT)mAllRitems.size(), (UINT)mMaterials.size(), (UINT)mWaves->VertexCount());
		mFrameResources.emplace_back(std::move(frameRes));
	}
}

void BlendApp::BuildPSOs()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaqueDesc{ 0 };
	ZeroMemory(&opaqueDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaqueDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	//opaqueDesc.DepthStencilState.StencilEnable = true;
	opaqueDesc.DepthStencilState.DepthEnable = false;
	//opaqueDesc.DepthStencilState.StencilEnable = false;
	//opaqueDesc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_INCR;
	//opaqueDesc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	//opaqueDesc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	//opaqueDesc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;

	opaqueDesc.BlendState.RenderTarget[0].BlendEnable = true;
	opaqueDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	opaqueDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
	opaqueDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	opaqueDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	opaqueDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
	opaqueDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	opaqueDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	//opaqueDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaqueDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaqueDesc.SampleMask = UINT_MAX;
	opaqueDesc.NumRenderTargets = 1;
	opaqueDesc.DSVFormat = mDepthStencilFormat;
	opaqueDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	opaqueDesc.VS = {
		reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
		mShaders["standardVS"]->GetBufferSize() };
	opaqueDesc.PS = {
		reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
		mShaders["opaquePS"]->GetBufferSize() };
	opaqueDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaqueDesc.pRootSignature = mRootSignature.Get();
	opaqueDesc.RTVFormats[0] = mBackBufferFormat;
	opaqueDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	opaqueDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaqueDesc, IID_PPV_ARGS(&mPSOs["opaque"])));

	D3D12_RENDER_TARGET_BLEND_DESC  transparentBlendDesc{};
	transparentBlendDesc.BlendEnable = true;
	transparentBlendDesc.LogicOpEnable = false;
	transparentBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
	transparentBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	transparentBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
	transparentBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
	transparentBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
	transparentBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	transparentBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
	transparentBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	//transparentBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALPHA | D3D12_COLOR_WRITE_ENABLE_GREEN;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC transparentDesc = opaqueDesc;
	transparentDesc.BlendState.RenderTarget[0] = transparentBlendDesc;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&transparentDesc, IID_PPV_ARGS(&mPSOs["transparent"])));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC alphaTestDesc = opaqueDesc;
	alphaTestDesc.PS = { mShaders["alphaTestPS"]->GetBufferPointer(), mShaders["alphaTestPS"]->GetBufferSize() };
	alphaTestDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&alphaTestDesc, IID_PPV_ARGS(&mPSOs["alphaTest"])));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC screenDesc = opaqueDesc;
	screenDesc.VS = { mShaders["screenVS"]->GetBufferPointer(), mShaders["screenVS"]->GetBufferSize() };
	screenDesc.PS = { mShaders["screenPS"]->GetBufferPointer(), mShaders["screenPS"]->GetBufferSize() };

	//screenDesc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
	//screenDesc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
	//screenDesc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	//screenDesc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&screenDesc, IID_PPV_ARGS(&mPSOs["screen"])));
}

void BlendApp::OnResize()
{
	D3DApp::OnResize();

	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	XMStoreFloat4x4(&mProj, P);
}

void BlendApp::OnKeyboardInput(const GameTimer& gt)
{
	const float dt = gt.DeltaTime();

	if (GetAsyncKeyState(0x44) & 0x8000)
		mSunTheta -= 1.0f * dt;
	if (GetAsyncKeyState(0x41) & 0x8000)
		mSunTheta += 1.0f * dt;
	if (GetAsyncKeyState(0x57) & 0x8000)
		mSunPhi -= 1.0f * dt;
	if (GetAsyncKeyState(0x53) & 0x8000)
		mSunPhi += 1.0f * dt;

	mSunPhi = MathHelper::Clamp(mSunPhi, 0.1f, XM_PIDIV2);
}

void BlendApp::UpdateCamera(const GameTimer& gt)
{
	mEyePos.x = mRadius * sinf(mPhi) * cosf(mTheta);
	mEyePos.z = mRadius * sinf(mPhi) * sinf(mTheta);
	mEyePos.y = mRadius * cosf(mPhi);

	XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&mView, view);
}

void BlendApp::AnimateMaterials(const GameTimer& gt)
{
	auto& waterMat = mMaterials["water"];

	float tu = waterMat->MatTransform(3, 0);
	float tv = waterMat->MatTransform(3, 1);

	tu += 0.1f * gt.DeltaTime();
	if (tu >= 1.0f) tu -= 1.0f;
	tv += 0.02f * gt.DeltaTime();
	if (tv >= 1.0f) tv -= 1.0f;

	waterMat->MatTransform(3, 0) = tu;
	waterMat->MatTransform(3, 1) = tv;

	waterMat->NumFramesDirty = gNumFrameResources;
}

void BlendApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto& objCB = mCurrFrameResource->ObjectCB;
	for (auto& e : mAllRitems)
	{
		if (e->NumFramesDirty <= 0)
			continue;

		XMMATRIX world = XMLoadFloat4x4(&e->World);
		XMMATRIX tex = XMLoadFloat4x4(&e->TexTransform);

		ObjectConstants objConstants;
		XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
		XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(tex));

		objCB->CopyData(e->ObjCBIndex, objConstants);
		
		e->NumFramesDirty--;
	}
}

void BlendApp::UpdateMaterialCBs(const GameTimer& gt)
{
	auto& matCB = mCurrFrameResource->MaterialCB;
	for (auto& mat : mMaterials)
	{
		auto& m = mat.second;
		if (m->NumFramesDirty <= 0)
			continue;

		XMMATRIX mt = XMLoadFloat4x4(&m->MatTransform);
		MaterialConstants matConstants;
		matConstants.DiffuseAlbedo = m->DiffuseAlbedo;
		matConstants.FresnelR0 = m->FresnelR0;
		matConstants.Roughness = m->Roughness;

		XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(mt));
		matCB->CopyData(m->MatCBIndex, matConstants);

		m->NumFramesDirty--;
	}
}

void BlendApp::UpdateMainPassCB(const GameTimer& gt)
{
	auto& passCB = mCurrFrameResource->PassCB;

	XMMATRIX view = XMLoadFloat4x4(&mView);
	XMMATRIX proj = XMLoadFloat4x4(&mProj);
	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(nullptr, view);
	XMMATRIX invProj = XMMatrixInverse(nullptr, proj);
	XMMATRIX invViewProj = XMMatrixInverse(nullptr, viewProj);

	XMStoreFloat4x4(&mPassConstants.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mPassConstants.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mPassConstants.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mPassConstants.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mPassConstants.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mPassConstants.InvViewProj, XMMatrixTranspose(invViewProj));
	mPassConstants.DeltaTime = gt.DeltaTime();
	mPassConstants.TotalTime = gt.TotalTime();
	mPassConstants.EyePosW = mEyePos;
	mPassConstants.NearZ = 1.0f;
	mPassConstants.FarZ = 1000.0f;
	mPassConstants.RenderTargetSize = { (float)mClientWidth, (float)mClientHeight };
	mPassConstants.InvRenderTargetSize = { 1.0f / (float)mClientWidth, 1.0f / (float)mClientHeight };
	mPassConstants.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };
	mPassConstants.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
	mPassConstants.Lights[0].Strength = { 0.9f, 0.9f, 0.8f };
	mPassConstants.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	mPassConstants.Lights[1].Strength = { 0.3f, 0.3f, 0.3f };
	mPassConstants.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
	mPassConstants.Lights[2].Strength = { 0.15f, 0.15f, 0.15f };

	passCB->CopyData(0, mPassConstants);
}

void BlendApp::UpdateWaves(const GameTimer& gt)
{
	static float t_base = 0.0f;
	if ((mTimer.TotalTime() - t_base) >= 0.25f)
	{
		t_base += 0.25f;
		int i = MathHelper::Rand(4, mWaves->RowCount() - 5);
		int j = MathHelper::Rand(4, mWaves->ColumnCount() - 5);
		float r = MathHelper::RandF(0.2f, 0.5f);
		
		mWaves->Disturb(i, j, r);
	}
	mWaves->Update(gt.DeltaTime());

	// Update the wave vertex buffer with the new solution.
	auto currWavesVB = mCurrFrameResource->WavesVB.get();
	for (int i = 0; i < mWaves->VertexCount(); ++i)
	{
		Vertex v;

		v.Pos = mWaves->Position(i);
		v.Normal = mWaves->Normal(i);

		// Derive tex-coords from position by 
		// mapping [-w/2,w/2] --> [0,1]
		v.TexC.x = 0.5f + v.Pos.x / mWaves->Width();
		v.TexC.y = 0.5f - v.Pos.z / mWaves->Depth();

		currWavesVB->CopyData(i, v);
	}

	// Set the dynamic VB of the wave renderitem to the current frame VB.
	mWavesRitem->Geo->VertexBufferGPU = currWavesVB->Resource();
}

void BlendApp::Update(const GameTimer& gt)
{
	OnKeyboardInput(gt);
	UpdateCamera(gt);

	nFrameResourceIndex = (nFrameResourceIndex + 1) % gNumFrameResources;
	mCurrFrameResource = mFrameResources[nFrameResourceIndex].get();
	if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
	{
		HANDLE handle = ::CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, handle));
		WaitForSingleObject(handle, INFINITE);
		CloseHandle(handle);
	}

	AnimateMaterials(gt);
	UpdateObjectCBs(gt);
	UpdateMaterialCBs(gt);
	UpdateMainPassCB(gt);
	UpdateWaves(gt);
}

int gStencilRef = 0;

void BlendApp::DrawRenderItems(
	ID3D12GraphicsCommandList* cmdList, 
	const std::vector<RenderItem*>& ritems,
	const GameTimer& gt)
{
	auto objCBRes = mCurrFrameResource->ObjectCB->Resource();
	auto matCBRes = mCurrFrameResource->MaterialCB->Resource();

	UINT objByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

	static float second = 0.0f;
	second += gt.DeltaTime();
	if (second > 1.0f)
		second = 0.0f;
	int animTexIndex = int(second * 60.0f);

	for (auto& ri : ritems)
	{
		D3D12_GPU_VIRTUAL_ADDRESS addrObj = objCBRes->GetGPUVirtualAddress();
		addrObj += ri->ObjCBIndex * objByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS addrMat = matCBRes->GetGPUVirtualAddress();
		if (ri->Geo->Name == "screenGeo")
			addrMat += (ri->Mat->MatCBIndex + gStencilRef) * matByteSize;
		else
			addrMat += ri->Mat->MatCBIndex * matByteSize;

		int texOffsetIdx = ri->Mat->DiffuseSrvHeapIndex;
		if (ri->Geo->Name == "cylinderGeo")
			texOffsetIdx += animTexIndex;
		CD3DX12_GPU_DESCRIPTOR_HANDLE hDesc(mDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		hDesc.Offset(texOffsetIdx, mCbvSrvDescriptorSize);

		mCommandList->SetGraphicsRootDescriptorTable(0, hDesc);
		mCommandList->SetGraphicsRootConstantBufferView(1, addrObj);
		mCommandList->SetGraphicsRootConstantBufferView(3, addrMat);
		
		mCommandList->IASetVertexBuffers(0, 1, &RvToLv(ri->Geo->VertexBufferView()));
		mCommandList->IASetIndexBuffer(&RvToLv(ri->Geo->IndexBufferView()));
		mCommandList->IASetPrimitiveTopology(ri->PrimitiveType);

		mCommandList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

void BlendApp::Draw(const GameTimer& gt)
{
	auto passRes = mCurrFrameResource->PassCB->Resource();
	
	auto cmdsLists = mCurrFrameResource->CmdListAlloc;
	ThrowIfFailed(cmdsLists->Reset());
	ThrowIfFailed(mCommandList->Reset(cmdsLists.Get(), mPSOs["opaque"].Get()));

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	mCommandList->ResourceBarrier(1, &RvToLv(CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET)));
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), (float*)&mPassConstants.FogColor, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), 
		D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
	mCommandList->OMSetRenderTargets(1, &RvToLv(CurrentBackBufferView()), true, &RvToLv(DepthStencilView()));
	ID3D12DescriptorHeap* ppHeaps[] = { mDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
	

	D3D12_GPU_VIRTUAL_ADDRESS addrCBV = passRes->GetGPUVirtualAddress();
	mCommandList->SetGraphicsRootConstantBufferView(2, addrCBV);

	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque], gt);

	mCommandList->SetPipelineState(mPSOs["alphaTest"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::AlphaTested], gt);

	mCommandList->SetPipelineState(mPSOs["transparent"].Get());
	//DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::TexAnim], gt);
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Transparent], gt);

	//gStencilRef = 0;
	//mCommandList->OMSetStencilRef(gStencilRef);
	//mCommandList->SetPipelineState(mPSOs["screen"].Get());
	//DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Screen], gt);

	//gStencilRef = 1;
	//mCommandList->OMSetStencilRef(gStencilRef);
	//DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Screen], gt);

	//gStencilRef = 2;
	//mCommandList->OMSetStencilRef(gStencilRef);
	//DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Screen], gt);

	//gStencilRef = 3;
	//mCommandList->OMSetStencilRef(gStencilRef);
	//DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Screen], gt);

	//gStencilRef = 4;
	//mCommandList->OMSetStencilRef(gStencilRef);
	//DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Screen], gt);

	//gStencilRef = 5;
	//mCommandList->OMSetStencilRef(gStencilRef);
	//DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Screen], gt);

	//gStencilRef = 6;
	//mCommandList->OMSetStencilRef(gStencilRef);
	//DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Screen], gt);

	//gStencilRef = 7;
	//mCommandList->OMSetStencilRef(gStencilRef);
	//DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Screen], gt);

	//gStencilRef = 8;
	//mCommandList->OMSetStencilRef(gStencilRef);
	//DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Screen], gt);

	//gStencilRef = 9;
	//mCommandList->OMSetStencilRef(gStencilRef);
	//DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Screen], gt);

	mCommandList->ResourceBarrier(1, &RvToLv(CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT)));
	ThrowIfFailed(mCommandList->Close());

	ID3D12CommandList* ppCommandLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;
	
	mCurrFrameResource->Fence = ++mCurrentFence;
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void BlendApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWnd);
}
void BlendApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}
void BlendApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

		mTheta += dx;
		mPhi += dy;

		mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
	}
	else if ((btnState & MK_RBUTTON) != 0)
	{
		float dx = 0.2f * static_cast<float>(x - mLastMousePos.x);
		float dy = 0.2f * static_cast<float>(y - mLastMousePos.y);

		mRadius += dx - dy;
		mRadius = MathHelper::Clamp(mRadius, 5.0f, 150.f);
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance, PSTR cmdLine, int showCmd)
{
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	try
	{
		BlendApp theApp(hInstance);
		if (!theApp.Initialize())
			return 0;

		return theApp.Run();
	}
	catch (DxException e)
	{
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}

	return 1;
}