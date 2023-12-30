#include "source.h"
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
	AlphaTested,
	Transparent,
	AlphaTestedTreeSprites,
	Circle,
	Sphere,
	Count
};

enum class PSOType : int
{
	Init = 0,
	Opaque,
	AlphaTested,
	Transparent,
	AlphaTestedTreeSprites,
	CircleGS,
	SphereGS,
	VertexNormalGS,
	FaceNormalGS,
	Count
};

class TreeBillboardsApp : public D3DApp
{
public:
	TreeBillboardsApp(HINSTANCE hInstance);
	TreeBillboardsApp(const TreeBillboardsApp& rhs) = delete;
	TreeBillboardsApp& operator=(const TreeBillboardsApp& rhs) = delete;
	~TreeBillboardsApp();

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
	template<typename T>
	void MakeGeometry(const std::string&& geoName, const std::string&& smName,
		std::vector<T>& vertices, const std::vector<std::uint16_t>& indices);
	void BuildLandGeometry();
	void BuildWavesGeometry();
	void BuildBoxGeometry();
	void BuildTreeSpritesGeometry();
	D3D12_SHADER_BYTECODE GetShaderBytecode(const std::string&& shaderName);
	void MakeOpaqueDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc);
	void MakeAlphaTestedDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc);
	void MakeTransparentDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc);
	void MakeAlphaTestedTreeSpritesDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc);
	void MakeCircleGS(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc);
	void MakeSphereGS(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc);
	void MakeVertexNormalGS(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc);
	void MakeFaceNormalGS(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc);
	void MakePSOPipelineState(PSOType destPso, PSOType srcPso = PSOType::Init);
	void BuildPSOs();
	void BuildFrameResources();
	void BuildCircleGeometry();
	void BuildSphereGeometry();
	void BuildMaterials();
	void BuildRenderItems();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, 
		const std::vector<RenderItem*>& ritems);
	
	float GetHillsHeight(float x, float z) const;
	XMFLOAT3 GetHillsNormal(float x, float z) const;
	
private:
	UINT mCbvSrvDescriptorSize = 0;
	std::unique_ptr<Waves> mWaves;

private:
	std::vector<std::unique_ptr<Texture>> mTextures;
	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap;
	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
	std::vector<D3D12_INPUT_ELEMENT_DESC> 
		mStdInputLayout, mTreeSpriteLayout, mCircleLayout, mSphereLayout, mNormalLayout;
	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::unordered_map<RenderLayer, std::vector<RenderItem*>> mRitemLayer;
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;
	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	std::unordered_map<PSOType, 
		std::pair<D3D12_GRAPHICS_PIPELINE_STATE_DESC, ComPtr<ID3D12PipelineState>>> mPSOs;
	int mCurFrameResIdx = 0;
	FrameResource* mCurFrameRes = nullptr;
	RenderItem* mWavesRitem = nullptr;

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

TreeBillboardsApp::TreeBillboardsApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
	//m4xMsaaState = true;
}

TreeBillboardsApp::~TreeBillboardsApp()
{
	if (md3dDevice != nullptr)
		FlushCommandQueue();
}

bool TreeBillboardsApp::Initialize()
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
	BuildTreeSpritesGeometry();
	BuildCircleGeometry();
	BuildSphereGeometry();
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

void TreeBillboardsApp::LoadTextures()
{
	auto LoadTexture = [&](std::wstring& filename)
	{
		auto tex = std::make_unique<Texture>();
		ThrowIfFailed(CreateDDSTextureFromFile12(md3dDevice.Get(), mCommandList.Get(),
			filename.c_str(), tex->Resource, tex->UploadHeap));
		mTextures.emplace_back(std::move(tex));
	};
	
	std::vector<std::wstring> fileList
	{
		L"../Textures/grass.dds",
		L"../Textures/water1.dds",
		L"../Textures/WireFence.dds",
		L"../Textures/treeArray2.dds",
	};
	for_each(fileList.begin(), fileList.end(), [&](std::wstring& filename) { LoadTexture(filename); });
}

void TreeBillboardsApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE srvTable{};
	srvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

	CD3DX12_ROOT_PARAMETER rootParameter[4];
	rootParameter[0].InitAsDescriptorTable(1, &srvTable, D3D12_SHADER_VISIBILITY_PIXEL);
	rootParameter[1].InitAsConstantBufferView(0);
	rootParameter[2].InitAsConstantBufferView(1);
	rootParameter[3].InitAsConstantBufferView(2);

	auto samplers = d3dUtil::GetStaticSamplers();
	CD3DX12_ROOT_SIGNATURE_DESC rootDesc{};
	rootDesc.Init(_countof(rootParameter), rootParameter, 
		static_cast<UINT>(samplers.size()), samplers.data(), 
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, 
		&serializedRootSig, &errorBlob);
	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(&mRootSignature)));
}

void TreeBillboardsApp::BuildDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
	heapDesc.NodeMask = 0;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	heapDesc.NumDescriptors = 6;// static_cast<UINT>(mTextures.size());
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	auto CreateShaderResourceView = [&, count{ 0 }](std::unique_ptr<Texture>& tex) mutable
	{
		auto texRes = tex->Resource;
		auto texDesc = texRes->GetDesc();

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Texture2D.MipLevels = texDesc.MipLevels;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
		srvDesc.Format = texDesc.Format;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;

		if (texDesc.DepthOrArraySize > 1) //텍스쳐 배열이라면
		{
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
			srvDesc.Texture2DArray.MipLevels = texDesc.MipLevels;
			srvDesc.Texture2DArray.MostDetailedMip = 0;
			srvDesc.Texture2DArray.ResourceMinLODClamp = 0.0f;
			srvDesc.Texture2DArray.FirstArraySlice = 0;
			srvDesc.Texture2DArray.ArraySize = texDesc.DepthOrArraySize;
		}

		CD3DX12_CPU_DESCRIPTOR_HANDLE hDesc{ mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart() };
		hDesc.Offset(count++, mCbvSrvDescriptorSize);

		md3dDevice->CreateShaderResourceView(texRes.Get(), &srvDesc, hDesc);
	};

	for_each(mTextures.begin(), mTextures.end(), CreateShaderResourceView);
}

void TreeBillboardsApp::BuildShadersAndInputLayout()
{
	const D3D_SHADER_MACRO mFog[]
	{
		"FOG", "1", NULL, NULL
	};

	const D3D_SHADER_MACRO mAlphaTest[]
	{
		"ALPHA_TEST", "1", "FOG", "1", NULL, NULL
	};

	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders/Default.hlsl", nullptr, "VS", "vs_5_0");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders/Default.hlsl", mFog, "PS", "ps_5_0");
	mShaders["alphaTestedPS"] = d3dUtil::CompileShader(L"Shaders/Default.hlsl", mAlphaTest, "PS", "ps_5_0");

	mShaders["treeSpriteVS"] = d3dUtil::CompileShader(L"Shaders/TreeSprite.hlsl", nullptr, "VS", "vs_5_0");
	mShaders["treeSpriteGS"] = d3dUtil::CompileShader(L"Shaders/TreeSprite.hlsl", nullptr, "GS", "gs_5_0");
	mShaders["treeSpritePS"] = d3dUtil::CompileShader(L"Shaders/TreeSprite.hlsl", mAlphaTest, "PS", "ps_5_0");

	mShaders["circleVS"] = d3dUtil::CompileShader(L"HLSL/circleVS.hlsl", nullptr, "main", "vs_5_0");
	mShaders["circleGS"] = d3dUtil::CompileShader(L"HLSL/circleGS.hlsl", nullptr, "main", "gs_5_0");
	mShaders["circlePS"] = d3dUtil::CompileShader(L"HLSL/circlePS.hlsl", mAlphaTest, "main", "ps_5_0");
	
	mShaders["sphereVS"] = d3dUtil::CompileShader(L"HLSL/circleVS.hlsl", mAlphaTest, "SphereVS", "vs_5_0");
	mShaders["sphereGS"] = d3dUtil::CompileShader(L"HLSL/circleGS.hlsl", mAlphaTest, "SphereGS", "gs_5_0");
	mShaders["sphereExplosionGS"] = d3dUtil::CompileShader(L"HLSL/circleGS.hlsl", mAlphaTest, "SphereExplosionGS", "gs_5_0");
	mShaders["spherePS"] = d3dUtil::CompileShader(L"HLSL/circlePS.hlsl", mAlphaTest, "main", "ps_5_0");

	mShaders["normalVS"] = d3dUtil::CompileShader(L"HLSL/circleVS.hlsl", nullptr, "NormalVS", "vs_5_0");
	mShaders["vertexNormalGS"] = d3dUtil::CompileShader(L"HLSL/circleGS.hlsl", nullptr, "VertexNormalGS", "gs_5_0");
	mShaders["vertexNormalPS"] = d3dUtil::CompileShader(L"HLSL/circlePS.hlsl", nullptr, "VertexNormalPS", "ps_5_0");

	mShaders["faceNormalGS"] = d3dUtil::CompileShader(L"HLSL/circleGS.hlsl", nullptr, "FaceNormalGS", "gs_5_0");
	mShaders["faceNormalPS"] = d3dUtil::CompileShader(L"HLSL/circlePS.hlsl", nullptr, "FaceNormalPS", "ps_5_0");
	
	mStdInputLayout = 
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	mTreeSpriteLayout =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{"SIZE", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	mCircleLayout =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{"NORMAL", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{"HEIGHT", 0, DXGI_FORMAT_R32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	mSphereLayout =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{"RADIUS", 0, DXGI_FORMAT_R32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	mNormalLayout =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}

float TreeBillboardsApp::GetHillsHeight(float x, float z) const
{
	return 0.3f * (z * sinf(0.1f * x) + x * cosf(0.1f * z));
}

XMFLOAT3 TreeBillboardsApp::GetHillsNormal(float x, float z) const
{
	XMFLOAT3 n(
		-0.03f * z * cosf(0.1f * x) - 0.3f * cosf(0.1f * z),
		1.0f,
		-0.3f * sinf(0.1f * x) + 0.03f * x * sinf(0.1f * z));
	
	XMVECTOR uintNormal = XMVector3Normalize(XMLoadFloat3(&n));
	XMStoreFloat3(&n, uintNormal);

	return n;
}

template<typename T>
void TreeBillboardsApp::MakeGeometry(const std::string&& geoName, const std::string&& smName,
	std::vector<T>& vertices, const std::vector<std::uint16_t>& indices)
{
	auto mg = std::make_unique<MeshGeometry>();
	if (vertices.size())
	{
		UINT totalVCnt = static_cast<UINT>(vertices.size());
		UINT vtxByteSize = totalVCnt * sizeof(T);
		mg->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
			vertices.data(), vtxByteSize, mg->VertexBufferUploader);
		mg->VertexByteStride = sizeof(T);
		mg->VertexBufferByteSize = vtxByteSize;
	}
	else
	{
		mg->VertexBufferGPU = nullptr;
		mg->VertexBufferUploader = nullptr;
		mg->VertexByteStride = 0;
		mg->VertexBufferByteSize = 0;
	}
	
	UINT totalICnt = static_cast<UINT>(indices.size());
	UINT idxByteSize = totalICnt * sizeof(std::int16_t);

	mg->Name = geoName;
	mg->DrawArgs[smName] = { totalICnt, 0, 0, {} };
	mg->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
		indices.data(), idxByteSize, mg->IndexBufferUploader);
	mg->IndexFormat = DXGI_FORMAT_R16_UINT;
	mg->IndexBufferByteSize = idxByteSize;

	mGeometries[mg->Name] = std::move(mg);
}

void TreeBillboardsApp::BuildLandGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(160.0f, 160.0f, 50, 50);
	
	std::vector<Vertex> vertices(grid.Vertices.size());
	for (size_t i = 0; i < grid.Vertices.size(); ++i)
	{
		auto& p = grid.Vertices[i].Position;
		vertices[i].Pos = p;
		vertices[i].Pos.y = GetHillsHeight(p.x, p.z);
		vertices[i].Normal = GetHillsNormal(p.x, p.z);
		vertices[i].TexC = grid.Vertices[i].TexC;
	}
	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), grid.GetIndices16().begin(), grid.GetIndices16().end());

	MakeGeometry("landGeo", "grid", vertices, indices);
}

void TreeBillboardsApp::BuildWavesGeometry()
{
	std::vector<std::uint16_t> indices(3 * mWaves->TriangleCount()); // 3 indices per face
	assert(mWaves->VertexCount() < 0x0000ffff);

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

	std::vector<Vertex> zero;
	MakeGeometry("waterGeo", "grid", zero, indices);
}

void TreeBillboardsApp::BuildBoxGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(8.0f, 8.0f, 8.0f, 3);

	std::vector<Vertex> vertices(box.Vertices.size());
	for (size_t i = 0; i < box.Vertices.size(); ++i)
	{
		auto& p = box.Vertices[i].Position;
		vertices[i].Pos = p;
		vertices[i].Normal = box.Vertices[i].Normal;
		vertices[i].TexC = box.Vertices[i].TexC;
	}
	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), box.GetIndices16().begin(), box.GetIndices16().end());

	MakeGeometry("boxGeo", "box", vertices, indices);
}

void TreeBillboardsApp::BuildTreeSpritesGeometry()
{
	struct TreeSpriteVertex
	{
		XMFLOAT3 Pos{};
		XMFLOAT2 Size{};
	};
	
	static const int treeCount = 16;
	std::vector<TreeSpriteVertex> vertices(treeCount);
	for (auto i : Range(0, treeCount))
	{
		float x = MathHelper::RandF(-45.0f, 45.0f);
		float z = MathHelper::RandF(-45.0f, 45.0f);
		float y = GetHillsHeight(x, z) + 8.0f;

		vertices[i].Pos = XMFLOAT3(x, y, z);
		vertices[i].Size = XMFLOAT2(20.0f, 20.0f);
	}

	std::vector<std::uint16_t> indices(treeCount);
	std::generate_n(indices.begin(), treeCount, [cnt{ 0 }]() mutable { return cnt++; });

	MakeGeometry("treeSpritesGeo", "points", vertices, indices);
}

void TreeBillboardsApp::BuildCircleGeometry()
{
	struct CircleVertex
	{
		XMFLOAT3 Pos{ 0.0f, 0.0f, 0.0f };
		XMFLOAT2 Normal{ 0.0f, 0.0f };
		float Height{ 0.0f };
	};

	auto DegreeToRadian = [](float degree) { return degree * MathHelper::Pi / 180.0f; };

	static const int circleCount = 120;
	std::vector<CircleVertex> cVertex;
	for (auto i : Range(0, circleCount))
	{
		float curDegree = 360.0f / (float)circleCount;
		curDegree *= (float)i;

		CircleVertex vertex;
		vertex.Pos.x = cos(DegreeToRadian(curDegree)) * 5.0f;
		vertex.Pos.z = sin(DegreeToRadian(curDegree)) * 5.0f;
		vertex.Normal.x = vertex.Pos.x;
		vertex.Normal.y = vertex.Pos.z;
		vertex.Height = 8.0f;
		cVertex.emplace_back(vertex);
	}

	std::vector<std::uint16_t> indices;
	for (auto i : Range(0, circleCount))
	{
		indices.emplace_back(i);
		int secIdx = 0;
		if (circleCount > i + 1) secIdx = i + 1;
		indices.emplace_back(secIdx);
	}

	MakeGeometry("circleGeo", "line", cVertex, indices);
}

void TreeBillboardsApp::BuildSphereGeometry()
{
	struct SphereVertex
	{
		XMFLOAT3 Pos{ 0.0f, 0.0f, 0.0f };
		XMFLOAT3 Normal{ 0.0f, 0.0f, 0.0f };
		float Radius{ 0.0f };
	};

	const float radius = 5.0f;
	const float X = 0.525731f;
	const float Z = 0.850651f;
	

	static const int totalVtxCnt = 12;
	XMFLOAT3 pos[totalVtxCnt] =
	{
		XMFLOAT3(-X, 0.0f, Z), XMFLOAT3(X, 0.0f, Z),
		XMFLOAT3(-X, 0.0f, -Z), XMFLOAT3(X, 0.0f, -Z),
		XMFLOAT3(0.0f, Z, X), XMFLOAT3(0.0f, Z, -X),
		XMFLOAT3(0.0f, -Z, X), XMFLOAT3(0.0f, -Z, -X),
		XMFLOAT3(Z, X, 0.0f), XMFLOAT3(-Z, X, 0.0f),
		XMFLOAT3(Z, -X, 0.0f), XMFLOAT3(-Z, -X, 0.0f),
	};

	std::vector<SphereVertex> sVertex(totalVtxCnt);
	for (auto i : Range(0, totalVtxCnt))
	{
		sVertex[i].Pos = pos[i];
		sVertex[i].Normal = pos[i];
		sVertex[i].Radius = radius;
	}

	std::vector<std::uint16_t> indices
	{
		1,4,0, 4,9,0, 4,5,9, 8,5,4, 1,8,4,
		1,10,8, 10,3,8, 8,3,5, 3,2,5, 3,7,2,
		3,10,7, 10,6,7, 6,11,7, 6,0,11, 6,1,0,
		10,1,6, 11,0,9, 2,11,9, 5,2,9, 11,2,7
	};

	MakeGeometry("sphereGeo", "triangle", sVertex, indices);
}

void TreeBillboardsApp::BuildMaterials()
{
	auto CreateMaterial = [&](std::string&& matName, int matCBIdx, int srvHeapIdx,
		XMFLOAT4 diffuse, XMFLOAT3 fresnelR0, float roughness)
		{
			auto mat = std::make_unique<Material>();
			mat->Name = matName;
			mat->MatCBIndex = matCBIdx;
			mat->DiffuseSrvHeapIndex = srvHeapIdx;
			mat->DiffuseAlbedo = diffuse;
			mat->FresnelR0 = fresnelR0;
			mat->Roughness = roughness;
			mMaterials[matName] = std::move(mat);
		};

	CreateMaterial("grass", 0, 0, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.01f, 0.01f, 0.01f }, 0.125f);
	CreateMaterial("water", 1, 1, { 1.0f, 1.0f, 1.0f, 0.5f }, { 0.1f, 0.1f, 0.1f }, 0.0f);
	CreateMaterial("wirefence", 2, 2, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.02f, 0.02f, 0.02f }, 0.25f);
	CreateMaterial("treeSprites", 3, 3, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.01f, 0.01f, 0.01f }, 0.125f);
	CreateMaterial("circle", 4, 0, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.01f, 0.01f, 0.01f }, 0.125f);
	CreateMaterial("sphere", 5, 0, { 0.5f, 1.0f, 0.5f, 1.0f }, { 0.01f, 0.01f, 0.01f }, 0.125f);
}

void TreeBillboardsApp::BuildRenderItems()
{
	struct RenderDesc
	{
		std::string geoName;
		std::string smName;
		std::string matName;
		XMMATRIX world;
		XMMATRIX texTransform;
		RenderLayer renderLayer;
		D3D_PRIMITIVE_TOPOLOGY primTopology;
	};

	std::vector<RenderDesc> renderDescList
	{
		RenderDesc{ "waterGeo", "grid", "water", XMMatrixIdentity(), XMMatrixScaling(5.0f, 5.0f, 1.0f), 
			RenderLayer::Transparent, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST },
		RenderDesc{ "landGeo", "grid", "grass", XMMatrixIdentity(), XMMatrixScaling(5.0f, 5.0f, 1.0f), 
			RenderLayer::Opaque, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST },
		RenderDesc{ "boxGeo", "box", "wirefence", XMMatrixTranslation(3.0f, 2.0f, -9.0f), XMMatrixIdentity(), 
			RenderLayer::AlphaTested, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST },
		RenderDesc{ "treeSpritesGeo", "points", "treeSprites", XMMatrixIdentity(), XMMatrixIdentity(), 
			RenderLayer::AlphaTestedTreeSprites, D3D_PRIMITIVE_TOPOLOGY_POINTLIST },
		RenderDesc{ "circleGeo", "line", "circle", XMMatrixIdentity(), XMMatrixIdentity(),
			RenderLayer::Circle, D3D_PRIMITIVE_TOPOLOGY_LINELIST },
		RenderDesc{ "sphereGeo", "triangle", "sphere", XMMatrixTranslation(0.0f, 0.0f, 0.0f), XMMatrixIdentity(),
			RenderLayer::Sphere, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST },
	};

	for_each(renderDescList.begin(), renderDescList.end(), [&, cnt{ 0u }](auto& rDesc) mutable
		{
			auto& curGeo = mGeometries[rDesc.geoName];
			auto ri = std::make_unique<RenderItem>();
			
			ri->Geo = curGeo.get();
			ri->Mat = mMaterials[rDesc.matName].get();
			ri->BaseVertexLocation = curGeo->DrawArgs[rDesc.smName].BaseVertexLocation;
			ri->StartIndexLocation = curGeo->DrawArgs[rDesc.smName].StartIndexLocation;
			ri->IndexCount = curGeo->DrawArgs[rDesc.smName].IndexCount;
			ri->NumFramesDirty = gNumFrameResources;
			ri->ObjCBIndex = cnt++;
			ri->PrimitiveType = rDesc.primTopology;
			XMStoreFloat4x4(&ri->World, rDesc.world);
			XMStoreFloat4x4(&ri->TexTransform, rDesc.texTransform);

			mRitemLayer[rDesc.renderLayer].emplace_back(ri.get());
			mAllRitems.emplace_back(std::move(ri));
		}
	);

	auto iter = find_if(mAllRitems.begin(), mAllRitems.end(), [](auto& ri) { return ri->Geo->Name == "waterGeo"; });
	if (iter != mAllRitems.end())
		mWavesRitem = iter->get();
}

void TreeBillboardsApp::BuildFrameResources()
{
	for (auto i : Range(0, gNumFrameResources))
	{
		auto frameRes = std::make_unique<FrameResource>(md3dDevice.Get(), 1, (UINT)mAllRitems.size(),
			(UINT)mMaterials.size(), (UINT)mWaves->VertexCount());
		mFrameResources.emplace_back(std::move(frameRes));
	}
}

D3D12_SHADER_BYTECODE TreeBillboardsApp::GetShaderBytecode(const std::string&& shaderName)
{
	return { mShaders[shaderName]->GetBufferPointer(), mShaders[shaderName]->GetBufferSize() };
}

void TreeBillboardsApp::MakeOpaqueDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc)
{
	inoutDesc->VS = GetShaderBytecode("standardVS");
	inoutDesc->PS = GetShaderBytecode("opaquePS");
	inoutDesc->RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	inoutDesc->BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	inoutDesc->DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	inoutDesc->SampleMask = UINT_MAX;
	inoutDesc->SampleDesc.Count = m4xMsaaState ? 4 : 1;
	inoutDesc->SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	inoutDesc->DSVFormat = mDepthStencilFormat;
	inoutDesc->RTVFormats[0] = mBackBufferFormat;
	inoutDesc->pRootSignature = mRootSignature.Get();
	inoutDesc->InputLayout = { mStdInputLayout.data(), (UINT)mStdInputLayout.size() };
	inoutDesc->NumRenderTargets = 1;
	inoutDesc->PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
}

void TreeBillboardsApp::MakeAlphaTestedDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc)
{
	inoutDesc->PS = GetShaderBytecode("alphaTestedPS");
	inoutDesc->RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
}

void TreeBillboardsApp::MakeTransparentDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc)
{
	auto& renderTarget = inoutDesc->BlendState.RenderTarget[0];
	renderTarget.BlendEnable = true;
	renderTarget.SrcBlend = D3D12_BLEND_SRC_ALPHA;
	renderTarget.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	renderTarget.BlendOp = D3D12_BLEND_OP_ADD;
	renderTarget.SrcBlendAlpha = D3D12_BLEND_ONE;
	renderTarget.DestBlendAlpha = D3D12_BLEND_ZERO;
	renderTarget.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	renderTarget.LogicOpEnable = false;
	renderTarget.LogicOp = D3D12_LOGIC_OP_NOOP;
}

void TreeBillboardsApp::MakeAlphaTestedTreeSpritesDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc)
{
	inoutDesc->VS = GetShaderBytecode("treeSpriteVS");
	inoutDesc->GS = GetShaderBytecode("treeSpriteGS");
	inoutDesc->PS = GetShaderBytecode("treeSpritePS");
	inoutDesc->PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
	inoutDesc->InputLayout = { mTreeSpriteLayout.data(), (UINT)mTreeSpriteLayout.size() };
	inoutDesc->RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
}

void TreeBillboardsApp::MakeCircleGS(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc)
{
	inoutDesc->VS = GetShaderBytecode("circleVS");
	inoutDesc->GS = GetShaderBytecode("circleGS");
	inoutDesc->PS = GetShaderBytecode("circlePS");
	inoutDesc->PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
	inoutDesc->InputLayout = { mCircleLayout.data(), (UINT)mCircleLayout.size() };
	inoutDesc->RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
}

void TreeBillboardsApp::MakeSphereGS(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc)
{
	inoutDesc->VS = GetShaderBytecode("sphereVS");
	inoutDesc->GS = GetShaderBytecode("sphereExplosionGS");
	inoutDesc->PS = GetShaderBytecode("spherePS");
	inoutDesc->PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	inoutDesc->InputLayout = { mSphereLayout.data(), (UINT)mSphereLayout.size() };
	inoutDesc->RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
}

void TreeBillboardsApp::MakeVertexNormalGS(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc)
{
	inoutDesc->VS = GetShaderBytecode("normalVS");
	inoutDesc->GS = GetShaderBytecode("vertexNormalGS");
	inoutDesc->PS = GetShaderBytecode("vertexNormalPS");
	inoutDesc->PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
	inoutDesc->InputLayout = { mNormalLayout.data(), (UINT)mNormalLayout.size() };
	inoutDesc->RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
}

void TreeBillboardsApp::MakeFaceNormalGS(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc)
{
	inoutDesc->VS = GetShaderBytecode("normalVS");
	inoutDesc->GS = GetShaderBytecode("faceNormalGS");
	inoutDesc->PS = GetShaderBytecode("faceNormalPS");
	inoutDesc->PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	inoutDesc->InputLayout = { mNormalLayout.data(), (UINT)mNormalLayout.size() };
	inoutDesc->RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
}

void TreeBillboardsApp::MakePSOPipelineState(PSOType destPso, PSOType srcPso)
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};

	if (srcPso != PSOType::Init)
		psoDesc = mPSOs[srcPso].first;
	
	switch (destPso)
	{
	case PSOType::Opaque:						MakeOpaqueDesc(&psoDesc);				break;
	case PSOType::AlphaTested:				MakeAlphaTestedDesc(&psoDesc);		break;
	case PSOType::Transparent:				MakeTransparentDesc(&psoDesc);		break;
	case PSOType::AlphaTestedTreeSprites: MakeAlphaTestedTreeSpritesDesc(&psoDesc);		break;
	case PSOType::CircleGS:						MakeCircleGS(&psoDesc);						break;
	case PSOType::SphereGS:					MakeSphereGS(&psoDesc);					break;
	case PSOType::VertexNormalGS:		MakeVertexNormalGS(&psoDesc);		break;
	case PSOType::FaceNormalGS:			MakeFaceNormalGS(&psoDesc);			break;
	default: assert(!"wrong type");
	}

	mPSOs[destPso].first = psoDesc;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&psoDesc, 
		IID_PPV_ARGS(&mPSOs[destPso].second)));
}

void TreeBillboardsApp::BuildPSOs()
{
	MakePSOPipelineState(PSOType::Opaque);
	MakePSOPipelineState(PSOType::AlphaTested, PSOType::Opaque);
	MakePSOPipelineState(PSOType::Transparent, PSOType::Opaque);
	MakePSOPipelineState(PSOType::AlphaTestedTreeSprites, PSOType::AlphaTested);
	MakePSOPipelineState(PSOType::CircleGS, PSOType::Opaque);
	MakePSOPipelineState(PSOType::SphereGS, PSOType::Opaque);
	MakePSOPipelineState(PSOType::VertexNormalGS, PSOType::Opaque);
	MakePSOPipelineState(PSOType::FaceNormalGS, PSOType::Opaque);
}

void TreeBillboardsApp::OnResize()
{
	D3DApp::OnResize();

	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	XMStoreFloat4x4(&mProj, P);
}

void TreeBillboardsApp::OnKeyboardInput(const GameTimer& gt)
{
}

void TreeBillboardsApp::UpdateCamera(const GameTimer& gt)
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

void TreeBillboardsApp::UpdateWaves(const GameTimer& gt)
{
	// Every quarter second, generate a random wave.
	static float t_base = 0.0f;
	if ((mTimer.TotalTime() - t_base) >= 0.25f)
	{
		t_base += 0.25f;

		int i = MathHelper::Rand(4, mWaves->RowCount() - 5);
		int j = MathHelper::Rand(4, mWaves->ColumnCount() - 5);

		float r = MathHelper::RandF(0.2f, 0.5f);

		mWaves->Disturb(i, j, r);
	}

	// Update the wave simulation.
	mWaves->Update(gt.DeltaTime());

	// Update the wave vertex buffer with the new solution.
	auto currWavesVB = mCurFrameRes->WavesVB.get();
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
	mWavesRitem->Geo->VertexBufferByteSize = (UINT)mWaves->VertexCount() * sizeof(Vertex);
	mWavesRitem->Geo->VertexByteStride = sizeof(Vertex);
}

void TreeBillboardsApp::AnimateMaterials(const GameTimer& gt)
{
	// Scroll the water material texture coordinates.
	auto waterMat = mMaterials["water"].get();

	float& tu = waterMat->MatTransform(3, 0);
	float& tv = waterMat->MatTransform(3, 1);

	tu += 0.1f * gt.DeltaTime();
	tv += 0.02f * gt.DeltaTime();

	if (tu >= 1.0f)
		tu -= 1.0f;

	if (tv >= 1.0f)
		tv -= 1.0f;

	waterMat->MatTransform(3, 0) = tu;
	waterMat->MatTransform(3, 1) = tv;

	// Material has changed, so need to update cbuffer.
	waterMat->NumFramesDirty = gNumFrameResources;
}

void TreeBillboardsApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto& curObjCB = mCurFrameRes->ObjectCB;
	for (auto& obj : mAllRitems)
	{
		if (obj->NumFramesDirty <= 0)
			continue;

		XMMATRIX world = XMLoadFloat4x4(&obj->World);
		XMMATRIX texTransform = XMLoadFloat4x4(&obj->TexTransform);
		
		ObjectConstants objConstant;
		XMStoreFloat4x4(&objConstant.World, XMMatrixTranspose(world));
		XMStoreFloat4x4(&objConstant.TexTransform, XMMatrixTranspose(texTransform));

		curObjCB->CopyData(obj->ObjCBIndex, objConstant);

		obj->NumFramesDirty--;
	}
}

void TreeBillboardsApp::UpdateMainPassCB(const GameTimer& gt)
{
	auto& curPassCB = mCurFrameRes->PassCB;

	XMMATRIX view = XMLoadFloat4x4(&mView);
	XMMATRIX invView = XMMatrixInverse(nullptr, view);
	XMMATRIX proj = XMLoadFloat4x4(&mProj);
	XMMATRIX invProj = XMMatrixInverse(nullptr, proj);
	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invViewProj = XMMatrixInverse(nullptr, viewProj);

	PassConstants passConstants{};
	XMStoreFloat4x4(&passConstants.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&passConstants.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&passConstants.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&passConstants.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&passConstants.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&passConstants.InvViewProj, XMMatrixTranspose(invViewProj));
	passConstants.EyePosW = mEyePos;
	passConstants.RenderTargetSize = { (float)mClientWidth, (float)mClientHeight };
	passConstants.InvRenderTargetSize = { 1.0f / (float)mClientWidth, 1.0f / (float)mClientHeight };
	passConstants.NearZ = 1.0f;
	passConstants.FarZ = 1000.0f;
	passConstants.TotalTime = gt.TotalTime();
	passConstants.DeltaTime = gt.DeltaTime();
	passConstants.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };
	passConstants.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
	passConstants.Lights[0].Strength = { 0.6f, 0.6f, 0.6f };
	passConstants.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	passConstants.Lights[1].Strength = { 0.3f, 0.3f, 0.3f };
	passConstants.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
	passConstants.Lights[2].Strength = { 0.15f, 0.15f, 0.15f };

	curPassCB->CopyData(0, passConstants);
}

void TreeBillboardsApp::UpdateMaterialCBs(const GameTimer& gt)
{
	auto& curMatCB = mCurFrameRes->MaterialCB;
	for (auto& mat : mMaterials)
	{
		auto& m = mat.second;
		if (m->NumFramesDirty <= 0)
			continue;

		XMMATRIX matTransform = XMLoadFloat4x4(&m->MatTransform);

		MaterialConstants matConstants;
		matConstants.DiffuseAlbedo = m->DiffuseAlbedo;
		matConstants.FresnelR0 = m->FresnelR0;
		matConstants.Roughness = m->Roughness;
		XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));

		curMatCB->CopyData(m->MatCBIndex, matConstants);

		m->NumFramesDirty--;
	}
}

void TreeBillboardsApp::Update(const GameTimer& gt)
{
	OnKeyboardInput(gt);
	UpdateCamera(gt);

	mCurFrameResIdx = (mCurFrameResIdx + 1) % gNumFrameResources;
	mCurFrameRes = mFrameResources[mCurFrameResIdx].get();
	if (mCurFrameRes->Fence != 0 && mFence->GetCompletedValue() < mCurFrameRes->Fence)
	{
		HANDLE hEvent = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurFrameRes->Fence, hEvent));
		WaitForSingleObject(hEvent, INFINITE);
		CloseHandle(hEvent);
	}

	UpdateWaves(gt);
	AnimateMaterials(gt);
	UpdateObjectCBs(gt);
	UpdateMainPassCB(gt);
	UpdateMaterialCBs(gt);
}

void TreeBillboardsApp::DrawRenderItems(
	ID3D12GraphicsCommandList* cmdList, 
	const std::vector<RenderItem*>& ritems)
{
	auto& objCB = mCurFrameRes->ObjectCB;
	auto& matCB = mCurFrameRes->MaterialCB;
	UINT objByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

	for (auto ri : ritems)
	{
		CD3DX12_GPU_DESCRIPTOR_HANDLE hSrv{ mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart() };
		hSrv.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvDescriptorSize);
		cmdList->SetGraphicsRootDescriptorTable(0, hSrv);

		auto res = objCB->Resource();
		D3D12_GPU_VIRTUAL_ADDRESS addrObj{ objCB->Resource()->GetGPUVirtualAddress() };
		addrObj += ri->ObjCBIndex * objByteSize;
		cmdList->SetGraphicsRootConstantBufferView(1, addrObj);
		
		D3D12_GPU_VIRTUAL_ADDRESS addrMat{ matCB->Resource()->GetGPUVirtualAddress() };
		addrMat += ri->Mat->MatCBIndex * matByteSize;
		cmdList->SetGraphicsRootConstantBufferView(3, addrMat);

		D3D12_VERTEX_BUFFER_VIEW vtxView;
		vtxView.BufferLocation = ri->Geo->VertexBufferGPU->GetGPUVirtualAddress();
		vtxView.SizeInBytes = ri->Geo->VertexBufferByteSize;
		vtxView.StrideInBytes = ri->Geo->VertexByteStride;
		cmdList->IASetVertexBuffers(0, 1, &vtxView);

		D3D12_INDEX_BUFFER_VIEW idxView;
		idxView.BufferLocation = ri->Geo->IndexBufferGPU->GetGPUVirtualAddress();
		idxView.Format = DXGI_FORMAT_R16_UINT;
		idxView.SizeInBytes = ri->Geo->IndexBufferByteSize;
		cmdList->IASetIndexBuffer(&idxView);
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);
		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
		//cmdList->DrawInstanced(4, 1, 0, 0);
		//cmdList->DrawInstanced(4, 1, 4, 0);
		//cmdList->DrawInstanced(4, 1, 8, 0);
		//cmdList->DrawInstanced(4, 1, 12, 0);
	}
}

void TreeBillboardsApp::Draw(const GameTimer& gt)
{
	auto cmdListAlloc = mCurFrameRes->CmdListAlloc;
	ThrowIfFailed(cmdListAlloc->Reset());
	ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs[PSOType::Opaque].second.Get()));

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	mCommandList->ResourceBarrier(1, &RvToLv(CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET)));

	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
		1.0f, 0, 0, nullptr);
	mCommandList->OMSetRenderTargets(1, &RvToLv(CurrentBackBufferView()), true, &RvToLv(DepthStencilView()));

	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	D3D12_GPU_VIRTUAL_ADDRESS addrPassCB(mCurFrameRes->PassCB->Resource()->GetGPUVirtualAddress());
	mCommandList->SetGraphicsRootConstantBufferView(2, addrPassCB);

	mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	DrawRenderItems(mCommandList.Get(), mRitemLayer[RenderLayer::Opaque]);

	mCommandList->SetPipelineState(mPSOs[PSOType::CircleGS].second.Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[RenderLayer::Circle]);

	mCommandList->SetPipelineState(mPSOs[PSOType::SphereGS].second.Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[RenderLayer::Sphere]);

	mCommandList->SetPipelineState(mPSOs[PSOType::AlphaTested].second.Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[RenderLayer::AlphaTested]);

	mCommandList->SetPipelineState(mPSOs[PSOType::AlphaTestedTreeSprites].second.Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[RenderLayer::AlphaTestedTreeSprites]);

	mCommandList->SetPipelineState(mPSOs[PSOType::Transparent].second.Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[RenderLayer::Transparent]);

	mCommandList->SetPipelineState(mPSOs[PSOType::VertexNormalGS].second.Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[RenderLayer::Opaque]);
	DrawRenderItems(mCommandList.Get(), mRitemLayer[RenderLayer::AlphaTested]);
	DrawRenderItems(mCommandList.Get(), mRitemLayer[RenderLayer::AlphaTestedTreeSprites]);
	DrawRenderItems(mCommandList.Get(), mRitemLayer[RenderLayer::Transparent]);

	mCommandList->SetPipelineState(mPSOs[PSOType::FaceNormalGS].second.Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[RenderLayer::Opaque]);
	DrawRenderItems(mCommandList.Get(), mRitemLayer[RenderLayer::AlphaTested]);
	DrawRenderItems(mCommandList.Get(), mRitemLayer[RenderLayer::AlphaTestedTreeSprites]);
	DrawRenderItems(mCommandList.Get(), mRitemLayer[RenderLayer::Transparent]);

	mCommandList->ResourceBarrier(1, &RvToLv(CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT)));

	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* ppCommandLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	mSwapChain->Present(0, 0);

	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;
	mCurFrameRes->Fence = ++mCurrentFence;
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void TreeBillboardsApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWnd);
}
void TreeBillboardsApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}
void TreeBillboardsApp::OnMouseMove(WPARAM btnState, int x, int y)
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
		TreeBillboardsApp theApp(hInstance);
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
 
 