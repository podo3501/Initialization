#include "CubeMapApp.h"
#include "../Common/d3dUtil.h"
#include "../Common/Util.h"
#include "../Common/UploadBuffer.h"
#include "FrameResource.h"
#include "../Common/GeometryGenerator.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

const int gNumFrameResources = 3;

CubeMapApp::CubeMapApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
}

CubeMapApp::~CubeMapApp()
{
	if (md3dDevice != nullptr)
		FlushCommandQueue();
}

bool CubeMapApp::Initialize()
{
	if (!D3DApp::Initialize())
		return false;

	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	mCamera.LookAt(
		XMFLOAT3(5.0f, 4.0f, -15.0f),
		XMFLOAT3(0.0f, 1.0f, 0.0f),
		XMFLOAT3(0.0f, 1.0f, 0.0f));

	LoadTextures();
	BuildRootSignature();
	BuildDescriptorHeaps();
	BuildShadersAndInputLayout();
	BuildCarGeometry();
	BuildMaterials();
	BuildRenderItems();
	BuildFrameResources();
	BuildPSOs();

	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists2[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists2), cmdsLists2);

	FlushCommandQueue();

	return true;
}

void CubeMapApp::LoadTextures()
{
	std::vector<std::wstring> filenames 
	{ 
		L"bricks2.dds",
		L"tile.dds",
		L"white1x1.dds",
		L"grasscube1024.dds"
	};
	
	for_each(filenames.begin(), filenames.end(), [&](auto& curFilename) {
		auto tex = std::make_unique<Texture>();
		tex->Filename = L"../Textures/" + curFilename;
		ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(), mCommandList.Get(),
			tex->Filename.c_str(), tex->Resource, tex->UploadHeap));
		mTextures.emplace_back(std::move(tex));
		});
}

void CubeMapApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE cubeTexTable{}, texTable{};
	cubeTexTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);
	texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5, 1, 0);

	CD3DX12_ROOT_PARAMETER slotRootParameter[5];
	slotRootParameter[0].InitAsConstantBufferView(0);
	slotRootParameter[1].InitAsConstantBufferView(1);
	slotRootParameter[2].InitAsShaderResourceView(0, 1);
	slotRootParameter[3].InitAsDescriptorTable(1, &cubeTexTable, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[4].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);

	auto staticSamplers = d3dUtil::GetStaticSamplers();
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc{
		_countof(slotRootParameter), slotRootParameter,
		static_cast<UINT>(staticSamplers.size()), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT };

	ComPtr<ID3DBlob> serialized = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serialized.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr) ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	ThrowIfFailed(hr);
	
	ThrowIfFailed(md3dDevice->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), 
		IID_PPV_ARGS(&mRootSignature)));
}

void CubeMapApp::BuildDescriptorHeaps()
{
	UINT mSkyTexHeapIndex = 0;

	D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	heapDesc.NodeMask = 0;
	heapDesc.NumDescriptors = static_cast<UINT>(mTextures.size());
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	for_each(mTextures.begin(), mTextures.end(), [&, index{ 0 }](auto& curTex) mutable {
		auto& curTexResource = curTex->Resource;
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = curTexResource->GetDesc().Format;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MipLevels = curTexResource->GetDesc().MipLevels;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;

		if (curTex->Filename.find(L"cube") != std::wstring::npos)
		{
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			srvDesc.TextureCube.MostDetailedMip = 0;
			srvDesc.TextureCube.MipLevels = curTexResource->GetDesc().MipLevels;
			srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
			srvDesc.Format = curTexResource->GetDesc().Format;
			mSkyTexHeapIndex = index;
		}
		
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDesc{ mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart() };
		hCpuDesc.Offset(index++, mCbvSrvUavDescriptorSize);
		md3dDevice->CreateShaderResourceView(curTex->Resource.Get(), &srvDesc, hCpuDesc);
		});
}

void CubeMapApp::BuildShadersAndInputLayout()
{
}

void CubeMapApp::BuildCarGeometry()
{
}

void CubeMapApp::BuildMaterials()
{
	auto MakeMaterial = [&](std::string&& name, int matCBIdx, int diffuseSrvHeapIdx,
		XMFLOAT4 diffuseAlbedo, XMFLOAT3 fresnelR0, float rough) {
		auto curMat = std::make_unique<Material>();
		curMat->Name = name;
		curMat->MatCBIndex = matCBIdx;
		curMat->DiffuseSrvHeapIndex = diffuseSrvHeapIdx;
		curMat->DiffuseAlbedo = diffuseAlbedo;
		curMat->FresnelR0 = fresnelR0;
		curMat->Roughness = rough;
		mMaterials[name] = std::move(curMat);
		};
}

void CubeMapApp::BuildRenderItems()
{
	auto MakeRenderItem = [&, objIdx{ 0 }](std::string&& geoName, std::string&& smName, std::string&& matName,
		const XMMATRIX& world, const XMMATRIX& texTransform, RenderLayer renderLayer, bool visible = true) mutable {
		auto renderItem = std::make_unique<RenderItem>();
		if (smName.empty() == false)
		{
			auto& sm = mGeometries[geoName]->DrawArgs[smName];
			renderItem->StartIndexLocation = sm.StartIndexLocation;
			renderItem->BaseVertexLocation = sm.BaseVertexLocation;
			renderItem->IndexCount = sm.IndexCount;
			renderItem->BBounds = sm.BBounds;
			renderItem->BSphere = sm.BSphere;
		}
		renderItem->Geo = mGeometries[geoName].get();
		renderItem->Mat = mMaterials[matName].get();
		renderItem->ObjCBIndex = objIdx++;
		renderItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		XMStoreFloat4x4(&renderItem->World, world);
		XMStoreFloat4x4(&renderItem->TexTransform, texTransform);
		renderItem->Visible = visible;
		mRitemLayer[renderLayer].emplace_back(renderItem.get());
		mAllRitems.emplace_back(std::move(renderItem));};
}

void CubeMapApp::BuildFrameResources()
{
	for (auto i : Range(0, gNumFrameResources))
	{
		auto frameRes = std::make_unique<FrameResource>(md3dDevice.Get(), 1,
			static_cast<UINT>(mAllRitems.size()), static_cast<UINT>(mMaterials.size()));
		mFrameResources.emplace_back(std::move(frameRes));
	}
}

D3D12_SHADER_BYTECODE GetShaderBytecode(
	std::unordered_map<std::string, ComPtr<ID3DBlob>>& shaders, std::string&& name)
{
	return { shaders[name]->GetBufferPointer(), shaders[name]->GetBufferSize() };
}

void CubeMapApp::MakeOpaqueDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc)
{
	inoutDesc->VS = GetShaderBytecode(mShaders, "standardVS");
	inoutDesc->PS = GetShaderBytecode(mShaders, "opaquePS");
	inoutDesc->NodeMask = 0;
	inoutDesc->SampleMask = UINT_MAX;
	inoutDesc->NumRenderTargets = 1;
	inoutDesc->InputLayout = { mInputLayout.data(), static_cast<UINT>(mInputLayout.size()) };
	inoutDesc->pRootSignature = mRootSignature.Get();
	inoutDesc->BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	inoutDesc->DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	inoutDesc->RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	inoutDesc->RTVFormats[0] = mBackBufferFormat;
	inoutDesc->DSVFormat = mDepthStencilFormat;
	inoutDesc->PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	inoutDesc->SampleDesc.Count = m4xMsaaState ? 4 : 1;
	inoutDesc->SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
}

void CubeMapApp::MakeHighlightDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc)
{
	inoutDesc->DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	D3D12_RENDER_TARGET_BLEND_DESC blendDesc;
	blendDesc.BlendEnable = true;
	blendDesc.LogicOpEnable = false;
	blendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
	blendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	blendDesc.BlendOp = D3D12_BLEND_OP_ADD;
	blendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
	blendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
	blendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	blendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
	blendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	inoutDesc->BlendState.RenderTarget[0] = blendDesc;
}

void CubeMapApp::MakePSOPipelineState(GraphicsPSO psoType)
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
	MakeOpaqueDesc(&psoDesc);

	switch (psoType)
	{
	case GraphicsPSO::Opaque:		break;
	case GraphicsPSO::Highlight:	MakeHighlightDesc(&psoDesc);		break;
	default: assert(!"wrong type");
	}

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSOs[psoType])));
}

void CubeMapApp::BuildPSOs()
{
	for (auto gPso : GraphicsPSO_ALL)
		MakePSOPipelineState(gPso);
}

void CubeMapApp::OnResize()
{
	D3DApp::OnResize();

	mCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.f);
}

void CubeMapApp::OnKeyboardInput(const GameTimer& gt)
{
	const float dt = gt.DeltaTime();

	float speed = 10.0f;
	float walkSpeed = 0.0f;
	float strafeSpeed = 0.0f;

	std::vector<int> keyList{ 'W', 'S', 'D', 'A' };
	for_each(keyList.begin(), keyList.end(), [&](int vKey) {
		bool bPressed = GetAsyncKeyState(vKey) & 0x8000;
		if (bPressed)
		{
			switch (vKey)
			{
			case 'W':		walkSpeed += speed;		break;
			case 'S':		walkSpeed += -speed;		break;
			case 'D':		strafeSpeed += speed;		break;
			case 'A':		strafeSpeed += -speed;		break;
			}
		}});
	
	mCamera.Move(Camera::eWalk, walkSpeed * dt);
	mCamera.Move(Camera::eStrafe, strafeSpeed * dt);

	mCamera.UpdateViewMatrix();
}

void CubeMapApp::AnimateMaterials(const GameTimer& gt)
{
}

void StoreMatrix4x4(XMFLOAT4X4& dest, XMFLOAT4X4& src) { XMStoreFloat4x4(&dest, XMMatrixTranspose(XMLoadFloat4x4(&src))); }
void StoreMatrix4x4(XMFLOAT4X4& dest, XMMATRIX src) { XMStoreFloat4x4(&dest, XMMatrixTranspose(src)); }
XMMATRIX Multiply(XMFLOAT4X4& m1, XMFLOAT4X4 m2) { return XMMatrixMultiply(XMLoadFloat4x4(&m1), XMLoadFloat4x4(&m2)); }
XMMATRIX Inverse(XMMATRIX& m) { return XMMatrixInverse(nullptr, m); }
XMMATRIX Inverse(XMFLOAT4X4& src) {	return Inverse(RvToLv(XMLoadFloat4x4(&src))); }

void CubeMapApp::UpdateObjectCBs(const GameTimer& gt)
{
}

void CubeMapApp::UpdateMaterialBuffer(const GameTimer& gt)
{
}

void CubeMapApp::UpdateMainPassCB(const GameTimer& gt)
{
}

void CubeMapApp::Update(const GameTimer& gt)
{
	OnKeyboardInput(gt);

	mFrameResIdx = (mFrameResIdx + 1) % gNumFrameResources;
	mCurFrameRes = mFrameResources[mFrameResIdx].get();
	if (mCurFrameRes->Fence != 0 && mFence->GetCompletedValue() < mCurFrameRes->Fence)
	{
		HANDLE hEvent = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurFrameRes->Fence, hEvent));
		WaitForSingleObject(hEvent, INFINITE);
		CloseHandle(hEvent);
	}
	
	AnimateMaterials(gt);
	UpdateObjectCBs(gt);
	UpdateMaterialBuffer(gt);
	UpdateMainPassCB(gt);
}

void CubeMapApp::DrawRenderItems(
	ID3D12GraphicsCommandList* cmdList,
	const std::vector<RenderItem*> ritems)
{
}

void CubeMapApp::Draw(const GameTimer& gt)
{
}

void CubeMapApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		mLastMousePos.x = x;
		mLastMousePos.y = y;

		SetCapture(mhMainWnd);
	}
}
void CubeMapApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}
void CubeMapApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		// Make each pixel correspond to a quarter of a degree.
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

		mCamera.Move(Camera::ePitch, dy);
		mCamera.Move(Camera::eRotateY, dx);
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
		CubeMapApp theApp(hInstance);
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