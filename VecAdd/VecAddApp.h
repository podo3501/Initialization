#ifdef _DEBUG
#pragma comment(lib, "../Lib/Common_d.lib")
#else
#pragma comment(lib, "../Lib/Common.lib")
#endif

#include "../Common/d3dApp.h"
#include "../Common/MathHelper.h"
#include <map>

struct FrameResource;

class VecAddApp : public D3DApp
{
public:
	VecAddApp(HINSTANCE hInstance);
	VecAddApp(const VecAddApp& rhs) = delete;
	VecAddApp& operator=(const VecAddApp& rhs) = delete;
	~VecAddApp();

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

	void BuildRootSignature();
	void BuildVecAddResource();
	void BuildVecLengthResource();
	void BuildPSOs();
	void ExcuteComputeShader();

private:
	Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> mVecAddCS = nullptr;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> mComputePSO = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> mInputBufferA = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> mInputUploadBufferA = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> mInputBufferB = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> mInputUploadBufferB = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> mOutputBuffer = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> mReadBackBuffer = nullptr;

	//Vector Length
	Microsoft::WRL::ComPtr<ID3DBlob> mVecLengthCS = nullptr;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> mComputeVecLengthPSO = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> mInputVLBuffer = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> mInputUploadVL = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> mOutputVLBuffer = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> mReadBackVLBuffer = nullptr;

	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> mCmdListAlloc = nullptr;

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
