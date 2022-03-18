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
#include "Waves.h"

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

    XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

    // Dirty flag indicating the object data has changed and we need to update the constant buffer.
    // Because we have an object cbuffer for each FrameResource, we have to apply the
    // update to each FrameResource.  Thus, when we modify obect data we should set 
    // NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
    int NumFramesDirty = gNumFrameResources;

    // Index into GPU constant buffer corresponding to the ObjectCB for this render item.
    UINT ObjCBIndex = -1;

    Material* Mat = nullptr;
    MeshGeometry* Geo = nullptr;

    // Primitive topology.
    D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    // DrawIndexedInstanced parameters.
    UINT IndexCount = 0;
    UINT StartIndexLocation = 0;
    int BaseVertexLocation = 0;
};

enum class RenderLayer : int
{
    Opaque = 0,
    Transparent,
    AlphaTested,
    AlphaTestedTreeSprites,
    Count
};

class CastleApp : public D3DApp
{
public:
    CastleApp(HINSTANCE hInstance);
    CastleApp(const CastleApp& rhs) = delete;
    CastleApp& operator=(const CastleApp& rhs) = delete;
    ~CastleApp();

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
    void UpdateObjectCBs(const GameTimer& gt);
    void UpdateMainPassCB(const GameTimer& gt);
    void UpdateMaterialCBs(const GameTimer& gt);
    
    void UpdateWater(const GameTimer& gt);

    void BuildDescriptorHeaps();
    //void BuildConstantBufferViews();
    void BuildRootSignature();
    void BuildShadersAndInputLayout();

    void BuildCastleGeometry();
    void BuildWaterGeometry();
    void BuildTreeSpritesGeometry();

    void BuildPSOs();
    void BuildFrameResources();
    void BuildRenderItems();
    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

    void LoadTextures();
    void BuildMaterials();
    std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

private:
    std::vector<std::unique_ptr<FrameResource>> mFrameResources;
    FrameResource* mCurrFrameResource = nullptr;
    int mCurrFrameResourceIndex = 0;

    UINT mCbvSrvDescriptorSize = 0;

    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
    ComPtr<ID3D12DescriptorHeap> mCbvHeap = nullptr;

    ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

    std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
    std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
    std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
    std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
    std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
    std::vector<D3D12_INPUT_ELEMENT_DESC> mTreeSpriteInputLayout;

    // List of all the render items.
    std::vector<std::unique_ptr<RenderItem>> mAllRitems;

    // Render items divided by PSO.
    std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];

    std::unique_ptr<Waves> mWaves;
    RenderItem* mWavesRitem = nullptr;

    PassConstants mMainPassCB;

    UINT mPassCbvOffset = 0;

    bool mIsWireframe = false;

    XMFLOAT3 mEyePos = {0.0f, 0.0f, 0.0f};
    XMFLOAT4X4 mView = MathHelper::Identity4x4();
    XMFLOAT4X4 mProj = MathHelper::Identity4x4();

    float mTheta = 1.5f * XM_PI;
    float mPhi = XM_PIDIV4 - 0.1f;
    float mRadius = 30.0f;

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

    // Get the increment size of a descriptor in this heap type.  This is hardware specific, 
    // so we have to query this information.
    mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    mWaves = std::make_unique<Waves>(128, 128, 1.0f, 0.03f, 4.0f, 0.2f);

    LoadTextures();
    BuildMaterials();
    BuildRootSignature();
    BuildDescriptorHeaps();
    BuildShadersAndInputLayout();

    BuildCastleGeometry();
    BuildWaterGeometry();
    BuildTreeSpritesGeometry();

    BuildRenderItems();
    BuildFrameResources();
    BuildPSOs();

    // Execute the initialization commands.
    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = {mCommandList.Get()};
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

    UpdateWater(gt);
    UpdateObjectCBs(gt);
    UpdateMaterialCBs(gt);
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
    ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    // Indicate a state transition on the resource usage.
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
                                                                           D3D12_RESOURCE_STATE_PRESENT,
                                                                           D3D12_RESOURCE_STATE_RENDER_TARGET));

    // Clear the back buffer and depth buffer.
    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0,
                                        0, nullptr);

    // Specify the buffers we are going to render to.
    mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

    ID3D12DescriptorHeap* descriptorHeaps[] = {mSrvDescriptorHeap.Get()};
    mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

    auto passCB = mCurrFrameResource->PassCB->Resource();
    mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

    DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

    mCommandList->SetPipelineState(mPSOs["treeSprites"].Get());
    DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::AlphaTestedTreeSprites]);
    
    mCommandList->SetPipelineState(mPSOs["transparent"].Get());
    DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Transparent]);

    // Indicate a state transition on the resource usage.
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
                                                                           D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                                           D3D12_RESOURCE_STATE_PRESENT));

    // Done recording commands.
    ThrowIfFailed(mCommandList->Close());

    // Add the command list to the queue for execution.
    ID3D12CommandList* cmdsLists[] = {mCommandList.Get()};
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
    mMainPassCB.AmbientLight = {0.25f, 0.25f, 0.35f, 1.0f};
    mMainPassCB.Lights[0].Direction = {0.57735f, -0.57735f, 0.57735f};
    mMainPassCB.Lights[0].Strength = {0.5f, 0.5f, 0.5f};
    mMainPassCB.Lights[1].Direction = {-0.57735f, -0.57735f, 0.57735f};
    mMainPassCB.Lights[1].Strength = {0.3f, 0.3f, 0.3f};
    mMainPassCB.Lights[2].Direction = {0.0f, -0.707f, -0.707f};
    mMainPassCB.Lights[2].Strength = {0.15f, 0.15f, 0.15f};

    float dx[4] = {7.0f, 7.0f, -7.0f, -7.0f}, dz[4] = {7.0f, -7.0f, 7.0f, -7.0f};
    for (int i = 0, k = 3; i < 4; ++i, ++k)
    {
        mMainPassCB.Lights[k].Position = {dx[i], 5.5f, dz[i]};
        mMainPassCB.Lights[k].Strength = {0.1f, 0.1f, 3.8f};
        mMainPassCB.Lights[k].FalloffStart = 1.0f;
        mMainPassCB.Lights[k].FalloffEnd = 5.0f;
    }

    auto currPassCB = mCurrFrameResource->PassCB.get();
    currPassCB->CopyData(0, mMainPassCB);
}

void CastleApp::UpdateMaterialCBs(const GameTimer& gt)
{
    auto currMaterialCB = mCurrFrameResource->MaterialCB.get();
    for (auto& e : mMaterials)
    {
        // Only update the cbuffer data if the constants have changed.  If the cbuffer
        // data changes, it needs to be updated for each FrameResource.
        Material* mat = e.second.get();
        if (mat->NumFramesDirty > 0)
        {
            XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

            MaterialConstants matConstants;
            matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
            matConstants.FresnelR0 = mat->FresnelR0;
            matConstants.Roughness = mat->Roughness;
            XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));

            currMaterialCB->CopyData(mat->MatCBIndex, matConstants);

            // Next FrameResource need to be updated too.
            mat->NumFramesDirty--;
        }
    }
}

void CastleApp::UpdateWater(const GameTimer& gt)
{
    // Scroll the water material texture coordinates.
    auto waterMat = mMaterials["water"].get();

    float& tu = waterMat->MatTransform(3, 0);
    float& tv = waterMat->MatTransform(3, 1);

    tu += 0.1f * gt.DeltaTime();
    tv += 0.02f * gt.DeltaTime();

    if(tu >= 1.0f)
        tu -= 1.0f;

    if(tv >= 1.0f)
        tv -= 1.0f;

    waterMat->MatTransform(3, 0) = tu;
    waterMat->MatTransform(3, 1) = tv;

    // Material has changed, so need to update cbuffer.
    waterMat->NumFramesDirty = gNumFrameResources;

    // Every quarter second, generate a random wave.
    // static float t_base = 0.0f;
    // if((mTimer.TotalTime() - t_base) >= 0.25f)
    // {
    //     t_base += 0.25f;
    //
    //     int i = MathHelper::Rand(4, mWaves->RowCount() - 5);
    //     int j = MathHelper::Rand(4, mWaves->ColumnCount() - 5);
    //
    //     float r = MathHelper::RandF(0.2f, 0.5f);
    //
    //     mWaves->Disturb(i, j, r);
    // }

    // Update the wave simulation.
    mWaves->Update(gt.DeltaTime());

    // Update the wave vertex buffer with the new solution.
    auto currWavesVB = mCurrFrameResource->WavesVB.get();
    for(int i = 0; i < mWaves->VertexCount(); ++i)
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

void CastleApp::BuildDescriptorHeaps()
{
    //
    // Create the SRV heap.
    //
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 9;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

    //
    // Fill out the heap with actual descriptors.
    //
    CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    auto grassTex = mTextures["grassTex"]->Resource;
    auto brickTex1 = mTextures["brickTex1"]->Resource;
    auto brickTex2 = mTextures["brickTex2"]->Resource;
    auto brickTex3 = mTextures["brickTex3"]->Resource;
    auto iceTex = mTextures["iceTex"]->Resource;
    auto checkboardTex = mTextures["checkboardTex"]->Resource;
    auto waterTex = mTextures["waterTex"]->Resource;
    auto woodTex = mTextures["woodTex"]->Resource;
    auto treeTex = mTextures["treeTex"]->Resource;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = grassTex->GetDesc().Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = grassTex->GetDesc().MipLevels;
    md3dDevice->CreateShaderResourceView(grassTex.Get(), &srvDesc, hDescriptor);

    // next descriptor
    hDescriptor.Offset(1, mCbvSrvDescriptorSize);
    srvDesc.Format = brickTex1->GetDesc().Format;
    srvDesc.Texture2D.MipLevels = brickTex1->GetDesc().MipLevels;
    md3dDevice->CreateShaderResourceView(brickTex1.Get(), &srvDesc, hDescriptor);

    // next descriptor
    hDescriptor.Offset(1, mCbvSrvDescriptorSize);
    srvDesc.Format = brickTex2->GetDesc().Format;
    srvDesc.Texture2D.MipLevels = brickTex2->GetDesc().MipLevels;
    md3dDevice->CreateShaderResourceView(brickTex2.Get(), &srvDesc, hDescriptor);

    // next descriptor
    hDescriptor.Offset(1, mCbvSrvDescriptorSize);
    srvDesc.Format = brickTex3->GetDesc().Format;
    srvDesc.Texture2D.MipLevels = brickTex3->GetDesc().MipLevels;
    md3dDevice->CreateShaderResourceView(brickTex3.Get(), &srvDesc, hDescriptor);

    // next descriptor
    hDescriptor.Offset(1, mCbvSrvDescriptorSize);
    srvDesc.Format = iceTex->GetDesc().Format;
    srvDesc.Texture2D.MipLevels = iceTex->GetDesc().MipLevels;
    md3dDevice->CreateShaderResourceView(iceTex.Get(), &srvDesc, hDescriptor);

    // next descriptor
    hDescriptor.Offset(1, mCbvSrvDescriptorSize);
    srvDesc.Format = checkboardTex->GetDesc().Format;
    srvDesc.Texture2D.MipLevels = checkboardTex->GetDesc().MipLevels;
    md3dDevice->CreateShaderResourceView(checkboardTex.Get(), &srvDesc, hDescriptor);

    // next descriptor
    hDescriptor.Offset(1, mCbvSrvDescriptorSize);
    srvDesc.Format = waterTex->GetDesc().Format;
    srvDesc.Texture2D.MipLevels = waterTex->GetDesc().MipLevels;
    md3dDevice->CreateShaderResourceView(waterTex.Get(), &srvDesc, hDescriptor);

    // next descriptor
    hDescriptor.Offset(1, mCbvSrvDescriptorSize);
    srvDesc.Format = woodTex->GetDesc().Format;
    srvDesc.Texture2D.MipLevels = woodTex->GetDesc().MipLevels;
    md3dDevice->CreateShaderResourceView(woodTex.Get(), &srvDesc, hDescriptor);

    // next descriptor
    hDescriptor.Offset(1, mCbvSrvDescriptorSize);
    auto desc = treeTex->GetDesc();
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    srvDesc.Format = treeTex->GetDesc().Format;
    srvDesc.Texture2DArray.MostDetailedMip = 0;
    srvDesc.Texture2DArray.MipLevels = -1;
    srvDesc.Texture2DArray.FirstArraySlice = 0;
    srvDesc.Texture2DArray.ArraySize = treeTex->GetDesc().DepthOrArraySize;
    md3dDevice->CreateShaderResourceView(treeTex.Get(), &srvDesc, hDescriptor);
}

void CastleApp::BuildRootSignature()
{
    CD3DX12_DESCRIPTOR_RANGE texTable;
    texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

    // Root parameter can be a table, root descriptor or root constants.
    CD3DX12_ROOT_PARAMETER slotRootParameter[4];

    // Perfomance TIP: Order from most frequent to least frequent.
    slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);
    slotRootParameter[1].InitAsConstantBufferView(0);
    slotRootParameter[2].InitAsConstantBufferView(1);
    slotRootParameter[3].InitAsConstantBufferView(2);

    auto staticSamplers = GetStaticSamplers();

    // A root signature is an array of root parameters.
    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter,
                                            (UINT)staticSamplers.size(), staticSamplers.data(),
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
    const D3D_SHADER_MACRO defines[] =
    {
        "NUM_POINT_LIGHTS", "4",
        NULL, NULL
    };

    const D3D_SHADER_MACRO alphaTestDefines[] =
    {
        "ALPHA_TEST", "1",
        NULL, NULL
    };

    mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_1");
    mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", defines, "PS", "ps_5_1");
    mShaders["alphaTestedPS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", alphaTestDefines, "PS", "ps_5_1");

    mShaders["treeSpriteVS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", nullptr, "VS", "vs_5_1");
    mShaders["treeSpriteGS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", defines, "GS", "gs_5_1");
    mShaders["treeSpritePS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", alphaTestDefines, "PS", "ps_5_1");

    mInputLayout =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    mTreeSpriteInputLayout =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"SIZE", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
}


void CastleApp::BuildCastleGeometry()
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
        vertices[k].Normal = box.Vertices[i].Normal;
        vertices[k].TexC = box.Vertices[i].TexC;
    }

    for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = grid.Vertices[i].Position;
        vertices[k].Normal = grid.Vertices[i].Normal;
        vertices[k].TexC = grid.Vertices[i].TexC;
    }

    for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = sphere.Vertices[i].Position;
        vertices[k].Normal = sphere.Vertices[i].Normal;
        vertices[k].TexC = sphere.Vertices[i].TexC;
    }

    for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = cylinder.Vertices[i].Position;
        vertices[k].Normal = cylinder.Vertices[i].Normal;
        vertices[k].TexC = cylinder.Vertices[i].TexC;
    }

    for (size_t i = 0; i < cone.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = cone.Vertices[i].Position;
        vertices[k].Normal = cone.Vertices[i].Normal;
        vertices[k].TexC = cone.Vertices[i].TexC;
    }

    for (size_t i = 0; i < pyramid1.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = pyramid1.Vertices[i].Position;
        vertices[k].Normal = pyramid1.Vertices[i].Normal;
        vertices[k].TexC = pyramid1.Vertices[i].TexC;
    }

    for (size_t i = 0; i < pyramid2.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = pyramid2.Vertices[i].Position;
        vertices[k].Normal = pyramid2.Vertices[i].Normal;
        vertices[k].TexC = pyramid2.Vertices[i].TexC;
    }

    for (size_t i = 0; i < squarePyramid.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = squarePyramid.Vertices[i].Position;
        vertices[k].Normal = squarePyramid.Vertices[i].Normal;
        vertices[k].TexC = squarePyramid.Vertices[i].TexC;
    }

    for (size_t i = 0; i < triangularPrism.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = triangularPrism.Vertices[i].Position;
        vertices[k].Normal = triangularPrism.Vertices[i].Normal;
        vertices[k].TexC = triangularPrism.Vertices[i].TexC;
    }

    for (size_t i = 0; i < donut.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = donut.Vertices[i].Position;
        vertices[k].Normal = donut.Vertices[i].Normal;
        vertices[k].TexC = donut.Vertices[i].TexC;
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
    geo->Name = "Castle";


    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
                                                        mCommandList.Get(), vertices.data(), vbByteSize,
                                                        geo->VertexBufferUploader);

    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
                                                       mCommandList.Get(), indices.data(), ibByteSize,
                                                       geo->IndexBufferUploader);

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

void CastleApp::BuildWaterGeometry()
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

    UINT vbByteSize = mWaves->VertexCount() * sizeof(Vertex);
    UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "waterGeo";

    // Set dynamically.
    geo->VertexBufferCPU = nullptr;
    geo->VertexBufferGPU = nullptr;

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
                                                       mCommandList.Get(), indices.data(), ibByteSize,
                                                       geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    SubmeshGeometry submesh;
    submesh.IndexCount = (UINT)indices.size();
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;

    geo->DrawArgs["grid"] = submesh;

    mGeometries["waterGeo"] = std::move(geo);
}

void CastleApp::BuildTreeSpritesGeometry()
{
    struct TreeSpriteVertex
    {
        XMFLOAT3 Pos;
        XMFLOAT2 Size;
    };

    static const int treeCount = 16;
    std::array<TreeSpriteVertex, 16> vertices;
    for(UINT i = 0; i < treeCount; ++i)
    {
        float theta = MathHelper::RandF(0.0f, XM_2PI);
        float radius = MathHelper::RandF(12.0f, 20.0f);

        float x = radius * std::cos(theta);
        float z = radius * std::sin(theta);
        
        float y = -0.1;
        
        // Move tree slightly above land height.
        y += 4.0f;

        vertices[i].Pos = XMFLOAT3(x, y, z);
        vertices[i].Size = XMFLOAT2(10.0f, 10.0f);
    }

    std::array<std::uint16_t, 16> indices =
    {
        0, 1, 2, 3, 4, 5, 6, 7,
        8, 9, 10, 11, 12, 13, 14, 15
    };

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(TreeSpriteVertex);
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "treeSpritesGeo";

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
        mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
        mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(TreeSpriteVertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    SubmeshGeometry submesh;
    submesh.IndexCount = (UINT)indices.size();
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;

    geo->DrawArgs["points"] = submesh;

    mGeometries["treeSpritesGeo"] = std::move(geo);
}

void CastleApp::BuildPSOs()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

    // PSO for opaque objects.

    ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    opaquePsoDesc.InputLayout = {mInputLayout.data(), (UINT)mInputLayout.size()};
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

    //
    // PSO for transparent objects
    //
    D3D12_GRAPHICS_PIPELINE_STATE_DESC transparentPsoDesc = opaquePsoDesc;

    D3D12_RENDER_TARGET_BLEND_DESC transparencyBlendDesc;
    transparencyBlendDesc.BlendEnable = true;
    transparencyBlendDesc.LogicOpEnable = false;
    transparencyBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    transparencyBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    transparencyBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
    transparencyBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
    transparencyBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
    transparencyBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    transparencyBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
    transparencyBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    //transparentPsoDesc.BlendState.AlphaToCoverageEnable = true;

    transparentPsoDesc.BlendState.RenderTarget[0] = transparencyBlendDesc;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&transparentPsoDesc, IID_PPV_ARGS(&mPSOs["transparent"])));
    //
    // PSO for tree sprites
    //
    D3D12_GRAPHICS_PIPELINE_STATE_DESC treeSpritePsoDesc = opaquePsoDesc;
    treeSpritePsoDesc.VS =
    {
        reinterpret_cast<BYTE*>(mShaders["treeSpriteVS"]->GetBufferPointer()),
        mShaders["treeSpriteVS"]->GetBufferSize()
    };
    treeSpritePsoDesc.GS =
    {
        reinterpret_cast<BYTE*>(mShaders["treeSpriteGS"]->GetBufferPointer()),
        mShaders["treeSpriteGS"]->GetBufferSize()
    };
    treeSpritePsoDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["treeSpritePS"]->GetBufferPointer()),
        mShaders["treeSpritePS"]->GetBufferSize()
    };
    //step1
    treeSpritePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    treeSpritePsoDesc.InputLayout = {mTreeSpriteInputLayout.data(), (UINT)mTreeSpriteInputLayout.size()};
    treeSpritePsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&treeSpritePsoDesc, IID_PPV_ARGS(&mPSOs["treeSprites"])));
}


void CastleApp::BuildFrameResources()
{
    for (int i = 0; i < gNumFrameResources; ++i)
    {
        mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
                                                                  1, (UINT)mAllRitems.size(), (UINT)mMaterials.size(), mWaves->VertexCount()));
    }
}


void CastleApp::BuildRenderItems()
{
    UINT objIndex = 0;
    auto gridRitem = std::make_unique<RenderItem>();

    XMStoreFloat4x4(&gridRitem->World, XMMatrixScaling(2.0f, 1.0f, 2.0f));
    gridRitem->ObjCBIndex = objIndex++;
    gridRitem->Mat = mMaterials["grass"].get();
    gridRitem->Geo = mGeometries["Castle"].get();
    gridRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
    gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
    gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
    mRitemLayer[(int)RenderLayer::Opaque].push_back(gridRitem.get());
    mAllRitems.push_back(std::move(gridRitem));

    float dx[4] = {7.0f, 7.0f, -7.0f, -7.0f}, dz[4] = {7.0f, -7.0f, -7.0f, 7.0f};

    for (int i = 0; i < 4; ++i)
    {
        auto towerRitem1 = std::make_unique<RenderItem>();

        XMStoreFloat4x4(&towerRitem1->World,
                        XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(dx[i], 0.5f, dz[i]));
        towerRitem1->ObjCBIndex = objIndex++;
        towerRitem1->Mat = mMaterials["brick1"].get();
        towerRitem1->Geo = mGeometries["Castle"].get();
        towerRitem1->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        towerRitem1->IndexCount = towerRitem1->Geo->DrawArgs["cylinder"].IndexCount;
        towerRitem1->StartIndexLocation = towerRitem1->Geo->DrawArgs["cylinder"].StartIndexLocation;
        towerRitem1->BaseVertexLocation = towerRitem1->Geo->DrawArgs["cylinder"].BaseVertexLocation;
        mRitemLayer[(int)RenderLayer::Opaque].push_back(towerRitem1.get());
        mAllRitems.push_back(std::move(towerRitem1));

        auto towerRitem2 = std::make_unique<RenderItem>();

        XMStoreFloat4x4(&towerRitem2->World,
                        XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(dx[i], 3.7f, dz[i]));
        towerRitem2->ObjCBIndex = objIndex++;
        towerRitem2->Mat = mMaterials["brick3"].get();
        towerRitem2->Geo = mGeometries["Castle"].get();
        towerRitem2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        towerRitem2->IndexCount = towerRitem2->Geo->DrawArgs["pyramid2"].IndexCount;
        towerRitem2->StartIndexLocation = towerRitem2->Geo->DrawArgs["pyramid2"].StartIndexLocation;
        towerRitem2->BaseVertexLocation = towerRitem2->Geo->DrawArgs["pyramid2"].BaseVertexLocation;
        mRitemLayer[(int)RenderLayer::Opaque].push_back(towerRitem2.get());
        mAllRitems.push_back(std::move(towerRitem2));

        auto towerRitem3 = std::make_unique<RenderItem>();
        XMStoreFloat4x4(&towerRitem3->World, XMMatrixTranslation(dx[i], 4.5f, dz[i]));
        towerRitem3->ObjCBIndex = objIndex++;
        towerRitem3->Mat = mMaterials["ice"].get();
        towerRitem3->Geo = mGeometries["Castle"].get();
        towerRitem3->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        towerRitem3->IndexCount = towerRitem3->Geo->DrawArgs["sphere"].IndexCount;
        towerRitem3->StartIndexLocation = towerRitem3->Geo->DrawArgs["sphere"].StartIndexLocation;
        towerRitem3->BaseVertexLocation = towerRitem3->Geo->DrawArgs["sphere"].BaseVertexLocation;
        mRitemLayer[(int)RenderLayer::Opaque].push_back(towerRitem3.get());
        mAllRitems.push_back(std::move(towerRitem3));
    }

    auto wallRitem1 = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&wallRitem1->World, XMMatrixScaling(13.0f, 3.0f, 1.5f) * XMMatrixTranslation(0.0, 1.0f, 7.0));
    XMStoreFloat4x4(&wallRitem1->TexTransform, XMMatrixScaling(2.5f, 0.5f, 1.0f));
    wallRitem1->ObjCBIndex = objIndex++;
    wallRitem1->Mat = mMaterials["brick2"].get();
    wallRitem1->Geo = mGeometries["Castle"].get();
    wallRitem1->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    wallRitem1->IndexCount = wallRitem1->Geo->DrawArgs["box"].IndexCount;
    wallRitem1->StartIndexLocation = wallRitem1->Geo->DrawArgs["box"].StartIndexLocation;
    wallRitem1->BaseVertexLocation = wallRitem1->Geo->DrawArgs["box"].BaseVertexLocation;
    mRitemLayer[(int)RenderLayer::Opaque].push_back(wallRitem1.get());
    mAllRitems.push_back(std::move(wallRitem1));

    auto wallRitem2 = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&wallRitem2->World, XMMatrixScaling(13.0f, 3.0f, 1.5f) * XMMatrixTranslation(0.0, 1.0f, -7.0));
    XMStoreFloat4x4(&wallRitem2->TexTransform, XMMatrixScaling(2.5f, 0.5f, 1.0f));
    wallRitem2->ObjCBIndex = objIndex++;
    wallRitem2->Mat = mMaterials["brick2"].get();
    wallRitem2->Geo = mGeometries["Castle"].get();
    wallRitem2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    wallRitem2->IndexCount = wallRitem2->Geo->DrawArgs["box"].IndexCount;
    wallRitem2->StartIndexLocation = wallRitem2->Geo->DrawArgs["box"].StartIndexLocation;
    wallRitem2->BaseVertexLocation = wallRitem2->Geo->DrawArgs["box"].BaseVertexLocation;
    mRitemLayer[(int)RenderLayer::Opaque].push_back(wallRitem2.get());
    mAllRitems.push_back(std::move(wallRitem2));

    auto wallRitem3 = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&wallRitem3->World,
                    XMMatrixScaling(13.0f, 3.0f, 1.5f) * XMMatrixRotationY(XM_PIDIV2) * XMMatrixTranslation(
                        7.0, 1.0f, 0.0));
    XMStoreFloat4x4(&wallRitem3->TexTransform, XMMatrixScaling(2.5f, 0.5f, 1.0f));
    wallRitem3->ObjCBIndex = objIndex++;
    wallRitem3->Mat = mMaterials["brick2"].get();
    wallRitem3->Geo = mGeometries["Castle"].get();
    wallRitem3->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    wallRitem3->IndexCount = wallRitem3->Geo->DrawArgs["box"].IndexCount;
    wallRitem3->StartIndexLocation = wallRitem3->Geo->DrawArgs["box"].StartIndexLocation;
    wallRitem3->BaseVertexLocation = wallRitem3->Geo->DrawArgs["box"].BaseVertexLocation;
    mRitemLayer[(int)RenderLayer::Opaque].push_back(wallRitem3.get());
    mAllRitems.push_back(std::move(wallRitem3));

    auto wallRitem4 = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&wallRitem4->World,
                    XMMatrixScaling(13.0f, 3.0f, 1.5f) * XMMatrixRotationY(XM_PIDIV2) * XMMatrixTranslation(
                        -7.0, 1.0f, 0.0));
    XMStoreFloat4x4(&wallRitem4->TexTransform, XMMatrixScaling(2.5f, 0.5f, 1.0f));
    wallRitem4->ObjCBIndex = objIndex++;
    wallRitem4->Mat = mMaterials["brick2"].get();
    wallRitem4->Geo = mGeometries["Castle"].get();
    wallRitem4->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    wallRitem4->IndexCount = wallRitem4->Geo->DrawArgs["box"].IndexCount;
    wallRitem4->StartIndexLocation = wallRitem4->Geo->DrawArgs["box"].StartIndexLocation;
    wallRitem4->BaseVertexLocation = wallRitem4->Geo->DrawArgs["box"].BaseVertexLocation;
    mRitemLayer[(int)RenderLayer::Opaque].push_back(wallRitem4.get());
    mAllRitems.push_back(std::move(wallRitem4));

    float ofset[] = {-4.5f, -2.5f, 2.5f, 4.5f};

    for (int i = 0; i < 4; ++i)
    {
        auto wallObjRitem1 = std::make_unique<RenderItem>();
        XMStoreFloat4x4(&wallObjRitem1->World, XMMatrixTranslation(7.0, 3.0f, ofset[i]));
        wallObjRitem1->ObjCBIndex = objIndex++;
        wallObjRitem1->Mat = mMaterials["brick1"].get();
        wallObjRitem1->Geo = mGeometries["Castle"].get();
        wallObjRitem1->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        wallObjRitem1->IndexCount = wallObjRitem1->Geo->DrawArgs["squarePyramid"].IndexCount;
        wallObjRitem1->StartIndexLocation = wallObjRitem1->Geo->DrawArgs["squarePyramid"].StartIndexLocation;
        wallObjRitem1->BaseVertexLocation = wallObjRitem1->Geo->DrawArgs["squarePyramid"].BaseVertexLocation;
        mRitemLayer[(int)RenderLayer::Opaque].push_back(wallObjRitem1.get());
        mAllRitems.push_back(std::move(wallObjRitem1));

        auto wallObjRitem2 = std::make_unique<RenderItem>();
        XMStoreFloat4x4(&wallObjRitem2->World, XMMatrixTranslation(-7.0, 3.0f, ofset[i]));
        wallObjRitem2->ObjCBIndex = objIndex++;
        wallObjRitem2->Mat = mMaterials["brick2"].get();
        wallObjRitem2->Geo = mGeometries["Castle"].get();
        wallObjRitem2->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        wallObjRitem2->IndexCount = wallObjRitem2->Geo->DrawArgs["squarePyramid"].IndexCount;
        wallObjRitem2->StartIndexLocation = wallObjRitem2->Geo->DrawArgs["squarePyramid"].StartIndexLocation;
        wallObjRitem2->BaseVertexLocation = wallObjRitem2->Geo->DrawArgs["squarePyramid"].BaseVertexLocation;
        mRitemLayer[(int)RenderLayer::Opaque].push_back(wallObjRitem2.get());
        mAllRitems.push_back(std::move(wallObjRitem2));

        auto wallObjRitem3 = std::make_unique<RenderItem>();
        XMStoreFloat4x4(&wallObjRitem3->World, XMMatrixTranslation(ofset[i], 3.0f, 7.0f));
        wallObjRitem3->ObjCBIndex = objIndex++;
        wallObjRitem3->Mat = mMaterials["brick2"].get();
        wallObjRitem3->Geo = mGeometries["Castle"].get();
        wallObjRitem3->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        wallObjRitem3->IndexCount = wallObjRitem3->Geo->DrawArgs["cone"].IndexCount;
        wallObjRitem3->StartIndexLocation = wallObjRitem3->Geo->DrawArgs["cone"].StartIndexLocation;
        wallObjRitem3->BaseVertexLocation = wallObjRitem3->Geo->DrawArgs["cone"].BaseVertexLocation;
        mRitemLayer[(int)RenderLayer::Opaque].push_back(wallObjRitem3.get());
        mAllRitems.push_back(std::move(wallObjRitem3));

        auto wallObjRitem4 = std::make_unique<RenderItem>();
        XMStoreFloat4x4(&wallObjRitem4->World, XMMatrixTranslation(ofset[i], 3.0f, -7.0f));
        wallObjRitem4->ObjCBIndex = objIndex++;
        wallObjRitem4->Mat = mMaterials["brick1"].get();
        wallObjRitem4->Geo = mGeometries["Castle"].get();
        wallObjRitem4->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        wallObjRitem4->IndexCount = wallObjRitem4->Geo->DrawArgs["pyramid1"].IndexCount;
        wallObjRitem4->StartIndexLocation = wallObjRitem4->Geo->DrawArgs["pyramid1"].StartIndexLocation;
        wallObjRitem4->BaseVertexLocation = wallObjRitem4->Geo->DrawArgs["pyramid1"].BaseVertexLocation;
        mRitemLayer[(int)RenderLayer::Opaque].push_back(wallObjRitem4.get());
        mAllRitems.push_back(std::move(wallObjRitem4));
    }

    auto baseRitem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&baseRitem->World, XMMatrixScaling(7.5f, 2.5f, 7.5f) * XMMatrixTranslation(0.0, 1.0f, 0.0));
    baseRitem->ObjCBIndex = objIndex++;
    baseRitem->Mat = mMaterials["checkboard"].get();
    baseRitem->Geo = mGeometries["Castle"].get();
    baseRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    baseRitem->IndexCount = baseRitem->Geo->DrawArgs["triangularPrism"].IndexCount;
    baseRitem->StartIndexLocation = baseRitem->Geo->DrawArgs["triangularPrism"].StartIndexLocation;
    baseRitem->BaseVertexLocation = baseRitem->Geo->DrawArgs["triangularPrism"].BaseVertexLocation;
    mRitemLayer[(int)RenderLayer::Opaque].push_back(baseRitem.get());
    mAllRitems.push_back(std::move(baseRitem));

    auto donutRitem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&donutRitem->World,
                    XMMatrixScaling(0.7f, 0.7f, 0.7f) * XMMatrixRotationX(XM_PIDIV2 * 1.3f) * XMMatrixTranslation(
                        0.0, 3.0f, 0.0));
    donutRitem->ObjCBIndex = objIndex++;
    donutRitem->Mat = mMaterials["ice"].get();
    donutRitem->Geo = mGeometries["Castle"].get();
    donutRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    donutRitem->IndexCount = donutRitem->Geo->DrawArgs["donut"].IndexCount;
    donutRitem->StartIndexLocation = donutRitem->Geo->DrawArgs["donut"].StartIndexLocation;
    donutRitem->BaseVertexLocation = donutRitem->Geo->DrawArgs["donut"].BaseVertexLocation;
    mRitemLayer[(int)RenderLayer::Opaque].push_back(donutRitem.get());
    mAllRitems.push_back(std::move(donutRitem));

    auto floorRitem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&floorRitem->World, XMMatrixScaling(13.0f, 0.7f, 13.0f) * XMMatrixTranslation(0.0, -0.2f, 0.0));
    floorRitem->ObjCBIndex = objIndex++;
    floorRitem->Mat = mMaterials["brick2"].get();
    floorRitem->Geo = mGeometries["Castle"].get();
    floorRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    floorRitem->IndexCount = floorRitem->Geo->DrawArgs["box"].IndexCount;
    floorRitem->StartIndexLocation = floorRitem->Geo->DrawArgs["box"].StartIndexLocation;
    floorRitem->BaseVertexLocation = floorRitem->Geo->DrawArgs["box"].BaseVertexLocation;
    mRitemLayer[(int)RenderLayer::Opaque].push_back(floorRitem.get());
    mAllRitems.push_back(std::move(floorRitem));

    auto waterRitem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&waterRitem->World, XMMatrixTranslation(0.0, 0.1f, 0.0));
    XMStoreFloat4x4(&waterRitem->TexTransform, XMMatrixScaling(5.0f, 5.0f, 1.0f));
    waterRitem->ObjCBIndex = objIndex++;
    waterRitem->Mat = mMaterials["water"].get();
    waterRitem->Geo = mGeometries["waterGeo"].get();
    waterRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    waterRitem->IndexCount = waterRitem->Geo->DrawArgs["grid"].IndexCount;
    waterRitem->StartIndexLocation = waterRitem->Geo->DrawArgs["grid"].StartIndexLocation;
    waterRitem->BaseVertexLocation = waterRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
    mWavesRitem = waterRitem.get();
    mRitemLayer[(int)RenderLayer::Transparent].push_back(waterRitem.get());
    mAllRitems.push_back(std::move(waterRitem));

    auto treeSpritesRitem = std::make_unique<RenderItem>();
	treeSpritesRitem->World = MathHelper::Identity4x4();
    treeSpritesRitem->ObjCBIndex = objIndex++;
    treeSpritesRitem->Mat = mMaterials["treeSprites"].get();
    treeSpritesRitem->Geo = mGeometries["treeSpritesGeo"].get();
    treeSpritesRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
    treeSpritesRitem->IndexCount = treeSpritesRitem->Geo->DrawArgs["points"].IndexCount;
    treeSpritesRitem->StartIndexLocation = treeSpritesRitem->Geo->DrawArgs["points"].StartIndexLocation;
    treeSpritesRitem->BaseVertexLocation = treeSpritesRitem->Geo->DrawArgs["points"].BaseVertexLocation;
    mRitemLayer[(int)RenderLayer::AlphaTestedTreeSprites].push_back(treeSpritesRitem.get());
    mAllRitems.push_back(std::move(treeSpritesRitem));

    auto gateRitem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&gateRitem->World, XMMatrixScaling(5.0f, 3.0f, 2.0f) * XMMatrixTranslation(0.0, 0.7f, -7.0));
    gateRitem->ObjCBIndex = objIndex++;
    gateRitem->Mat = mMaterials["wood"].get();
    gateRitem->Geo = mGeometries["Castle"].get();
    gateRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    gateRitem->IndexCount = gateRitem->Geo->DrawArgs["box"].IndexCount;
    gateRitem->StartIndexLocation = gateRitem->Geo->DrawArgs["box"].StartIndexLocation;
    gateRitem->BaseVertexLocation = gateRitem->Geo->DrawArgs["box"].BaseVertexLocation;
    mRitemLayer[(int)RenderLayer::Opaque].push_back(gateRitem.get());
    mAllRitems.push_back(std::move(gateRitem));
}

void CastleApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

    auto objectCB = mCurrFrameResource->ObjectCB->Resource();
    auto matCB = mCurrFrameResource->MaterialCB->Resource();

    // For each render item...
    for (size_t i = 0; i < ritems.size(); ++i)
    {
        auto ri = ritems[i];
        cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
        cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
        cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

        CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
        tex.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvDescriptorSize);

        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
        D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;

        cmdList->SetGraphicsRootDescriptorTable(0, tex);
        cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
        cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);

        cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
}

void CastleApp::LoadTextures()
{
    auto grassTex = std::make_unique<Texture>();
    grassTex->Name = "grassTex";
    grassTex->Filename = L"../../Textures/grass.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
        mCommandList.Get(), grassTex->Filename.c_str(),
        grassTex->Resource, grassTex->UploadHeap));

    auto brickTex1 = std::make_unique<Texture>();
    brickTex1->Name = "brickTex1";
    brickTex1->Filename = L"../../Textures/bricks.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
        mCommandList.Get(), brickTex1->Filename.c_str(),
        brickTex1->Resource, brickTex1->UploadHeap));

    auto brickTex2 = std::make_unique<Texture>();
    brickTex2->Name = "brickTex2";
    brickTex2->Filename = L"../../Textures/bricks2.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
        mCommandList.Get(), brickTex2->Filename.c_str(),
        brickTex2->Resource, brickTex2->UploadHeap));

    auto brickTex3 = std::make_unique<Texture>();
    brickTex3->Name = "brickTex3";
    brickTex3->Filename = L"../../Textures/bricks3.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
        mCommandList.Get(), brickTex3->Filename.c_str(),
        brickTex3->Resource, brickTex3->UploadHeap));

    auto iceTex = std::make_unique<Texture>();
    iceTex->Name = "iceTex";
    iceTex->Filename = L"../../Textures/ice.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
        mCommandList.Get(), iceTex->Filename.c_str(),
        iceTex->Resource, iceTex->UploadHeap));

    auto checkboardTex = std::make_unique<Texture>();
    checkboardTex->Name = "checkboardTex";
    checkboardTex->Filename = L"../../Textures/checkboard.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
        mCommandList.Get(), checkboardTex->Filename.c_str(),
        checkboardTex->Resource, checkboardTex->UploadHeap));

    auto waterTex = std::make_unique<Texture>();
    waterTex->Name = "waterTex";
    waterTex->Filename = L"../../Textures/water1.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
        mCommandList.Get(), waterTex->Filename.c_str(),
        waterTex->Resource, waterTex->UploadHeap));

    auto woodTex = std::make_unique<Texture>();
    woodTex->Name = "woodTex";
    woodTex->Filename = L"../../Textures/WoodCrate02.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
        mCommandList.Get(), woodTex->Filename.c_str(),
        woodTex->Resource, woodTex->UploadHeap));

    auto treeTex = std::make_unique<Texture>();
    treeTex->Name = "treeTex";
    treeTex->Filename = L"../../Textures/treearray.dds";
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
        mCommandList.Get(), treeTex->Filename.c_str(),
        treeTex->Resource, treeTex->UploadHeap));

    mTextures[grassTex->Name] = std::move(grassTex);
    mTextures[brickTex1->Name] = std::move(brickTex1);
    mTextures[brickTex2->Name] = std::move(brickTex2);
    mTextures[brickTex3->Name] = std::move(brickTex3);
    mTextures[iceTex->Name] = std::move(iceTex);
    mTextures[checkboardTex->Name] = std::move(checkboardTex);
    mTextures[waterTex->Name] = std::move(waterTex);
    mTextures[woodTex->Name] = std::move(woodTex);
    mTextures[treeTex->Name] = std::move(treeTex);
}

void CastleApp::BuildMaterials()
{
    auto grass = std::make_unique<Material>();
    grass->Name = "grass";
    grass->MatCBIndex = 0;
    grass->DiffuseSrvHeapIndex = 0;
    grass->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    grass->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
    grass->Roughness = 0.125f;

    auto brick1 = std::make_unique<Material>();
    brick1->Name = "brick1";
    brick1->MatCBIndex = 1;
    brick1->DiffuseSrvHeapIndex = 1;
    brick1->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    brick1->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
    brick1->Roughness = 0.125f;

    auto brick2 = std::make_unique<Material>();
    brick2->Name = "brick2";
    brick2->MatCBIndex = 2;
    brick2->DiffuseSrvHeapIndex = 2;
    brick2->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    brick2->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
    brick2->Roughness = 0.225f;

    auto brick3 = std::make_unique<Material>();
    brick3->Name = "brick3";
    brick3->MatCBIndex = 3;
    brick3->DiffuseSrvHeapIndex = 3;
    brick3->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    brick3->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
    brick3->Roughness = 0.325f;

    auto ice = std::make_unique<Material>();
    ice->Name = "ice";
    ice->MatCBIndex = 4;
    ice->DiffuseSrvHeapIndex = 4;
    ice->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    ice->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
    ice->Roughness = 0.015f;

    auto checkboard = std::make_unique<Material>();
    checkboard->Name = "checkboard";
    checkboard->MatCBIndex = 5;
    checkboard->DiffuseSrvHeapIndex = 5;
    checkboard->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    checkboard->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
    checkboard->Roughness = 0.325f;

    auto water = std::make_unique<Material>();
    water->Name = "water";
    water->MatCBIndex = 6;
    water->DiffuseSrvHeapIndex = 6;
    water->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.5f);
    water->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
    water->Roughness = 0.0f;

    auto wood = std::make_unique<Material>();
    wood->Name = "wood";
    wood->MatCBIndex = 7;
    wood->DiffuseSrvHeapIndex = 7;
    wood->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    wood->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
    wood->Roughness = 0.325f;

    auto treeSprites = std::make_unique<Material>();
    treeSprites->Name = "treeSprites";
    treeSprites->MatCBIndex = 8;
    treeSprites->DiffuseSrvHeapIndex = 8;
    treeSprites->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    treeSprites->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
    treeSprites->Roughness = 0.125f;

    mMaterials["grass"] = std::move(grass);
    mMaterials["brick1"] = std::move(brick1);
    mMaterials["brick2"] = std::move(brick2);
    mMaterials["brick3"] = std::move(brick3);
    mMaterials["ice"] = std::move(ice);
    mMaterials["checkboard"] = std::move(checkboard);
    mMaterials["water"] = std::move(water);
    mMaterials["wood"] = std::move(wood);
    mMaterials["treeSprites"] = std::move(treeSprites);
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> CastleApp::GetStaticSamplers()
{
    // Applications usually only need a handful of samplers.  So just define them all up front
    // and keep them available as part of the root signature.  

    const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
        0, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
        1, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
        2, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
        3, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
        4, // shaderRegister
        D3D12_FILTER_ANISOTROPIC, // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressW
        0.0f, // mipLODBias
        8); // maxAnisotropy

    const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
        5, // shaderRegister
        D3D12_FILTER_ANISOTROPIC, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressW
        0.0f, // mipLODBias
        8); // maxAnisotropy

    return {
        pointWrap, pointClamp,
        linearWrap, linearClamp,
        anisotropicWrap, anisotropicClamp
    };
}
