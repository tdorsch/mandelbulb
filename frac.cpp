//--------------------------------------------------------------------------------------
// File: frac.cpp
//
// Mandelbulb rendering test.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------
#include "DXUT.h"
#include "DXUTcamera.h"
#include "DXUTgui.h"
#include "DXUTsettingsdlg.h"
#include "SDKmisc.h"
#include <D3DX11tex.h>
#include <D3DX11.h>
#include <D3DX11core.h>
#include <D3DX11async.h>

#define ENABLE_MODEL_VIEW_CAMERA

enum POSTPROCESS_MODE
{
    PM_COMPUTE_SHADER,
    PM_PIXEL_SHADER,
};
POSTPROCESS_MODE g_ePostProcessMode = PM_PIXEL_SHADER;// Stores which path is currently used for post-processing

CModelViewerCamera          g_MVCamera;                 // A model viewing camera
CFirstPersonCamera          g_FPCamera;                 // A first person camera
CBaseCamera*                g_pCamera;                  // base camera

CDXUTDialogResourceManager  g_DialogResourceManager;    // Manager for shared resources of dialogs
CD3DSettingsDlg             g_D3DSettingsDlg;           // Device settings dialog
CDXUTDialog                 g_HUD;                      // Dialog for standard controls
CDXUTDialog                 g_SampleUI;                 // Dialog for sample specific controls
CDXUTTextHelper*            g_pTxtHelper = NULL;

// Stuff used for drawing the "full screen quad"
struct SCREEN_VERTEX
{
    D3DXVECTOR4 pos;
    D3DXVECTOR2 tex;
};
ID3D11Buffer*               g_pScreenQuadVB = NULL;
ID3D11InputLayout*          g_pQuadLayout = NULL;
ID3D11VertexShader*         g_pQuadVS = NULL;

// Constant buffer layout for transferring data to the CS
struct CbMandelbulb
{
    D3DXMATRIX mProj;
    D3DXMATRIX mInvWorld;
    D3DXMATRIX mInvView;
    float dist;
    float pad0;
    float pad1;
    float pad2;
};

ID3D11Buffer*               g_pcbMandelbulb = NULL;         // Constant buffer for passing parameters into the CS

CbMandelbulb                g_cbMandelbulb;                 // mandelbulb time factor
bool                        g_bBloom = false;               // Bloom effect on/off
bool                        g_bFullScrBlur = false;         // Full screen blur on/off
bool                        g_bPostProcessON = true;        // All post-processing effect on/off

CDXUTStatic*                g_pStaticTech = NULL;           // Sample specific UI
CDXUTComboBox*              g_pComboBoxTech = NULL;
CDXUTCheckBox*              g_pCheckBloom = NULL;
CDXUTCheckBox*              g_pCheckScrBlur = NULL;
ID3D11PixelShader*          g_pMandelbulbPS = NULL;
ID3D11PixelShader*          g_pMandelboxPS = NULL;

ID3D11SamplerState*         g_pSampleStatePoint = NULL;
ID3D11SamplerState*         g_pSampleStateLinear = NULL;


//--------------------------------------------------------------------------------------
// UI control IDs
//--------------------------------------------------------------------------------------
#define IDC_TOGGLEFULLSCREEN    1
#define IDC_TOGGLEREF           3
#define IDC_CHANGEDEVICE        4
#define IDC_POSTPROCESS_MODE    5
#define IDC_BLOOM               6
#define IDC_POSTPROCESSON       7
#define IDC_SCREENBLUR          8

//--------------------------------------------------------------------------------------
// Forward declarations 
//--------------------------------------------------------------------------------------
bool CALLBACK ModifyDeviceSettings( DXUTDeviceSettings* pDeviceSettings, void* pUserContext );
void CALLBACK OnFrameMove( double fTime, float fElapsedTime, void* pUserContext );
LRESULT CALLBACK MsgProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool* pbNoFurtherProcessing,
                         void* pUserContext );
void CALLBACK OnGUIEvent( UINT nEvent, int nControlID, CDXUTControl* pControl, void* pUserContext );

bool CALLBACK IsD3D11DeviceAcceptable( const CD3D11EnumAdapterInfo *AdapterInfo, UINT Output, const CD3D11EnumDeviceInfo *DeviceInfo,
                                      DXGI_FORMAT BackBufferFormat, bool bWindowed, void* pUserContext );
HRESULT CALLBACK OnD3D11CreateDevice( ID3D11Device* pd3dDevice, const DXGI_SURFACE_DESC* pBackBufferSurfaceDesc,
                                     void* pUserContext );
HRESULT CALLBACK OnD3D11ResizedSwapChain( ID3D11Device* pd3dDevice, IDXGISwapChain* pSwapChain,
                                         const DXGI_SURFACE_DESC* pBackBufferSurfaceDesc, void* pUserContext );
void CALLBACK OnD3D11ReleasingSwapChain( void* pUserContext );
void CALLBACK OnD3D11DestroyDevice( void* pUserContext );
void CALLBACK OnD3D11FrameRender( ID3D11Device* pd3dDevice, ID3D11DeviceContext* pd3dImmediateContext, double fTime,
                                 float fElapsedTime, void* pUserContext );

void InitApp();
void RenderText();

template<typename T>
HRESULT CopyToBuffer(ID3D11DeviceContext* pContext, T* pData, ID3D11Buffer* pcbBuffer)
{
    HRESULT hr = S_OK;
    D3D11_MAPPED_SUBRESOURCE MappedResource;            
    V( pContext->Map( pcbBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedResource ) );
    memcpy_s(MappedResource.pData, sizeof(T), pData, sizeof(T));
    pContext->Unmap( pcbBuffer, 0 );

    return hr;
}

double DE(D3DXVECTOR3 p)
{
    D3DXVECTOR3 c = p;
    float r = D3DXVec3Length(&c);
    float dr = 1;
    for (int i = 0; i < 4 && r < 3; ++i)
    {
        float xr = pow(r, 7);
        dr = 6 * xr * dr + 1;
  
        float theta = atan2(c.y, c.x) * 8;
        float phi = asin(c.z / r) * 8;
        r = xr * r;
        c = r * D3DXVECTOR3(cos(phi) * cos(theta), cos(phi) * sin(theta), sin(phi));
   
        c += p;
        r = D3DXVec3Length(&c);
    }

    return 0.35 * log(r) * r / dr;
}

float DE2(D3DXVECTOR3 p)
{
    D3DXVECTOR3 c = p;
    float r = D3DXVec3Length(&c);
    float dr = 1;
    for (int i = 0; i < 4 && r < 3; ++i)
    {
        float xr = pow(r, 7);
        dr = 6 * xr * dr + 1;
  
        float theta = atan2(c.y, c.x) * 8;
        float phi = asin(c.z / r) * 8;
        r = xr * r;
        c = r * D3DXVECTOR3(cos(phi) * cos(theta), cos(phi) * sin(theta), sin(phi));
   
        c += p;
        r = D3DXVec3Length(&c);
    }
    return 0.35 * log(r) * r / dr;
}

//--------------------------------------------------------------------------------------
// Find and compile the specified shader
//--------------------------------------------------------------------------------------
HRESULT CompileShaderFromFile( WCHAR* szFileName, LPCSTR szEntryPoint, LPCSTR szShaderModel, ID3DBlob** ppBlobOut )
{
    HRESULT hr = S_OK;

    // find the file
    WCHAR str[MAX_PATH];
    V_RETURN( DXUTFindDXSDKMediaFileCch( str, MAX_PATH, szFileName ) );

    DWORD dwShaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined( DEBUG ) || defined( _DEBUG )
    // Set the D3DCOMPILE_DEBUG flag to embed debug information in the shaders.
    // Setting this flag improves the shader debugging experience, but still allows 
    // the shaders to be optimized and to run exactly the way they will run in 
    // the release configuration of this program.
    dwShaderFlags |= D3DCOMPILE_DEBUG;
#endif

    ID3DBlob* pErrorBlob;
    hr = D3DX11CompileFromFile( str, NULL, NULL, szEntryPoint, szShaderModel, 
        dwShaderFlags, 0, NULL, ppBlobOut, &pErrorBlob, NULL );
    if( FAILED(hr) )
    {
        if( pErrorBlob != NULL )
            OutputDebugStringA( (char*)pErrorBlob->GetBufferPointer() );
        SAFE_RELEASE( pErrorBlob );
        return hr;
    }
    SAFE_RELEASE( pErrorBlob );

    return S_OK;
}

//--------------------------------------------------------------------------------------
// Entry point to the program. Initializes everything and goes into a message processing 
// loop. Idle time is used to render the scene.
//--------------------------------------------------------------------------------------
int WINAPI wWinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow )
{
    // Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
#endif

    // Disable gamma correction on this sample
    DXUTSetIsInGammaCorrectMode( false );

    DXUTSetCallbackDeviceChanging( ModifyDeviceSettings );
    DXUTSetCallbackMsgProc( MsgProc );
    DXUTSetCallbackFrameMove( OnFrameMove );
    
    DXUTSetCallbackD3D11DeviceAcceptable( IsD3D11DeviceAcceptable );
    DXUTSetCallbackD3D11DeviceCreated( OnD3D11CreateDevice );
    DXUTSetCallbackD3D11SwapChainResized( OnD3D11ResizedSwapChain );
    DXUTSetCallbackD3D11FrameRender( OnD3D11FrameRender );
    DXUTSetCallbackD3D11SwapChainReleasing( OnD3D11ReleasingSwapChain );
    DXUTSetCallbackD3D11DeviceDestroyed( OnD3D11DestroyDevice );

    InitApp();
    
    DXUTInit( true, true );                 // Use this line instead to try to create a hardware device

    DXUTSetCursorSettings( true, true );    // Show the cursor and clip it when in full screen
    DXUTCreateWindow( L"HDRToneMappingCS11" );
    DXUTCreateDevice( D3D_FEATURE_LEVEL_10_0, true, 640, 480 );
    DXUTMainLoop();                         // Enter into the DXUT render loop

    return DXUTGetExitCode();
}

//--------------------------------------------------------------------------------------
// Initialize the app 
//--------------------------------------------------------------------------------------
void InitApp()
{
#ifdef ENABLE_MODEL_VIEW_CAMERA
    g_pCamera = &g_MVCamera;
#else
    g_pCamera = &g_FPCamera;
#endif

    g_D3DSettingsDlg.Init( &g_DialogResourceManager );
    g_HUD.Init( &g_DialogResourceManager );
    g_SampleUI.Init( &g_DialogResourceManager );

    g_HUD.SetCallback( OnGUIEvent ); int iY = 30;
    g_HUD.AddButton( IDC_TOGGLEFULLSCREEN, L"Toggle full screen", 0, iY, 170, 23 );
    g_HUD.AddButton( IDC_TOGGLEREF, L"Toggle REF (F3)", 0, iY += 26, 170, 23, VK_F3 );
    g_HUD.AddButton( IDC_CHANGEDEVICE, L"Change device (F2)", 0, iY += 26, 170, 23, VK_F2 );

    g_SampleUI.AddCheckBox( IDC_POSTPROCESSON, L"(P)ost process on:", -20, 150-50, 140, 18, g_bPostProcessON, 'P' );

    g_SampleUI.AddStatic( 0, L"Post processing (t)ech", 0, 150-20, 105, 25, false, &g_pStaticTech );
    g_SampleUI.AddComboBox( IDC_POSTPROCESS_MODE, 0, 150, 150, 24, 'T', false, &g_pComboBoxTech );
    g_pComboBoxTech->AddItem( L"Compute Shader", IntToPtr(PM_COMPUTE_SHADER) );
    g_pComboBoxTech->AddItem( L"Pixel Shader", IntToPtr(PM_PIXEL_SHADER) );

    g_SampleUI.AddCheckBox( IDC_BLOOM, L"Show (B)loom", 0, 195, 140, 18, g_bBloom, 'B', false, &g_pCheckBloom );
    g_SampleUI.AddCheckBox( IDC_SCREENBLUR, L"Full (S)creen Blur", 0, 195+20, 140, 18, g_bFullScrBlur, 'S', false, &g_pCheckScrBlur );

    g_SampleUI.SetCallback( OnGUIEvent ); 
}

//--------------------------------------------------------------------------------------
// This callback function is called immediately before a device is created to allow the 
// application to modify the device settings. The supplied pDeviceSettings parameter 
// contains the settings that the framework has selected for the new device, and the 
// application can make any desired changes directly to this structure.  Note however that 
// DXUT will not correct invalid device settings so care must be taken 
// to return valid device settings, otherwise CreateDevice() will fail.  
//--------------------------------------------------------------------------------------
bool CALLBACK ModifyDeviceSettings( DXUTDeviceSettings* pDeviceSettings, void* pUserContext )
{
    assert( pDeviceSettings->ver == DXUT_D3D11_DEVICE );
    
    // Add UAC flag to back buffer Texture2D resource, so we can create an UAV on the back buffer of the swap chain,
    // then it can be bound as the output resource of the CS
    // However, as CS4.0 cannot output to textures, this is taken out when the sample has been ported to CS4.0
    //pDeviceSettings->d3d11.sd.BufferUsage |= DXGI_USAGE_UNORDERED_ACCESS;

    // For the first device created if it is a REF device, optionally display a warning dialog box
    static bool s_bFirstTime = true;
    if( s_bFirstTime )
    {
        s_bFirstTime = false;
        if( ( DXUT_D3D9_DEVICE == pDeviceSettings->ver && pDeviceSettings->d3d9.DeviceType == D3DDEVTYPE_REF ) ||
            ( DXUT_D3D11_DEVICE == pDeviceSettings->ver &&
            pDeviceSettings->d3d11.DriverType == D3D_DRIVER_TYPE_REFERENCE ) )
        {
            DXUTDisplaySwitchingToREFWarning( pDeviceSettings->ver );
        }
    }

    return true;
}

//--------------------------------------------------------------------------------------
// This callback function will be called once at the beginning of every frame. This is the
// best location for your application to handle updates to the scene, but is not 
// intended to contain actual rendering calls, which should instead be placed in the 
// OnFrameRender callback.  
//--------------------------------------------------------------------------------------
void CALLBACK OnFrameMove( double fTime, float fElapsedTime, void* pUserContext )
{
    // Update the camera's position based on user input
    g_pCamera->FrameMove(fElapsedTime);
}

void RenderText()
{
    g_pTxtHelper->Begin();
    g_pTxtHelper->SetInsertionPos( 2, 0 );
    g_pTxtHelper->SetForegroundColor( D3DXCOLOR( 1.0f, 0.0f, 1.0f, 1.0f ) );
    g_pTxtHelper->DrawTextLine( DXUTGetFrameStats( DXUTIsVsyncEnabled() ) );
    g_pTxtHelper->DrawTextLine( DXUTGetDeviceStats() );

    g_pTxtHelper->End();
}

//--------------------------------------------------------------------------------------
// Before handling window messages, DXUT passes incoming windows 
// messages to the application through this callback function. If the application sets 
// *pbNoFurtherProcessing to TRUE, then DXUT will not process this message.
//--------------------------------------------------------------------------------------
LRESULT CALLBACK MsgProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, bool* pbNoFurtherProcessing,
                         void* pUserContext )
{
    // Pass messages to dialog resource manager calls so GUI state is updated correctly
    *pbNoFurtherProcessing = g_DialogResourceManager.MsgProc( hWnd, uMsg, wParam, lParam );
    if( *pbNoFurtherProcessing )
        return 0;

    // Pass messages to settings dialog if its active
    if( g_D3DSettingsDlg.IsActive() )
    {
        g_D3DSettingsDlg.MsgProc( hWnd, uMsg, wParam, lParam );
        return 0;
    }

    // Give the dialogs a chance to handle the message first
    *pbNoFurtherProcessing = g_HUD.MsgProc( hWnd, uMsg, wParam, lParam );
    if( *pbNoFurtherProcessing )
        return 0;
    *pbNoFurtherProcessing = g_SampleUI.MsgProc( hWnd, uMsg, wParam, lParam );
    if( *pbNoFurtherProcessing )
        return 0;

    // Pass all windows messages to camera so it can respond to user input
    g_pCamera->HandleMessages( hWnd, uMsg, wParam, lParam );

    return 0;
}

//--------------------------------------------------------------------------------------
// Handles the GUI events
//--------------------------------------------------------------------------------------
void CALLBACK OnGUIEvent( UINT nEvent, int nControlID, CDXUTControl* pControl, void* pUserContext )
{
    switch( nControlID )
    {
        case IDC_TOGGLEFULLSCREEN:
            DXUTToggleFullScreen(); break;
        case IDC_TOGGLEREF:
            DXUTToggleREF(); break;
        case IDC_CHANGEDEVICE:
            g_D3DSettingsDlg.SetActive( !g_D3DSettingsDlg.IsActive() ); break;

        case IDC_BLOOM:
            g_bBloom = !g_bBloom; break;
        case IDC_POSTPROCESSON:
            g_bPostProcessON = !g_bPostProcessON; 
            g_pStaticTech->SetEnabled( g_bPostProcessON );
            g_pComboBoxTech->SetEnabled( g_bPostProcessON );
            g_pCheckBloom->SetEnabled( g_bPostProcessON );
            g_pCheckScrBlur->SetEnabled( g_bPostProcessON );
            break;
        case IDC_SCREENBLUR:
            g_bFullScrBlur = !g_bFullScrBlur;
            break;

        case IDC_POSTPROCESS_MODE:
        {
            CDXUTComboBox* pComboBox = ( CDXUTComboBox* )pControl;
            g_ePostProcessMode = ( POSTPROCESS_MODE )( int )PtrToInt( pComboBox->GetSelectedData() );

            break;
        }        
    }

}

//--------------------------------------------------------------------------------------
// Reject any D3D11 devices that aren't acceptable by returning false
//--------------------------------------------------------------------------------------
bool CALLBACK IsD3D11DeviceAcceptable( const CD3D11EnumAdapterInfo *AdapterInfo, UINT Output, const CD3D11EnumDeviceInfo *DeviceInfo,
                                      DXGI_FORMAT BackBufferFormat, bool bWindowed, void* pUserContext )
{
    // reject any device which doesn't support CS4x
    if ( DeviceInfo->ComputeShaders_Plus_RawAndStructuredBuffers_Via_Shader_4_x == FALSE )
        return false;

    return true;
}

//--------------------------------------------------------------------------------------
// Create any D3D11 resources that aren't dependant on the back buffer
//--------------------------------------------------------------------------------------
HRESULT CALLBACK OnD3D11CreateDevice( ID3D11Device* pd3dDevice, const DXGI_SURFACE_DESC* pBackBufferSurfaceDesc,
                                     void* pUserContext )
{
    HRESULT hr;

    static bool bFirstOnCreateDevice = true;

    // Warn the user that in order to support CS4x, a non-hardware device has been created, continue or quit?
    if ( DXUTGetDeviceSettings().d3d11.DriverType != D3D_DRIVER_TYPE_HARDWARE && bFirstOnCreateDevice )
    {
        if ( MessageBox( 0, L"CS4x capability is missing. "\
                            L"In order to continue, a non-hardware device has been created, "\
                            L"it will be very slow, continue?", L"Warning", MB_ICONEXCLAMATION | MB_YESNO ) != IDYES )
            return E_FAIL;
    }

    bFirstOnCreateDevice = false;

    ID3D11DeviceContext* pd3dImmediateContext = DXUTGetD3D11DeviceContext();
    V_RETURN( g_DialogResourceManager.OnD3D11CreateDevice( pd3dDevice, pd3dImmediateContext ) );
    V_RETURN( g_D3DSettingsDlg.OnD3D11CreateDevice( pd3dDevice ) );
    g_pTxtHelper = new CDXUTTextHelper( pd3dDevice, pd3dImmediateContext, &g_DialogResourceManager, 15 );
    
    ID3DBlob* pBlob = NULL;

    V_RETURN( CompileShaderFromFile( L"MandelbulbPS.hlsl", "MandelbulbPS", "ps_4_0", &pBlob ) );
    V_RETURN( pd3dDevice->CreatePixelShader( pBlob->GetBufferPointer(), pBlob->GetBufferSize(), NULL, &g_pMandelbulbPS ) );  
    SAFE_RELEASE( pBlob );
    DXUT_SetDebugName( g_pMandelbulbPS, "MandelbulbPS" );

    V_RETURN( CompileShaderFromFile( L"MandelboxPS.hlsl", "MandelboxPS", "ps_4_0", &pBlob ) );
    V_RETURN( pd3dDevice->CreatePixelShader( pBlob->GetBufferPointer(), pBlob->GetBufferSize(), NULL, &g_pMandelboxPS ) );  
    SAFE_RELEASE( pBlob );
    DXUT_SetDebugName( g_pMandelboxPS, "MandelboxPS" );

    V_RETURN( CompileShaderFromFile( L"QuadVS.hlsl", "QuadVS", "vs_4_0", &pBlob ) );
    V_RETURN( pd3dDevice->CreateVertexShader( pBlob->GetBufferPointer(), pBlob->GetBufferSize(), NULL, &g_pQuadVS ) );
    DXUT_SetDebugName( g_pQuadVS, "QuadVS" );

    const D3D11_INPUT_ELEMENT_DESC quadlayout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    V_RETURN( pd3dDevice->CreateInputLayout( quadlayout, 2, pBlob->GetBufferPointer(), pBlob->GetBufferSize(), &g_pQuadLayout ) );
    SAFE_RELEASE( pBlob );
    DXUT_SetDebugName( g_pQuadLayout, "Quad" );

    // Setup constant buffers
    D3D11_BUFFER_DESC Desc;
    Desc.Usage = D3D11_USAGE_DYNAMIC;
    Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    Desc.MiscFlags = 0;
    Desc.ByteWidth = sizeof( CbMandelbulb );
    V_RETURN( pd3dDevice->CreateBuffer( &Desc, NULL, &g_pcbMandelbulb ) );
    DXUT_SetDebugName( g_pcbMandelbulb, "CbMandelbulb" );

    // Samplers
    D3D11_SAMPLER_DESC SamplerDesc;
    ZeroMemory( &SamplerDesc, sizeof(SamplerDesc) );
    SamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    SamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    SamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    SamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    V_RETURN( pd3dDevice->CreateSamplerState( &SamplerDesc, &g_pSampleStateLinear ) );
    DXUT_SetDebugName( g_pSampleStateLinear, "Linear" );

    SamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    V_RETURN( pd3dDevice->CreateSamplerState( &SamplerDesc, &g_pSampleStatePoint ) );
    DXUT_SetDebugName( g_pSampleStatePoint, "Point" );

    // Create a screen quad for render to texture operations
    SCREEN_VERTEX svQuad[4];
    svQuad[0].pos = D3DXVECTOR4( -1.0f, 1.0f, 0.5f, 1.0f );
    svQuad[0].tex = D3DXVECTOR2( 0.0f, 0.0f );
    svQuad[1].pos = D3DXVECTOR4( 1.0f, 1.0f, 0.5f, 1.0f );
    svQuad[1].tex = D3DXVECTOR2( 1.0f, 0.0f );
    svQuad[2].pos = D3DXVECTOR4( -1.0f, -1.0f, 0.5f, 1.0f );
    svQuad[2].tex = D3DXVECTOR2( 0.0f, 1.0f );
    svQuad[3].pos = D3DXVECTOR4( 1.0f, -1.0f, 0.5f, 1.0f );
    svQuad[3].tex = D3DXVECTOR2( 1.0f, 1.0f );

    D3D11_BUFFER_DESC vbdesc =
    {
        4 * sizeof( SCREEN_VERTEX ),
        D3D11_USAGE_DEFAULT,
        D3D11_BIND_VERTEX_BUFFER,
        0,
        0
    };
    D3D11_SUBRESOURCE_DATA InitData;
    InitData.pSysMem = svQuad;
    InitData.SysMemPitch = 0;
    InitData.SysMemSlicePitch = 0;
    V_RETURN( pd3dDevice->CreateBuffer( &vbdesc, &InitData, &g_pScreenQuadVB ) );
    DXUT_SetDebugName( g_pScreenQuadVB, "ScreenQuad" );

    // Setup the camera   
    //D3DXVECTOR3 vecEye( 0.0f, 0.5f, -3.0f );
    D3DXVECTOR3 vecEye( 3.0f, 0.0, 0.0f );
    D3DXVECTOR3 vecAt ( 0.0f, 0.0f, 0.0f );
    g_pCamera->SetViewParams( &vecEye, &vecAt );

    return S_OK;
}

HRESULT CALLBACK OnD3D11ResizedSwapChain( ID3D11Device* pd3dDevice, IDXGISwapChain* pSwapChain,
                                         const DXGI_SURFACE_DESC* pBackBufferSurfaceDesc, void* pUserContext )
{
    HRESULT hr;

    V_RETURN( g_DialogResourceManager.OnD3D11ResizedSwapChain( pd3dDevice, pBackBufferSurfaceDesc ) );
    V_RETURN( g_D3DSettingsDlg.OnD3D11ResizedSwapChain( pd3dDevice, pBackBufferSurfaceDesc ) );

    // Setup the camera's projection parameters
    float fAspectRatio = pBackBufferSurfaceDesc->Width / ( FLOAT )pBackBufferSurfaceDesc->Height;
    g_pCamera->SetProjParams( D3DX_PI / 4, fAspectRatio, 0.1f, 5000.0f );
#ifdef ENABLE_MODEL_VIEW_CAMERA
    g_MVCamera.SetWindow( pBackBufferSurfaceDesc->Width, pBackBufferSurfaceDesc->Height );
#endif

    g_HUD.SetLocation( pBackBufferSurfaceDesc->Width - 170, 0 );
    g_HUD.SetSize( 170, 170 );
    g_SampleUI.SetLocation( pBackBufferSurfaceDesc->Width - 170, pBackBufferSurfaceDesc->Height - 240 );
    g_SampleUI.SetSize( 150, 110 );

    return S_OK;
}

template <class T>
void SWAP( T* &x, T* &y )
{
    T* temp = x;
    x = y;
    y = temp;
}

void DrawFullScreenQuad11( ID3D11DeviceContext* pd3dImmediateContext, 
                           ID3D11PixelShader* pPS,
                           UINT Width, UINT Height )
{
    // Save the old viewport
    D3D11_VIEWPORT vpOld[D3D11_VIEWPORT_AND_SCISSORRECT_MAX_INDEX];
    UINT nViewPorts = 1;
    pd3dImmediateContext->RSGetViewports( &nViewPorts, vpOld );

    // Setup the viewport to match the backbuffer
    D3D11_VIEWPORT vp;
    vp.Width = (float)Width;
    vp.Height = (float)Height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    pd3dImmediateContext->RSSetViewports( 1, &vp );

    UINT strides = sizeof( SCREEN_VERTEX );
    UINT offsets = 0;
    ID3D11Buffer* pBuffers[1] = { g_pScreenQuadVB };

    pd3dImmediateContext->IASetInputLayout( g_pQuadLayout );
    pd3dImmediateContext->IASetVertexBuffers( 0, 1, pBuffers, &strides, &offsets );
    pd3dImmediateContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP );

    pd3dImmediateContext->VSSetShader( g_pQuadVS, NULL, 0 );
    pd3dImmediateContext->PSSetShader( pPS, NULL, 0 );
    pd3dImmediateContext->Draw( 4, 0 );

    // Restore the Old viewport
    pd3dImmediateContext->RSSetViewports( nViewPorts, vpOld );
}

float GaussianDistribution( float x, float y, float rho )
{
    float g = 1.0f / sqrtf( 2.0f * D3DX_PI * rho * rho );
    g *= expf( -( x * x + y * y ) / ( 2 * rho * rho ) );

    return g;
}

//--------------------------------------------------------------------------------------
// Helper function which makes CS invocation more convenient
//--------------------------------------------------------------------------------------
template<typename T>
void RunComputeShader( ID3D11DeviceContext* pd3dImmediateContext,
                       ID3D11ComputeShader* pComputeShader,
                       UINT nNumViews, ID3D11ShaderResourceView** pShaderResourceViews, 
                       ID3D11Buffer* pCBCS, T* pCSData, DWORD dwNumDataBytes,
                       ID3D11UnorderedAccessView* pUnorderedAccessView,
                       UINT X, UINT Y, UINT Z )
{
    HRESULT hr = S_OK;
    
    pd3dImmediateContext->CSSetShader( pComputeShader, NULL, 0 );
    pd3dImmediateContext->CSSetShaderResources( 0, nNumViews, pShaderResourceViews );
    pd3dImmediateContext->CSSetUnorderedAccessViews( 0, 1, &pUnorderedAccessView, NULL );
    if ( pCBCS )
    {
        CopyToBuffer<T>(pd3dImmediateContext, pCSData, pCBCS);
        ID3D11Buffer* ppCB[1] = { pCBCS };
        pd3dImmediateContext->CSSetConstantBuffers( 0, 1, ppCB );
    }

    pd3dImmediateContext->Dispatch( X, Y, Z );

    ID3D11UnorderedAccessView* ppUAViewNULL[1] = { NULL };
    pd3dImmediateContext->CSSetUnorderedAccessViews( 0, 1, ppUAViewNULL, NULL );

    ID3D11ShaderResourceView* ppSRVNULL[3] = { NULL, NULL, NULL };
    pd3dImmediateContext->CSSetShaderResources( 0, 3, ppSRVNULL );
    ID3D11Buffer* ppBufferNULL[1] = { NULL };
    pd3dImmediateContext->CSSetConstantBuffers( 0, 1, ppBufferNULL );
}

//--------------------------------------------------------------------------------------
// Debug function which copies a GPU buffer to a CPU readable buffer
//--------------------------------------------------------------------------------------
ID3D11Buffer* CreateAndCopyToDebugBuf( ID3D11Device* pDevice, ID3D11DeviceContext* pd3dImmediateContext, ID3D11Buffer* pBuffer )
{
    ID3D11Buffer* debugbuf = NULL;

    D3D11_BUFFER_DESC desc;
    ZeroMemory( &desc, sizeof(desc) );
    pBuffer->GetDesc( &desc );
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.MiscFlags = 0;
    pDevice->CreateBuffer(&desc, NULL, &debugbuf);
    DXUT_SetDebugName( debugbuf, "Debug" );

    pd3dImmediateContext->CopyResource( debugbuf, pBuffer );

    return debugbuf;
}

void CALLBACK OnD3D11FrameRender( ID3D11Device* pd3dDevice, ID3D11DeviceContext* pd3dImmediateContext, double fTime,
                                 float fElapsedTime, void* pUserContext )
{
    // If the settings dialog is being shown, then render it instead of rendering the app's scene
    if( g_D3DSettingsDlg.IsActive() )
    {
        g_D3DSettingsDlg.OnRender( fElapsedTime );
        return;
    }

    const DXGI_SURFACE_DESC* pBackBufferDesc = DXUTGetDXGIBackBufferSurfaceDesc();

    // Store off original render target, this is the back buffer of the swap chain
    ID3D11RenderTargetView* pOrigRTV = NULL;
    ID3D11DepthStencilView* pOrigDSV = NULL;
    pd3dImmediateContext->OMGetRenderTargets( 1, &pOrigRTV, &pOrigDSV );

    float ClearColor[4] = { 0.3f, 0.3f, 0.3f, 1.0f } ; // red, green, blue, alpha
    
    pd3dImmediateContext->ClearRenderTargetView( pOrigRTV, ClearColor );
    pd3dImmediateContext->ClearDepthStencilView( pOrigDSV, D3D11_CLEAR_DEPTH, 1.0, 0 );      

    D3DXMATRIXA16 mWorld;
    D3DXMATRIXA16 mView;
    D3DXMATRIXA16 mProj;
    D3DXMATRIXA16 mWorldViewProjection;

    float distEst = (float)DE(*g_pCamera->GetEyePt());
    // Get the projection & view matrix from the camera class
#ifdef ENABLE_MODEL_VIEW_CAMERA
    mWorld = *g_MVCamera.GetWorldMatrix();
    g_MVCamera.SetScale(distEst * 0.5f);
#else
    mWorld = *g_FPCamera.GetWorldMatrix();
#endif
    mView = *g_pCamera->GetViewMatrix();
    mProj = *g_pCamera->GetProjMatrix();
    
    mWorldViewProjection = mWorld * mView * mProj;

    g_cbMandelbulb.mProj = mProj;
    D3DXMatrixInverse(&g_cbMandelbulb.mInvView, NULL, &mView);
    D3DXMatrixInverse(&g_cbMandelbulb.mInvWorld, NULL, &mWorld);
    g_cbMandelbulb.dist = distEst;
    CopyToBuffer<CbMandelbulb>(pd3dImmediateContext, (CbMandelbulb*)&g_cbMandelbulb, g_pcbMandelbulb);

    if ( g_bPostProcessON )
    {
    }

    ID3D11RenderTargetView* aRTViews[ 1 ] = { pOrigRTV };
    pd3dImmediateContext->OMSetRenderTargets( 1, aRTViews, pOrigDSV );

    // Tone-mapping
    if ( g_ePostProcessMode == PM_COMPUTE_SHADER )
    {
        // TODO Compute shader implementation
    }
    else //if ( g_ePostProcessMode == PM_PIXEL_SHADER )
    {
        ID3D11ShaderResourceView* aRViews[ 1 ] = { NULL };
        pd3dImmediateContext->PSSetShaderResources( 0, 1, aRViews );

        ID3D11Buffer* ppCB[1] = { g_pcbMandelbulb };
        pd3dImmediateContext->PSSetConstantBuffers( 0, 1, ppCB );

        ID3D11SamplerState* aSamplers[] = { g_pSampleStatePoint, g_pSampleStateLinear };
        pd3dImmediateContext->PSSetSamplers( 0, 2, aSamplers );

        //DrawFullScreenQuad11( pd3dImmediateContext, g_pMandelboxPS, pBackBufferDesc->Width, pBackBufferDesc->Height );
        DrawFullScreenQuad11( pd3dImmediateContext, g_pMandelbulbPS, pBackBufferDesc->Width, pBackBufferDesc->Height );
    }

    ID3D11ShaderResourceView* ppSRVNULL[1] = { NULL };
    pd3dImmediateContext->PSSetShaderResources( 0, 1, ppSRVNULL );

    SAFE_RELEASE( pOrigRTV );
    SAFE_RELEASE( pOrigDSV );

    DXUT_BeginPerfEvent( DXUT_PERFEVENTCOLOR, L"HUD / Stats" );
    g_HUD.OnRender( fElapsedTime );
    g_SampleUI.OnRender( fElapsedTime );
    RenderText();
    DXUT_EndPerfEvent();
}

//--------------------------------------------------------------------------------------
// Release D3D11 resources created in OnD3D11CreateDevice 
//--------------------------------------------------------------------------------------
void CALLBACK OnD3D11DestroyDevice( void* pUserContext )
{
    g_DialogResourceManager.OnD3D11DestroyDevice();
    g_D3DSettingsDlg.OnD3D11DestroyDevice();
    DXUTGetGlobalResourceCache().OnDestroyDevice();
    SAFE_DELETE( g_pTxtHelper );

    SAFE_RELEASE( g_pMandelbulbPS );
    SAFE_RELEASE( g_pMandelboxPS );
    SAFE_RELEASE( g_pcbMandelbulb );

    SAFE_RELEASE( g_pSampleStateLinear );
    SAFE_RELEASE( g_pSampleStatePoint );

    SAFE_RELEASE( g_pScreenQuadVB );
    SAFE_RELEASE( g_pQuadVS );
    SAFE_RELEASE( g_pQuadLayout );
}

void CALLBACK OnD3D11ReleasingSwapChain( void* pUserContext )
{
    g_DialogResourceManager.OnD3D11ReleasingSwapChain();
}
