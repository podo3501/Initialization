#include "BasicTessellationApp.h"

#include "../Common/Util.h"
#include "../Common/UploadBuffer.h"
#include "FrameResource.h"
#include "../Common/GeometryGenerator.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

const int gNumFrameResources = 3;

BasicTessellationApp::BasicTessellationApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
}

BasicTessellationApp::~BasicTessellationApp()
{
	if (md3dDevice != nullptr)
		FlushCommandQueue();
}

bool BasicTessellationApp::Initialize()
{
	if (!D3DApp::Initialize())
		return false;

	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	LoadTextures();
	BuildRootSignature();
	BuildDescriptorHeaps();
	BuildShadersAndInputLayout();
	BuildLandGeometry();
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

void BasicTessellationApp::LoadTextures()
{
	std::vector<std::wstring> fileList
	{
		L"../Textures/grass.dds",
		L"../Textures/water1.dds",
		L"../Textures/WireFence.dds"
	};
	
	for_each(fileList.begin(), fileList.end(), [&](auto& curName) {
		auto tex = std::make_unique<Texture>();
		ThrowIfFailed(d3dUtil::LoadTextureFromFile(md3dDevice.Get(), mCommandList.Get(),
			curName.c_str(), tex->Resource, tex->UploadHeap));
		mTextures.emplace_back(std::move(tex));
		});
}

void BasicTessellationApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE srvTable
	{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0 };

	const int ParamCount = 4;
	CD3DX12_ROOT_PARAMETER rootParameter[ParamCount];
	rootParameter[0].InitAsDescriptorTable(1, &srvTable, D3D12_SHADER_VISIBILITY_PIXEL);
	rootParameter[1].InitAsConstantBufferView(0);
	rootParameter[2].InitAsConstantBufferView(1);
	rootParameter[3].InitAsConstantBufferView(2);

	auto samplers = d3dUtil::GetStaticSamplers();
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc
	{
		ParamCount, rootParameter,
		static_cast<UINT>(samplers.size()), samplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
	};

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc,
		D3D_ROOT_SIGNATURE_VERSION_1_0, &serializedRootSig, &errorBlob);

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(0,
		serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(&mRootSignature)));
}

UINT BasicTessellationApp::GetNumDescriptors()
{
	return static_cast<UINT>(mTextures.size());
}

void BasicTessellationApp::BuildDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC srvHeap{};
	srvHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeap.NumDescriptors = GetNumDescriptors();
	srvHeap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	srvHeap.NodeMask = 0;
	md3dDevice->CreateDescriptorHeap(&srvHeap, IID_PPV_ARGS(&mDescriptorHeap));

	for_each(mTextures.begin(), mTextures.end(), [&, count{ 0u }](auto& curTex) mutable {
		auto texRes = curTex.get()->Resource;
		auto texDesc = texRes->GetDesc();
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = texDesc.Format;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = texDesc.MipLevels;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
		CD3DX12_CPU_DESCRIPTOR_HANDLE hDesc{ mDescriptorHeap->GetCPUDescriptorHandleForHeapStart() };
		hDesc.Offset(count++, mCbvSrvUavDescriptorSize);
		md3dDevice->CreateShaderResourceView(texRes.Get(), &srvDesc, hDesc);
		});
}

void BasicTessellationApp::BuildShadersAndInputLayout()
{
	const D3D_SHADER_MACRO defines[] = { "FOG", "1", NULL, NULL };
	const D3D_SHADER_MACRO alphaTestDefines[] = { "FOG", "1", "ALPHA_TEST", "1", NULL, NULL };

	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders/VertexShader.hlsl", nullptr, "main", "vs_5_0");
	mShaders["landHS"] = d3dUtil::CompileShader(L"Shaders/LandHS.hlsl", nullptr, "main", "hs_5_0");
	mShaders["landDS"] = d3dUtil::CompileShader(L"Shaders/LandDS.hlsl", nullptr, "main", "ds_5_0");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders/PixelShader.hlsl", nullptr, "main", "ps_5_0");

	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}

float BasicTessellationApp::GetHillsHeight(float x, float z) const
{
	return 0.3f * (z * sinf(0.1f * x) + x * cosf(0.1f * z));
}

XMFLOAT3 BasicTessellationApp::GetHillsNormal(float x, float z) const
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
void BasicTessellationApp::MakeGeometry(const std::string&& geoName, const std::string&& smName,
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

	std::vector<std::int16_t> newIndices{ 0, 1, 2, 1, 3, 2 };
	UINT totalICnt = static_cast<UINT>(newIndices.size());
	UINT idxByteSize = totalICnt * sizeof(std::int16_t);

	mg->Name = geoName;
	mg->DrawArgs[smName] = { totalICnt, 0, 0, {} };
	mg->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
		newIndices.data(), idxByteSize, mg->IndexBufferUploader);
	mg->IndexFormat = DXGI_FORMAT_R16_UINT;
	mg->IndexBufferByteSize = idxByteSize;
	
	mGeometries[mg->Name] = std::move(mg);
}

void BasicTessellationApp::BuildLandGeometry()
{
	GeometryGenerator gen;
	GeometryGenerator::MeshData grid = gen.CreateGrid(160.0f, 160.0f, 2, 2);

	UINT totalVCnt = static_cast<UINT>(grid.Vertices.size());
	UINT vertexBufferByteSize = totalVCnt * sizeof(Vertex);
	std::vector<Vertex> vertices(totalVCnt);
	for (auto i : Range(0, totalVCnt))
	{
		vertices[i].Pos = grid.Vertices[i].Position;
		//vertices[i].Pos.y = GetHillsHeight(vertices[i].Pos.x, vertices[i].Pos.z);
		//vertices[i].Normal = GetHillsNormal(vertices[i].Pos.x, vertices[i].Pos.z);
		//vertices[i].TexC = grid.Vertices[i].TexC;
	}

	UINT totalICnt = static_cast<UINT>(grid.Indices32.size());
	UINT indexBufferByteSize = totalICnt * sizeof(std::uint16_t);
	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), grid.GetIndices16().begin(), grid.GetIndices16().end());

	MakeGeometry("landGeo", "grid", vertices, indices);
}

void BasicTessellationApp::BuildMaterials()
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

	CreateMaterial("grass", 0, 0, { 0.0f, 0.6f, 0.0f, 1.0f }, { 0.01f, 0.01f, 0.01f }, 0.125f);
	CreateMaterial("water", 1, 1, { 1.0f, 1.0f, 1.0f, 0.5f }, { 0.1f, 0.1f, 0.1f }, 0.0f);
	CreateMaterial("wirefence", 2, 2, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.02f, 0.02f, 0.02f }, 0.25f);
}

void BasicTessellationApp::BuildRenderItems()
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
		RenderDesc{ "landGeo", "grid", "grass", XMMatrixIdentity(), XMMatrixScaling(5.0f, 5.0f, 1.0f),
			RenderLayer::Opaque, D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST },
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
}

void BasicTessellationApp::BuildFrameResources()
{
	for (auto i : Range(0, gNumFrameResources))
	{
		auto frameRes = std::make_unique<FrameResource>(md3dDevice.Get(), 1, (UINT)mAllRitems.size(),
			(UINT)mMaterials.size());
		mFrameResources.emplace_back(std::move(frameRes));
	}
}

D3D12_SHADER_BYTECODE BasicTessellationApp::GetShaderBytecode(const std::string&& shaderName)
{
	return { mShaders[shaderName]->GetBufferPointer(), mShaders[shaderName]->GetBufferSize() };
}

void BasicTessellationApp::MakeOpaqueDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc)
{
	inoutDesc->VS = GetShaderBytecode("standardVS");
	inoutDesc->HS = GetShaderBytecode("landHS");
	inoutDesc->DS = GetShaderBytecode("landDS");
	inoutDesc->PS = GetShaderBytecode("opaquePS");
	inoutDesc->RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	inoutDesc->RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
	inoutDesc->BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	inoutDesc->DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	inoutDesc->SampleMask = UINT_MAX;
	inoutDesc->SampleDesc.Count = m4xMsaaState ? 4 : 1;
	inoutDesc->SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	inoutDesc->DSVFormat = mDepthStencilFormat;
	inoutDesc->RTVFormats[0] = mBackBufferFormat;
	inoutDesc->pRootSignature = mRootSignature.Get();
	inoutDesc->InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	inoutDesc->NumRenderTargets = 1;
	inoutDesc->PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
}

void BasicTessellationApp::MakePSOPipelineState(GraphicsPSO psoType)
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
	MakeOpaqueDesc(&psoDesc);

	switch (psoType)
	{
	case GraphicsPSO::Opaque:						break;
	default: assert(!"wrong type");
	}

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&psoDesc,
		IID_PPV_ARGS(&mGraphicsPSOs[psoType])));
}

void BasicTessellationApp::BuildPSOs()
{
	for (auto gPso : GraphicsPSO_ALL)
		MakePSOPipelineState(gPso);
}

void BasicTessellationApp::OnResize()
{
	D3DApp::OnResize();

	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 2000.0f);
	XMStoreFloat4x4(&mProj, P);
}

void BasicTessellationApp::OnKeyboardInput(const GameTimer& gt)
{
}

void BasicTessellationApp::UpdateCamera(const GameTimer& gt)
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

void BasicTessellationApp::AnimateMaterials(const GameTimer& gt)
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

void BasicTessellationApp::UpdateObjectCBs(const GameTimer& gt)
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

void BasicTessellationApp::UpdateMaterialCBs(const GameTimer& gt)
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

void BasicTessellationApp::UpdateMainPassCB(const GameTimer& gt)
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

void BasicTessellationApp::UpdateWaves(const GameTimer& gt)
{
}

void BasicTessellationApp::Update(const GameTimer& gt)
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

	AnimateMaterials(gt);
	UpdateObjectCBs(gt);
	UpdateMaterialCBs(gt);
	UpdateMainPassCB(gt);
	UpdateWaves(gt);
}

void BasicTessellationApp::DrawRenderItems(
	ID3D12GraphicsCommandList* cmdList,
	const std::vector<RenderItem*>& ritems)
{
	auto& objCB = mCurFrameRes->ObjectCB;
	auto& matCB = mCurFrameRes->MaterialCB;
	UINT objByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

	for (auto ri : ritems)
	{
		CD3DX12_GPU_DESCRIPTOR_HANDLE hSrv{ mDescriptorHeap->GetGPUDescriptorHandleForHeapStart() };
		hSrv.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvUavDescriptorSize);
		cmdList->SetGraphicsRootDescriptorTable(0, hSrv);

		auto res = objCB->Resource();
		D3D12_GPU_VIRTUAL_ADDRESS addrObj{ objCB->Resource()->GetGPUVirtualAddress() };
		addrObj += ri->ObjCBIndex * objByteSize;
		cmdList->SetGraphicsRootConstantBufferView(1, addrObj);

		D3D12_GPU_VIRTUAL_ADDRESS addrMat{ matCB->Resource()->GetGPUVirtualAddress() };
		addrMat += ri->Mat->MatCBIndex * matByteSize;
		cmdList->SetGraphicsRootConstantBufferView(3, addrMat);

		cmdList->IASetVertexBuffers(0, 1, &RvToLv(ri->Geo->VertexBufferView()));
		cmdList->IASetIndexBuffer(&RvToLv(ri->Geo->IndexBufferView()));
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);
		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

void BasicTessellationApp::Draw(const GameTimer& gt)
{
	auto cmdListAlloc = mCurFrameRes->CmdListAlloc;
	ThrowIfFailed(cmdListAlloc->Reset());
	ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mGraphicsPSOs[GraphicsPSO::Opaque].Get()));

	ID3D12DescriptorHeap* descriptorHeaps[] = { mDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	mCommandList->ResourceBarrier(1, &RvToLv(CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET)));

	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
		1.0f, 0, 0, nullptr);
	mCommandList->OMSetRenderTargets(1, &RvToLv(CurrentBackBufferView()), true, &RvToLv(DepthStencilView()));

	D3D12_GPU_VIRTUAL_ADDRESS addrPassCB(mCurFrameRes->PassCB->Resource()->GetGPUVirtualAddress());
	mCommandList->SetGraphicsRootConstantBufferView(2, addrPassCB);

	mCommandList->SetPipelineState(mGraphicsPSOs[GraphicsPSO::Opaque].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[RenderLayer::Opaque]);

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

void BasicTessellationApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWnd);
}
void BasicTessellationApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}
void BasicTessellationApp::OnMouseMove(WPARAM btnState, int x, int y)
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
		float dx = 0.7f * static_cast<float>(x - mLastMousePos.x);
		float dy = 0.7f * static_cast<float>(y - mLastMousePos.y);

		mRadius += dx - dy;
		mRadius = MathHelper::Clamp(mRadius, 5.0f, 1000.f);
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
		BasicTessellationApp theApp(hInstance);
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