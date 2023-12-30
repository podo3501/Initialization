//***************************************************************************************
// Waves.h by Frank Luna (C) 2011 All Rights Reserved.
//
// Performs the calculations for the wave simulation.  After the simulation has been
// updated, the client must copy the current solution into vertex buffers for rendering.
// This class only does the calculations, it does not do any drawing.
//***************************************************************************************

#ifndef WAVES_H
#define WAVES_H

#include <vector>
#include <DirectXMath.h>
#include <map>
#include <unordered_map>
#include <array>
#include <wrl.h>
#include <d3dcommon.h>
#include <d3d12.h>

class GameTimer;

enum class ComputePSO : int
{
    WavesCS = 0,
    DisturbCS,
    Count,
};

constexpr std::array<ComputePSO, static_cast<size_t>(ComputePSO::Count)> ComputePSO_ALL
{
    ComputePSO::WavesCS,
    ComputePSO::DisturbCS,
};

class Waves
{
public:
    Waves(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, 
        int m, int n, float dx, float dt, float speed, float damping);
    Waves(const Waves& rhs) = delete;
    Waves& operator=(const Waves& rhs) = delete;
    ~Waves();


    int RowCount()const          {        return mNumRows;          }
    int ColumnCount()const    {        return mNumCols;            }
    int VertexCount()const       {        return mVertexCount;      } 
    int TriangleCount()const    {        return mTriangleCount;   }
    float Width()const               {        return mNumCols * mSpatialStep;    }
    float Depth()const               {        return mNumRows * mSpatialStep;  }

	// Returns the solution at the ith grid point.
    const DirectX::XMFLOAT3& Position(int i)const { return mCurrSolution[i]; }

	// Returns the solution normal at the ith grid point.
    const DirectX::XMFLOAT3& Normal(int i)const { return mNormals[i]; }

	// Returns the unit tangent vector at the ith grid point in the local x-axis direction.
    const DirectX::XMFLOAT3& TangentX(int i)const { return mTangentX[i]; }

	void Update(float dt);
	void Disturb(int i, int j, float magnitude);

private:
    ID3D12Device* mDevice = nullptr;
    ID3D12GraphicsCommandList* mCmdList = nullptr;

    int mNumRows = 0;
    int mNumCols = 0;

    int mVertexCount = 0;
    int mTriangleCount = 0;

    // Simulation constants we can precompute.
    float mK1 = 0.0f;
    float mK2 = 0.0f;
    float mK3 = 0.0f;

    float mTimeStep = 0.0f;
    float mSpatialStep = 0.0f;

    std::vector<DirectX::XMFLOAT3> mPrevSolution;
    std::vector<DirectX::XMFLOAT3> mCurrSolution;
    std::vector<DirectX::XMFLOAT3> mNormals;
    std::vector<DirectX::XMFLOAT3> mTangentX;

//Compute Shader 
public:
    UINT GetNumDescriptors();
    void BuildComputeRootSignature();
    void BuildWavesCSResource();
    void BuildDescriptorHeaps(
        D3D12_CPU_DESCRIPTOR_HANDLE hCpu,
        D3D12_GPU_DESCRIPTOR_HANDLE hGpu,
        UINT descSize);
    void BuildDescriptorHeaps();
    void MakePSOPipelineState(ComputePSO psoType);
    void BuildPSOs();
    void MakeWavesCS(const GameTimer& gt);
    D3D12_GPU_DESCRIPTOR_HANDLE DisplacementMap();

private:
    void SetComputeRoot(int* row = nullptr, int* col = nullptr, float* magnitude = nullptr);
    void DisturbCS(int row, int col, float magnitude);
    void DisturbCS(const GameTimer& gt);
    void WavesUpdateCS(const GameTimer& gt);

private:
    Microsoft::WRL::ComPtr<ID3DBlob> mWavesCS = nullptr;
    Microsoft::WRL::ComPtr<ID3DBlob> mDisturbCS = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> mComputeRootSig = nullptr;
    std::unordered_map<ComputePSO, Microsoft::WRL::ComPtr<ID3D12PipelineState>> mComputePSOs;

    Microsoft::WRL::ComPtr<ID3D12Resource> mPrevSolInput = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> mCurrSolInput = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> mOutput = nullptr;

    D3D12_GPU_DESCRIPTOR_HANDLE mGHPrevSolSrv{}, mGHCurrSolSrv{}, mGHOutputSrv{},
        mGHPrevSolUav{}, mGHCurrSolUav{}, mGHOutputUav{};
    
    Microsoft::WRL::ComPtr<ID3D12Resource> mUploadPrevSol = nullptr;
    
};

#endif // WAVES_H