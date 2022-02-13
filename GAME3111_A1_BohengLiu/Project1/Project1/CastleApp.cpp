/** @file Week4-6-ShapeComplete.cpp
 *  @brief Using a Root Descriptor instead of Descriptor Table
 *  For performance, there is a limit of 64 DWORDs that can be put in a root signature.
 *  The three types of root parameters have the following costs :
 *  1. Descriptor Table : 1 DWORD => the application is expected to bind a contiguous range of descriptors in a descriptor heap
 *  2. Root Descriptor : 2 DWORDs
 *  3. Root Constant : 1 DWORD per 32 - bit constant
 *  
 *  Unlike descriptor tables which require us to set a descriptor handle in a descriptor
 *  heap, to set a root descriptor, we simply bind the virtual address of the resource directly.
 *  
 *  There are 3 steps that need to be changed in three methods: 
 *  BuildRootSignature(), Draw(), DrawRenderItems() to convert from Descriptor Table to Root descriptor
 *
 *   Controls:
 *   Hold down '1' key to view scene in wireframe mode.
 *   Hold the left mouse button down and move the mouse to rotate.
 *   Hold the right mouse button down and move the mouse to zoom in and out.
 *
 *  @author Hooman Salamat
 */


// Modified from Week4-7-ShapeUsingRootDescriptor.cpp


#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"
#include "FrameResource.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

const int gNumFrameResources = 3;

// Lightweight structure stores parameters to draw a shape.  This will
// vary from app-to-app.
struct RenderItem
{
	RenderItem() = default;

	// World matrix of the shape that describes the object's local space
	// relative to the world space, which defines the position, orientation,
	// and scale of the object in the world.
	XMFLOAT4X4 World = MathHelper::Identity4x4();

	// Dirty flag indicating the object data has changed and we need to update the constant buffer.
	// Because we have an object cbuffer for each FrameResource, we have to apply the
	// update to each FrameResource.  Thus, when we modify obect data we should set 
	// NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
	int NumFramesDirty = gNumFrameResources;

	// Index into GPU constant buffer corresponding to the ObjectCB for this render item.
	UINT ObjCBIndex = -1;

	MeshGeometry* Geo = nullptr;

	// Primitive topology.
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	// DrawIndexedInstanced parameters.
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;
};

class CastleApp : public D3DApp
{
public:
	CastleApp(HINSTANCE hInstance);
	CastleApp(const CastleApp& rhs) = delete;
	CastleApp& operator=(const CastleApp& rhs) = delete;
	~CastleApp();

	virtual bool Initialize()override;

private:
	virtual void OnResize()override;
	virtual void Update(const GameTimer& gt)override;
	virtual void Draw(const GameTimer& gt)override;

	virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

	void OnKeyboardInput(const GameTimer& gt);
	void UpdateCamera(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);

	void BuildDescriptorHeaps();
	void BuildConstantBufferViews();
	void BuildRootSignature();
	void BuildShadersAndInputLayout();
	void BuildShapeGeometry();
	void BuildPSOs();
	void BuildFrameResources();
	void BuildRenderItems();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

private:

	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	FrameResource* mCurrFrameResource = nullptr;
	int mCurrFrameResourceIndex = 0;

	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	ComPtr<ID3D12DescriptorHeap> mCbvHeap = nullptr;

	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;

	// Render items divided by PSO.
	std::vector<RenderItem*> mOpaqueRitems;

	PassConstants mMainPassCB;

	UINT mPassCbvOffset = 0;

	bool mIsWireframe = false;

	XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();

	float mTheta = 1.5f * XM_PI;
	float mPhi = 0.2f * XM_PI;
	float mRadius = 15.0f;

	POINT mLastMousePos;
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
	PSTR cmdLine, int showCmd)
{
	// Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	try
	{
		CastleApp theApp(hInstance);
		if (!theApp.Initialize())
			return 0;

		return theApp.Run();
	}
	catch (DxException& e)
	{
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}
}

CastleApp::CastleApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
	mMainWndCaption = L"Donut Castle";
}

CastleApp::~CastleApp()
{
	if (md3dDevice != nullptr)
		FlushCommandQueue();
}

bool CastleApp::Initialize()
{
	if (!D3DApp::Initialize())
		return false;

	// Reset the command list to prep for initialization commands.
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	BuildRootSignature();
	BuildShadersAndInputLayout();
	BuildShapeGeometry();
	BuildRenderItems();
	BuildFrameResources();
	BuildDescriptorHeaps();
	BuildConstantBufferViews();
	BuildPSOs();

	// Execute the initialization commands.
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Wait until initialization is complete.
	FlushCommandQueue();

	return true;
}

void CastleApp::OnResize()
{
	D3DApp::OnResize();

	// The window resized, so update the aspect ratio and recompute the projection matrix.
	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	XMStoreFloat4x4(&mProj, P);
}

void CastleApp::Update(const GameTimer& gt)
{
	OnKeyboardInput(gt);
	UpdateCamera(gt);

	// Cycle through the circular frame resource array.
	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

	// Has the GPU finished processing the commands of the current frame resource?
	// If not, wait until the GPU has completed commands up to this fence point.
	if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	UpdateObjectCBs(gt);
	UpdateMainPassCB(gt);
}

void CastleApp::Draw(const GameTimer& gt)
{
	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

	// Reuse the memory associated with command recording.
	// We can only reset when the associated command lists have finished execution on the GPU.
	ThrowIfFailed(cmdListAlloc->Reset());

	// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
	// Reusing the command list reuses memory.
	if (mIsWireframe)
	{
		ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque_wireframe"].Get()));
	}
	else
	{
		ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));
	}

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	// Clear the back buffer and depth buffer.
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Specify the buffers we are going to render to.
	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	//step2: No more descriptor heap,  we simply bind the virtual address of the resource directly for each frame resource.

	//ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvHeap.Get() };
	//mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);


	// Bind per-pass constant buffer. We only need to do this once per - pass.

	//int passCbvIndex = mPassCbvOffset + mCurrFrameResourceIndex;
	//auto passCbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
	//passCbvHandle.Offset(passCbvIndex, mCbvSrvUavDescriptorSize);
	//mCommandList->SetGraphicsRootDescriptorTable(1, passCbvHandle);

	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());
	//end of step2


	DrawRenderItems(mCommandList.Get(), mOpaqueRitems);

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	// Done recording commands.
	ThrowIfFailed(mCommandList->Close());

	// Add the command list to the queue for execution.
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Swap the back and front buffers
	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	// Advance the fence value to mark commands up to this fence point.
	mCurrFrameResource->Fence = ++mCurrentFence;

	// Add an instruction to the command queue to set a new fence point. 
	// Because we are on the GPU timeline, the new fence point won't be 
	// set until the GPU finishes processing all the commands prior to this Signal().
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void CastleApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWnd);
}

void CastleApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void CastleApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		// Make each pixel correspond to a quarter of a degree.
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

		// Update angles based on input to orbit camera around box.
		mTheta += dx;
		mPhi += dy;

		// Restrict the angle mPhi.
		mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
	}
	else if ((btnState & MK_RBUTTON) != 0)
	{
		// Make each pixel correspond to 0.2 unit in the scene.
		float dx = 0.05f * static_cast<float>(x - mLastMousePos.x);
		float dy = 0.05f * static_cast<float>(y - mLastMousePos.y);

		// Update the camera radius based on input.
		mRadius += dx - dy;

		// Restrict the radius.
		mRadius = MathHelper::Clamp(mRadius, 5.0f, 150.0f);
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

void CastleApp::OnKeyboardInput(const GameTimer& gt)
{
	if (GetAsyncKeyState('1') & 0x8000)
		mIsWireframe = true;
	else
		mIsWireframe = false;
}

void CastleApp::UpdateCamera(const GameTimer& gt)
{
	// Convert Spherical to Cartesian coordinates.
	mEyePos.x = mRadius * sinf(mPhi) * cosf(mTheta);
	mEyePos.z = mRadius * sinf(mPhi) * sinf(mTheta);
	mEyePos.y = mRadius * cosf(mPhi);

	// Build the view matrix.
	XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&mView, view);
}

void CastleApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();
	for (auto& e : mAllRitems)
	{
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		if (e->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			// Next FrameResource need to be updated too.
			e->NumFramesDirty--;
		}
	}
}

void CastleApp::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = XMLoadFloat4x4(&mView);
	XMMATRIX proj = XMLoadFloat4x4(&mProj);

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	mMainPassCB.EyePosW = mEyePos;
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void CastleApp::BuildDescriptorHeaps()
{
	UINT objCount = (UINT)mOpaqueRitems.size();

	// Need a CBV descriptor for each object for each frame resource,
	// +1 for the perPass CBV for each frame resource.
	UINT numDescriptors = (objCount + 1) * gNumFrameResources;

	// Save an offset to the start of the pass CBVs.  These are the last 3 descriptors.
	mPassCbvOffset = objCount * gNumFrameResources;

	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
	cbvHeapDesc.NumDescriptors = numDescriptors;
	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	cbvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&cbvHeapDesc,
		IID_PPV_ARGS(&mCbvHeap)));
}

void CastleApp::BuildConstantBufferViews()
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

	UINT objCount = (UINT)mOpaqueRitems.size();

	// Need a CBV descriptor for each object for each frame resource.
	for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
	{
		auto objectCB = mFrameResources[frameIndex]->ObjectCB->Resource();
		for (UINT i = 0; i < objCount; ++i)
		{
			D3D12_GPU_VIRTUAL_ADDRESS cbAddress = objectCB->GetGPUVirtualAddress();

			// Offset to the ith object constant buffer in the buffer.
			cbAddress += i * objCBByteSize;

			// Offset to the object cbv in the descriptor heap.
			int heapIndex = frameIndex * objCount + i;
			auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
			handle.Offset(heapIndex, mCbvSrvUavDescriptorSize);

			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
			cbvDesc.BufferLocation = cbAddress;
			cbvDesc.SizeInBytes = objCBByteSize;

			md3dDevice->CreateConstantBufferView(&cbvDesc, handle);
		}
	}

	UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

	// Last three descriptors are the pass CBVs for each frame resource.
	for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
	{
		auto passCB = mFrameResources[frameIndex]->PassCB->Resource();
		D3D12_GPU_VIRTUAL_ADDRESS cbAddress = passCB->GetGPUVirtualAddress();

		// Offset to the pass cbv in the descriptor heap.
		int heapIndex = mPassCbvOffset + frameIndex;
		auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
		handle.Offset(heapIndex, mCbvSrvUavDescriptorSize);

		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
		cbvDesc.BufferLocation = cbAddress;
		cbvDesc.SizeInBytes = passCBByteSize;

		md3dDevice->CreateConstantBufferView(&cbvDesc, handle);
	}
}

void CastleApp::BuildRootSignature()
{
	CD3DX12_ROOT_PARAMETER slotRootParameter[2];

	//! step1: Replace Descriptor Table with Root Descriptor
	//! CD3DX12_DESCRIPTOR_RANGE cbvTable0;
	//! cbvTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
	//! CD3DX12_DESCRIPTOR_RANGE cbvTable1;
	//! cbvTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);

	//! Root parameter can be a table, root descriptor or root constants.
	//! Create root CBVs.
	//! slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable0);
	//! slotRootParameter[1].InitAsDescriptorTable(1, &cbvTable1);
	slotRootParameter[0].InitAsConstantBufferView(0); // per - object CBV
	slotRootParameter[1].InitAsConstantBufferView(1); // per - pass CBV

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, slotRootParameter, 0, nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void CastleApp::BuildShadersAndInputLayout()
{
	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\VS.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\PS.hlsl", nullptr, "PS", "ps_5_1");

	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}


void CastleApp::BuildShapeGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 0);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.7f, 0.3f, 3.0f, 20, 20);

	// new 3D shapes
	GeometryGenerator::MeshData cone = geoGen.CreateCone(0.5f, 1.5f, 20, 20);
	GeometryGenerator::MeshData pyramid1 = geoGen.CreatePyramid1(1.5f, 1.5f, 0);
	GeometryGenerator::MeshData pyramid2 = geoGen.CreatePyramid2(1.5f, 0.5f, 1.0f, 0);
	GeometryGenerator::MeshData squarePyramid = geoGen.CreateSquarePyramid(1.5f, 1.0f, 0);
	GeometryGenerator::MeshData triangularPrism = geoGen.CreateTriangularPrism(1.0f, 0.5f, 0);
	GeometryGenerator::MeshData donut = geoGen.CreateTorus(2.0f, 1.0f, 20, 20);

	// We are concatenating all the geometry into one big vertex/index buffer.  So
	// define the regions in the buffer each submesh covers.

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = (UINT)box.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();
	UINT coneVertexOffset = cylinderVertexOffset + (UINT)cylinder.Vertices.size();
	UINT pyramid1VertexOffset = coneVertexOffset + (UINT)cone.Vertices.size();
	UINT pyramid2VertexOffset = pyramid1VertexOffset + (UINT)pyramid1.Vertices.size();
	UINT squarePyramidVertexOffset = pyramid2VertexOffset + (UINT)pyramid2.Vertices.size();
	UINT triangularPrismVertexOffset = squarePyramidVertexOffset + (UINT)squarePyramid.Vertices.size();
	UINT donutVertexOffset = triangularPrismVertexOffset + (UINT)triangularPrism.Vertices.size();

	// Cache the starting index for each object in the concatenated index buffer.
	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = (UINT)box.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();
	UINT coneIndexOffset = cylinderIndexOffset + (UINT)cylinder.Indices32.size();
	UINT pyramid1IndexOffset = coneIndexOffset + (UINT)cone.Indices32.size();
	UINT pyramid2IndexOffset = pyramid1IndexOffset + (UINT)pyramid1.Indices32.size();
	UINT squarePyramidIndexOffset = pyramid2IndexOffset + (UINT)pyramid2.Indices32.size();
	UINT triangularPrismIndexOffset = squarePyramidIndexOffset + (UINT)squarePyramid.Indices32.size();
	UINT donutIndexOffset = triangularPrismIndexOffset + (UINT)triangularPrism.Indices32.size();

	// Define the SubmeshGeometry that cover different
	// regions of the vertex/index buffers.

	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = boxVertexOffset;

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = gridVertexOffset;

	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

	SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

	SubmeshGeometry coneSubmesh;
	coneSubmesh.IndexCount = (UINT)cone.Indices32.size();
	coneSubmesh.StartIndexLocation = coneIndexOffset;
	coneSubmesh.BaseVertexLocation = coneVertexOffset;

	SubmeshGeometry pyramid1Submesh;
	pyramid1Submesh.IndexCount = (UINT)pyramid1.Indices32.size();
	pyramid1Submesh.StartIndexLocation = pyramid1IndexOffset;
	pyramid1Submesh.BaseVertexLocation = pyramid1VertexOffset;

	SubmeshGeometry pyramid2Submesh;
	pyramid2Submesh.IndexCount = (UINT)pyramid2.Indices32.size();
	pyramid2Submesh.StartIndexLocation = pyramid2IndexOffset;
	pyramid2Submesh.BaseVertexLocation = pyramid2VertexOffset;

	SubmeshGeometry squarePyramidSubmesh;
	squarePyramidSubmesh.IndexCount = (UINT)squarePyramid.Indices32.size();
	squarePyramidSubmesh.StartIndexLocation = squarePyramidIndexOffset;
	squarePyramidSubmesh.BaseVertexLocation = squarePyramidVertexOffset;

	SubmeshGeometry triangularPrismSubmesh;
	triangularPrismSubmesh.IndexCount = (UINT)triangularPrism.Indices32.size();
	triangularPrismSubmesh.StartIndexLocation = triangularPrismIndexOffset;
	triangularPrismSubmesh.BaseVertexLocation = triangularPrismVertexOffset;

	SubmeshGeometry donutSubmesh;
	donutSubmesh.IndexCount = (UINT)donut.Indices32.size();
	donutSubmesh.StartIndexLocation = donutIndexOffset;
	donutSubmesh.BaseVertexLocation = donutVertexOffset;


	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.

	auto totalVertexCount =
		box.Vertices.size() +
		grid.Vertices.size() +
		sphere.Vertices.size() +
		cylinder.Vertices.size() +
		cone.Vertices.size() +
		pyramid1.Vertices.size() +
		pyramid2.Vertices.size() +
		squarePyramid.Vertices.size() +
		triangularPrism.Vertices.size() +
		donut.Vertices.size();

	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;

	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::DarkGray);
	}

	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::ForestGreen);
	}

	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::DarkGoldenrod);
	}

	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::DarkSlateGray);
	}

	for (size_t i = 0; i < cone.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cone.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Silver);
	}

	for (size_t i = 0; i < pyramid1.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = pyramid1.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::LightGoldenrodYellow);
	}

	for (size_t i = 0; i < pyramid2.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = pyramid2.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::SaddleBrown);
	}

	for (size_t i = 0; i < squarePyramid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = squarePyramid.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::DimGray);
	}

	for (size_t i = 0; i < triangularPrism.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = triangularPrism.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::WhiteSmoke);
	}

	for (size_t i = 0; i < donut.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = donut.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Goldenrod);
	}

	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));

	indices.insert(indices.end(), std::begin(cone.GetIndices16()), std::end(cone.GetIndices16()));
	indices.insert(indices.end(), std::begin(pyramid1.GetIndices16()), std::end(pyramid1.GetIndices16()));
	indices.insert(indices.end(), std::begin(pyramid2.GetIndices16()), std::end(pyramid2.GetIndices16()));
	indices.insert(indices.end(), std::begin(squarePyramid.GetIndices16()), std::end(squarePyramid.GetIndices16()));
	indices.insert(indices.end(), std::begin(triangularPrism.GetIndices16()), std::end(triangularPrism.GetIndices16()));
	indices.insert(indices.end(), std::begin(donut.GetIndices16()), std::end(donut.GetIndices16()));

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


	geo->DrawArgs["box"] = boxSubmesh;
	geo->DrawArgs["grid"] = gridSubmesh;
	geo->DrawArgs["sphere"] = sphereSubmesh;
	geo->DrawArgs["cylinder"] = cylinderSubmesh;


	geo->DrawArgs["cone"] = coneSubmesh;
	geo->DrawArgs["pyramid1"] = pyramid1Submesh;
	geo->DrawArgs["pyramid2"] = pyramid2Submesh;
	geo->DrawArgs["squarePyramid"] = squarePyramidSubmesh;
	geo->DrawArgs["triangularPrism"] = triangularPrismSubmesh;
	geo->DrawArgs["donut"] = donutSubmesh;


	mGeometries[geo->Name] = std::move(geo);
}

void CastleApp::BuildPSOs()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	// PSO for opaque objects.

	ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	opaquePsoDesc.pRootSignature = mRootSignature.Get();

	opaquePsoDesc.VS =
	{
	 reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
	 mShaders["standardVS"]->GetBufferSize()
	};

	opaquePsoDesc.PS =
	{
	 reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
	 mShaders["opaquePS"]->GetBufferSize()
	};

	opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaquePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePsoDesc.SampleMask = UINT_MAX;
	opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePsoDesc.NumRenderTargets = 1;
	opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
	opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	opaquePsoDesc.DSVFormat = mDepthStencilFormat;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));

	// PSO for opaque wireframe objects.

	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaqueWireframePsoDesc = opaquePsoDesc;
	opaqueWireframePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaqueWireframePsoDesc, IID_PPV_ARGS(&mPSOs["opaque_wireframe"])));
}



void CastleApp::BuildFrameResources()
{
	for (int i = 0; i < gNumFrameResources; ++i)
	{
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
			1, (UINT)mAllRitems.size()));
	}
}


void CastleApp::BuildRenderItems()
{
	UINT objIndex = 0;
	auto gridRitem = std::make_unique<RenderItem>();

	gridRitem->World = MathHelper::Identity4x4();
	gridRitem->ObjCBIndex = objIndex++;
	gridRitem->Geo = mGeometries["shapeGeo"].get();
	gridRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
	gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
	mAllRitems.push_back(std::move(gridRitem));

	float dx[4] = { 7.0f, 7.0f, -7.0f, -7.0f }, dz[4] = { 7.0f, -7.0f, -7.0f, 7.0f };

	for (int i = 0; i < 4; ++ i)
	{
		auto towerRitem1 = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&towerRitem1->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(dx[i], 0.5f, dz[i]));
		towerRitem1->ObjCBIndex = objIndex++;
		towerRitem1->Geo = mGeometries["shapeGeo"].get();
		towerRitem1->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		towerRitem1->IndexCount = towerRitem1->Geo->DrawArgs["cylinder"].IndexCount;
		towerRitem1->StartIndexLocation = towerRitem1->Geo->DrawArgs["cylinder"].StartIndexLocation;
		towerRitem1->BaseVertexLocation = towerRitem1->Geo->DrawArgs["cylinder"].BaseVertexLocation;
		mAllRitems.push_back(std::move(towerRitem1));

		auto towerRitem2 = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&towerRitem2->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(dx[i], 3.7f, dz[i]));
		towerRitem2->ObjCBIndex = objIndex++;
		towerRitem2->Geo = mGeometries["shapeGeo"].get();
		towerRitem2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		towerRitem2->IndexCount = towerRitem2->Geo->DrawArgs["pyramid2"].IndexCount;
		towerRitem2->StartIndexLocation = towerRitem2->Geo->DrawArgs["pyramid2"].StartIndexLocation;
		towerRitem2->BaseVertexLocation = towerRitem2->Geo->DrawArgs["pyramid2"].BaseVertexLocation;
		mAllRitems.push_back(std::move(towerRitem2));

		auto towerRitem3 = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&towerRitem3->World, XMMatrixTranslation(dx[i], 4.5f, dz[i]));
		towerRitem3->ObjCBIndex = objIndex++;
		towerRitem3->Geo = mGeometries["shapeGeo"].get();
		towerRitem3->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		towerRitem3->IndexCount = towerRitem3->Geo->DrawArgs["sphere"].IndexCount;
		towerRitem3->StartIndexLocation = towerRitem3->Geo->DrawArgs["sphere"].StartIndexLocation;
		towerRitem3->BaseVertexLocation = towerRitem3->Geo->DrawArgs["sphere"].BaseVertexLocation;
		mAllRitems.push_back(std::move(towerRitem3));
	}

	auto wallRitem1 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wallRitem1->World, XMMatrixScaling(13.0f, 3.0f, 1.5f) * XMMatrixTranslation(0.0, 1.0f, 7.0));
	wallRitem1->ObjCBIndex = objIndex++;
	wallRitem1->Geo = mGeometries["shapeGeo"].get();
	wallRitem1->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallRitem1->IndexCount = wallRitem1->Geo->DrawArgs["box"].IndexCount;
	wallRitem1->StartIndexLocation = wallRitem1->Geo->DrawArgs["box"].StartIndexLocation;
	wallRitem1->BaseVertexLocation = wallRitem1->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(wallRitem1));

	auto wallRitem2 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wallRitem2->World, XMMatrixScaling(13.0f, 3.0f, 1.5f) * XMMatrixTranslation(0.0, 1.0f, -7.0));
	wallRitem2->ObjCBIndex = objIndex++;
	wallRitem2->Geo = mGeometries["shapeGeo"].get();
	wallRitem2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallRitem2->IndexCount = wallRitem2->Geo->DrawArgs["box"].IndexCount;
	wallRitem2->StartIndexLocation = wallRitem2->Geo->DrawArgs["box"].StartIndexLocation;
	wallRitem2->BaseVertexLocation = wallRitem2->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(wallRitem2));

	auto wallRitem3 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wallRitem3->World, XMMatrixScaling(13.0f, 3.0f, 1.5f) * XMMatrixRotationY(XM_PIDIV2) * XMMatrixTranslation(7.0, 1.0f, 0.0));
	wallRitem3->ObjCBIndex = objIndex++;
	wallRitem3->Geo = mGeometries["shapeGeo"].get();
	wallRitem3->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallRitem3->IndexCount = wallRitem3->Geo->DrawArgs["box"].IndexCount;
	wallRitem3->StartIndexLocation = wallRitem3->Geo->DrawArgs["box"].StartIndexLocation;
	wallRitem3->BaseVertexLocation = wallRitem3->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(wallRitem3));

	auto wallRitem4 = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&wallRitem4->World, XMMatrixScaling(13.0f, 3.0f, 1.5f) * XMMatrixRotationY(XM_PIDIV2) * XMMatrixTranslation(-7.0, 1.0f, 0.0));
	wallRitem4->ObjCBIndex = objIndex++;
	wallRitem4->Geo = mGeometries["shapeGeo"].get();
	wallRitem4->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallRitem4->IndexCount = wallRitem4->Geo->DrawArgs["box"].IndexCount;
	wallRitem4->StartIndexLocation = wallRitem4->Geo->DrawArgs["box"].StartIndexLocation;
	wallRitem4->BaseVertexLocation = wallRitem4->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(wallRitem4));

	float ofset[] = { -4.5f, -2.5f, 2.5f, 4.5f };

	for (int i = 0; i < 4; ++i)
	{
		auto wallObjRitem1 = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&wallObjRitem1->World, XMMatrixTranslation(7.0, 3.0f, ofset[i]));
		wallObjRitem1->ObjCBIndex = objIndex++;
		wallObjRitem1->Geo = mGeometries["shapeGeo"].get();
		wallObjRitem1->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		wallObjRitem1->IndexCount = wallObjRitem1->Geo->DrawArgs["squarePyramid"].IndexCount;
		wallObjRitem1->StartIndexLocation = wallObjRitem1->Geo->DrawArgs["squarePyramid"].StartIndexLocation;
		wallObjRitem1->BaseVertexLocation = wallObjRitem1->Geo->DrawArgs["squarePyramid"].BaseVertexLocation;
		mAllRitems.push_back(std::move(wallObjRitem1));

		auto wallObjRitem2 = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&wallObjRitem2->World, XMMatrixTranslation(-7.0, 3.0f, ofset[i]));
		wallObjRitem2->ObjCBIndex = objIndex++;
		wallObjRitem2->Geo = mGeometries["shapeGeo"].get();
		wallObjRitem2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		wallObjRitem2->IndexCount = wallObjRitem2->Geo->DrawArgs["squarePyramid"].IndexCount;
		wallObjRitem2->StartIndexLocation = wallObjRitem2->Geo->DrawArgs["squarePyramid"].StartIndexLocation;
		wallObjRitem2->BaseVertexLocation = wallObjRitem2->Geo->DrawArgs["squarePyramid"].BaseVertexLocation;
		mAllRitems.push_back(std::move(wallObjRitem2));

		auto wallObjRitem3 = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&wallObjRitem3->World, XMMatrixTranslation(ofset[i], 3.0f, 7.0f));
		wallObjRitem3->ObjCBIndex = objIndex++;
		wallObjRitem3->Geo = mGeometries["shapeGeo"].get();
		wallObjRitem3->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		wallObjRitem3->IndexCount = wallObjRitem3->Geo->DrawArgs["cone"].IndexCount;
		wallObjRitem3->StartIndexLocation = wallObjRitem3->Geo->DrawArgs["cone"].StartIndexLocation;
		wallObjRitem3->BaseVertexLocation = wallObjRitem3->Geo->DrawArgs["cone"].BaseVertexLocation;
		mAllRitems.push_back(std::move(wallObjRitem3));

		auto wallObjRitem4 = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&wallObjRitem4->World, XMMatrixTranslation(ofset[i], 3.0f, -7.0f));
		wallObjRitem4->ObjCBIndex = objIndex++;
		wallObjRitem4->Geo = mGeometries["shapeGeo"].get();
		wallObjRitem4->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		wallObjRitem4->IndexCount = wallObjRitem4->Geo->DrawArgs["pyramid1"].IndexCount;
		wallObjRitem4->StartIndexLocation = wallObjRitem4->Geo->DrawArgs["pyramid1"].StartIndexLocation;
		wallObjRitem4->BaseVertexLocation = wallObjRitem4->Geo->DrawArgs["pyramid1"].BaseVertexLocation;
		mAllRitems.push_back(std::move(wallObjRitem4));
	}

	auto baseRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&baseRitem->World, XMMatrixScaling(7.5f, 1.0f, 7.5f));
	baseRitem->ObjCBIndex = objIndex++;
	baseRitem->Geo = mGeometries["shapeGeo"].get();
	baseRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	baseRitem->IndexCount = baseRitem->Geo->DrawArgs["triangularPrism"].IndexCount;
	baseRitem->StartIndexLocation = baseRitem->Geo->DrawArgs["triangularPrism"].StartIndexLocation;
	baseRitem->BaseVertexLocation = baseRitem->Geo->DrawArgs["triangularPrism"].BaseVertexLocation;
	mAllRitems.push_back(std::move(baseRitem));

	auto donutRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&donutRitem->World, XMMatrixScaling(0.7f, 0.7f, 0.7f) * XMMatrixRotationX(XM_PIDIV2 * 1.3f) * XMMatrixTranslation(0.0, 2.0f, 0.0));
	donutRitem->ObjCBIndex = objIndex++;
	donutRitem->Geo = mGeometries["shapeGeo"].get();
	donutRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	donutRitem->IndexCount = donutRitem->Geo->DrawArgs["donut"].IndexCount;
	donutRitem->StartIndexLocation = donutRitem->Geo->DrawArgs["donut"].StartIndexLocation;
	donutRitem->BaseVertexLocation = donutRitem->Geo->DrawArgs["donut"].BaseVertexLocation;
	mAllRitems.push_back(std::move(donutRitem));
	

	// All the render items are opaque.
	for (auto& e : mAllRitems)
		mOpaqueRitems.push_back(e.get());
}

void CastleApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	// For each render item...

	for (size_t i = 0; i < ritems.size(); ++i)
	{
		auto ri = ritems[i];
		cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		//! step3:no more heap, we simple bind the virtual address of the resource (constant buffer) 
		//! directly for each render item.

		//! Offset to the CBV in the descriptor heap for this object and for this frame resource.
		//! UINT cbvIndex = mCurrFrameResourceIndex * (UINT)mOpaqueRitems.size() + ri->ObjCBIndex;
		//! auto cbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
		//! cbvHandle.Offset(cbvIndex, mCbvSrvUavDescriptorSize);
		//!cmdList->SetGraphicsRootDescriptorTable(0, cbvHandle);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress();
		objCBAddress += ri->ObjCBIndex * objCBByteSize;
		cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);

		//!step3

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

