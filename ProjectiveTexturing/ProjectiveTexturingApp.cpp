﻿#include "ProjectiveTexturingApp.h"
#include "../Common/d3dUtil.h"
#include "../Common/Util.h"
#include "../Common/UploadBuffer.h"
#include "FrameResource.h"
#include "../Common/GeometryGenerator.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

const int gNumFrameResources = 3;

ProjectiveTexturingApp::ProjectiveTexturingApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
}

ProjectiveTexturingApp::~ProjectiveTexturingApp()
{
	if (md3dDevice != nullptr)
		FlushCommandQueue();
}

bool ProjectiveTexturingApp::Initialize()
{
	if (!D3DApp::Initialize())
		return false;

	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	mCamera.SetPosition(0.0f, 2.0f, -15.0f);

	LoadTextures();
	BuildRootSignature();
	BuildDescriptorHeaps();
	BuildShadersAndInputLayout();
	BuildShapeGeometry();
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

void ProjectiveTexturingApp::CreateRtvAndDsvDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
	rtvHeapDesc.NumDescriptors = SwapChainBufferCount;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
		&rtvHeapDesc, IID_PPV_ARGS(mRtvHeap.GetAddressOf())));

	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
	dsvHeapDesc.NumDescriptors = 2;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
		&dsvHeapDesc, IID_PPV_ARGS(mDsvHeap.GetAddressOf())));
}

void ProjectiveTexturingApp::LoadTextures()
{
	std::vector<std::wstring> filenames 
	{ 
		L"bricks2.dds"
	};
	
	for_each(filenames.begin(), filenames.end(), [&](auto& curFilename) {
		auto tex = std::make_unique<Texture>();
		tex->Filename = L"../Textures/" + curFilename;
		ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(), mCommandList.Get(),
			tex->Filename.c_str(), tex->Resource, tex->UploadHeap));
		mTextures.emplace_back(std::move(tex));
		});
}

void ProjectiveTexturingApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable{};
	texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5, 0, 0);

	CD3DX12_ROOT_PARAMETER slotRootParameter[4];
	slotRootParameter[0].InitAsConstantBufferView(0);
	slotRootParameter[1].InitAsConstantBufferView(1);
	slotRootParameter[2].InitAsShaderResourceView(0, 1);
	slotRootParameter[3].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);

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

void ProjectiveTexturingApp::BuildDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	heapDesc.NodeMask = 0;
	heapDesc.NumDescriptors = 5;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	UINT index = 0;
	for_each(mTextures.begin(), mTextures.end(), [&](auto& curTex) {
		auto& curTexResource = curTex->Resource;	
		srvDesc.Format = curTexResource->GetDesc().Format;
		srvDesc.Texture2D.MipLevels = curTexResource->GetDesc().MipLevels;

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

void ProjectiveTexturingApp::BuildShadersAndInputLayout()
{
	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders/StandardVS.hlsl", nullptr, "main", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders/OpaquePS.hlsl", nullptr, "main", "ps_5_1");

	mInputLayout =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	};
}

void ProjectiveTexturingApp::BuildShapeGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(5.0f, 20, 20);

	UINT sphereVertexOffset = 0;
	UINT sphereIndexOffset = 0;

	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

	auto totalVertexCount = sphere.Vertices.size();
	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;
	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Normal = sphere.Vertices[i].Normal;
		vertices[k].TexC = sphere.Vertices[i].TexC;
	}

	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "shapeGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->DrawArgs["sphere"] = sphereSubmesh;

	mGeometries[geo->Name] = std::move(geo);
}

void ProjectiveTexturingApp::BuildMaterials()
{
	auto MakeMaterial = [&](std::string&& name, int matCBIdx, int diffuseSrvHeapIdx, int normalSrvHeapIdx,
		XMFLOAT4 diffuseAlbedo, XMFLOAT3 fresnelR0, float rough) {
			auto curMat = std::make_unique<Material>();
			curMat->Name = name;
			curMat->MatCBIndex = matCBIdx;
			curMat->DiffuseSrvHeapIndex = diffuseSrvHeapIdx;
			curMat->NormalSrvHeapIndex = normalSrvHeapIdx;
			curMat->DiffuseAlbedo = diffuseAlbedo;
			curMat->FresnelR0 = fresnelR0;
			curMat->Roughness = rough;
			mMaterials[name] = std::move(curMat);
		};

	MakeMaterial("bricks0", 0, 0, 1, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.1f, 0.1f, 0.1f }, 0.3f);
}

void ProjectiveTexturingApp::BuildRenderItems()
{
	auto MakeRenderItem = [&, objIdx{ 0 }](std::string&& geoName, std::string&& smName, std::string&& matName,
		const XMMATRIX& world, const XMMATRIX& texTransform, RenderLayer renderLayer, bool visible = true) mutable {
		auto renderItem = std::make_unique<RenderItem>();
		auto& sm = mGeometries[geoName]->DrawArgs[smName];
		renderItem->Geo = mGeometries[geoName].get();
		renderItem->StartIndexLocation = sm.StartIndexLocation;
		renderItem->BaseVertexLocation = sm.BaseVertexLocation;
		renderItem->IndexCount = sm.IndexCount;
		renderItem->Mat = mMaterials[matName].get();
		renderItem->ObjCBIndex = objIdx++;
		renderItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		XMStoreFloat4x4(&renderItem->World, world);
		XMStoreFloat4x4(&renderItem->TexTransform, texTransform);
		renderItem->Visible = visible;
		mRitemLayer[renderLayer].emplace_back(renderItem.get());
		mAllRitems.emplace_back(std::move(renderItem));
	};

	MakeRenderItem("shapeGeo", "sphere", "bricks0", XMMatrixIdentity(), XMMatrixIdentity(), RenderLayer::Opaque);
}

void ProjectiveTexturingApp::BuildFrameResources()
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

void ProjectiveTexturingApp::MakeOpaqueDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC* inoutDesc)
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

void ProjectiveTexturingApp::MakePSOPipelineState(GraphicsPSO psoType)
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
	MakeOpaqueDesc(&psoDesc);

	switch (psoType)
	{
	case GraphicsPSO::Opaque:					break;
	default: assert(!"wrong type");
	}

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSOs[psoType])));
}

void ProjectiveTexturingApp::BuildPSOs()
{
	for (auto gPso : GraphicsPSO_ALL)
		MakePSOPipelineState(gPso);
}

void ProjectiveTexturingApp::OnResize()
{
	D3DApp::OnResize();

	mCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.f);
}

void ProjectiveTexturingApp::OnKeyboardInput(const GameTimer& gt)
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

void ProjectiveTexturingApp::AnimateMaterials(const GameTimer& gt)
{
}

void StoreMatrix4x4(XMFLOAT4X4& dest, XMFLOAT4X4& src) { XMStoreFloat4x4(&dest, XMMatrixTranspose(XMLoadFloat4x4(&src))); }
void StoreMatrix4x4(XMFLOAT4X4& dest, XMMATRIX src) { XMStoreFloat4x4(&dest, XMMatrixTranspose(src)); }
XMMATRIX Multiply(XMFLOAT4X4& m1, XMFLOAT4X4 m2) { return XMMatrixMultiply(XMLoadFloat4x4(&m1), XMLoadFloat4x4(&m2)); }
XMMATRIX Inverse(XMMATRIX& m) { return XMMatrixInverse(nullptr, m); }
XMMATRIX Inverse(XMFLOAT4X4& src) {	return Inverse(RvToLv(XMLoadFloat4x4(&src))); }

void ProjectiveTexturingApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurFrameRes->ObjectCB.get();
	for (auto& e : mAllRitems)
	{
		if (e->NumFramesDirty <= 0)
			continue;

		ObjectConstants objConstants;
		StoreMatrix4x4(objConstants.World, e->World);
		StoreMatrix4x4(objConstants.TexTransform, e->TexTransform);
		objConstants.MaterialIndex = e->Mat->MatCBIndex;

		currObjectCB->CopyData(e->ObjCBIndex, objConstants);

		e->NumFramesDirty--;
	}
}

void ProjectiveTexturingApp::UpdateMaterialBuffer(const GameTimer& gt)
{
	auto currMaterialBuffer = mCurFrameRes->MaterialBuffer.get();
	for (auto& e : mMaterials)
	{
		Material* mat = e.second.get();
		if (mat->NumFramesDirty <= 0)
			continue;

		MaterialData matData;
		matData.DiffuseAlbedo = mat->DiffuseAlbedo;
		matData.FresnelR0 = mat->FresnelR0;
		matData.Roughness = mat->Roughness;
		matData.DiffuseMapIndex = mat->DiffuseSrvHeapIndex;
		matData.NormalMapIndex = mat->NormalSrvHeapIndex;
		StoreMatrix4x4(matData.MatTransform, mat->MatTransform);

		currMaterialBuffer->CopyData(mat->MatCBIndex, matData);

		mat->NumFramesDirty--;
	}
}

void ProjectiveTexturingApp::UpdateMainPassCB(const GameTimer& gt)
{
	auto& passCB = mCurFrameRes->PassCB;

	XMMATRIX view = mCamera.GetView();
	XMMATRIX proj = mCamera.GetProj();

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);

	PassConstants pc;
	StoreMatrix4x4(pc.View, view);
	StoreMatrix4x4(pc.InvView, Inverse(view));
	StoreMatrix4x4(pc.Proj, proj);
	StoreMatrix4x4(pc.InvProj, Inverse(proj));
	StoreMatrix4x4(pc.ViewProj, viewProj);
	StoreMatrix4x4(pc.InvViewProj, Inverse(viewProj));
	StoreMatrix4x4(pc.ShadowTransform, mShadowTransform);
	pc.EyePosW = mCamera.GetPosition3f();
	pc.RenderTargetSize = { (float)mClientWidth, (float)mClientHeight };
	pc.InvRenderTargetSize = { 1.0f / (float)mClientWidth, 1.0f / (float)mClientHeight };
	pc.NearZ = 1.0f;
	pc.FarZ = 1000.0f;
	pc.TotalTime = gt.TotalTime();
	pc.DeltaTime = gt.DeltaTime();
	pc.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };
	pc.Lights[0].Direction = mRotatedLightDirections[0];
	pc.Lights[0].Strength = { 0.9f, 0.8f, 0.7f };
	pc.Lights[1].Direction = mRotatedLightDirections[1];
	pc.Lights[1].Strength = { 0.4f, 0.4f, 0.4f };
	pc.Lights[2].Direction = mRotatedLightDirections[2];
	pc.Lights[2].Strength = { 0.2f, 0.2f, 0.2f };

	passCB->CopyData(0, pc);
}

void ProjectiveTexturingApp::Update(const GameTimer& gt)
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

	mLightRotationAngle += 0.1f * gt.DeltaTime();
	XMMATRIX R = XMMatrixRotationY(mLightRotationAngle);
	for (auto i : Range(0, 3))
	{
		XMVECTOR lightDir = XMLoadFloat3(&mBaseLightDirections[i]);
		lightDir = XMVector3TransformNormal(lightDir, R);
		XMStoreFloat3(&mRotatedLightDirections[i], lightDir);
	}
	
	AnimateMaterials(gt);
	UpdateObjectCBs(gt);
	UpdateMaterialBuffer(gt);
	UpdateMainPassCB(gt);
}

void ProjectiveTexturingApp::DrawRenderItems(
	ID3D12GraphicsCommandList* cmdList,
	const std::vector<RenderItem*> ritems)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	auto objectCB = mCurFrameRes->ObjectCB->Resource();

	for (auto& ri : ritems)
	{
		cmdList->IASetVertexBuffers(0, 1, &RvToLv(ri->Geo->VertexBufferView()));
		cmdList->IASetIndexBuffer(&RvToLv(ri->Geo->IndexBufferView()));
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
		cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);
		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

void ProjectiveTexturingApp::Draw(const GameTimer& gt)
{
	auto cmdListAlloc = mCurFrameRes->CmdListAlloc;
	ThrowIfFailed(cmdListAlloc->Reset());

	ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs[GraphicsPSO::Opaque].Get()));

	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	mCommandList->ResourceBarrier(1, &RvToLv(CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET)));

	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	mCommandList->OMSetRenderTargets(1, &RvToLv(CurrentBackBufferView()), true, &RvToLv(DepthStencilView()));

	auto passCB = mCurFrameRes->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

	mCommandList->SetGraphicsRootDescriptorTable(3, mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

	mCommandList->SetPipelineState(mPSOs[GraphicsPSO::Opaque].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[RenderLayer::Opaque]);

	mCommandList->ResourceBarrier(1, &RvToLv(CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT)));

	ThrowIfFailed(mCommandList->Close());

	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	mCurFrameRes->Fence = ++mCurrentFence;

	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void ProjectiveTexturingApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		mLastMousePos.x = x;
		mLastMousePos.y = y;

		SetCapture(mhMainWnd);
	}
}
void ProjectiveTexturingApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}
void ProjectiveTexturingApp::OnMouseMove(WPARAM btnState, int x, int y)
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
		ProjectiveTexturingApp theApp(hInstance);
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