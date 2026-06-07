#include "pch.h"
#include "hooks.h"
#include "MinHook.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx12.h"
#include "gui.h"
#include "features.h"
#include "Instances.hpp"
#include <d3d11.h>     // still used for dummy swap chain vtable capture in Initialize()
#include <d3d12.h>
#include <dxgi1_4.h>
#include <algorithm>
#include <unordered_set>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3d12.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// --- Globals ---
static bool    g_ShowMenu   = true;
static HWND    g_GameWindow = NULL;
static WNDPROC oWndProc     = NULL;

// --- D3D12 Globals ---
static const UINT                  MAX_BUFFERS = 8;
static ID3D12Device*               g_pd3d12Device             = nullptr;
static ID3D12CommandQueue*         g_pd3dCmdQueue             = nullptr;  // game's CQ, captured via ECL hook
static ID3D12DescriptorHeap*       g_pd3dSrvHeap              = nullptr;
static ID3D12DescriptorHeap*       g_pd3dRtvHeap              = nullptr;
static ID3D12CommandAllocator*     g_commandAllocators[MAX_BUFFERS] = {};
static ID3D12GraphicsCommandList*  g_pd3dCommandList          = nullptr;
static ID3D12Resource*             g_renderTargets[MAX_BUFFERS] = {};
static D3D12_CPU_DESCRIPTOR_HANDLE g_rtvHandles[MAX_BUFFERS]   = {};
static UINT                        g_bufferCount              = 0;

// --- Hook typedefs ---
typedef HRESULT(APIENTRY* Present_t)(IDXGISwapChain*, UINT, UINT);
static Present_t oPresent = nullptr;
typedef HRESULT(APIENTRY* Present1_t)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
static Present1_t oPresent1 = nullptr;
typedef HRESULT(APIENTRY* ResizeBuffers_t)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
static ResizeBuffers_t oResizeBuffers = nullptr;
typedef void(APIENTRY* ExecuteCommandLists_t)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
static ExecuteCommandLists_t oExecuteCommandLists = nullptr;
static ProcessEvent_t oProcessEvent = nullptr;

// -----------------------------------------------------------------------
LRESULT WINAPI hkWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_KEYUP && wParam == VK_F8)
	{
		g_ShowMenu = !g_ShowMenu;
		ImGui::GetIO().MouseDrawCursor = g_ShowMenu;
	}
	if (g_ShowMenu) ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
	return CallWindowProc(oWndProc, hWnd, msg, wParam, lParam);
}

// Captures the game's D3D12 command queue (first call = main render queue)
void APIENTRY hkExecuteCommandLists(ID3D12CommandQueue* pCmdQueue, UINT NumCmdLists, ID3D12CommandList* const* ppCmdLists)
{
	if (!g_pd3dCmdQueue)
	{
		// Only capture DIRECT queues (not copy/compute)
		D3D12_COMMAND_QUEUE_DESC desc = pCmdQueue->GetDesc();
		if (desc.Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
		{
			g_pd3dCmdQueue = pCmdQueue;
			std::cout << "[+] D3D12 CommandQueue captured: 0x" << std::hex << (uintptr_t)pCmdQueue << std::dec << std::endl;
		}
	}
	return oExecuteCommandLists(pCmdQueue, NumCmdLists, ppCmdLists);
}

// -----------------------------------------------------------------------
// Release / recreate RTV resources around ResizeBuffers
static void ReleaseRenderTargets()
{
	for (UINT i = 0; i < MAX_BUFFERS; i++)
	{
		if (g_renderTargets[i]) { g_renderTargets[i]->Release(); g_renderTargets[i] = nullptr; }
	}
}

static void RecreateRenderTargets(IDXGISwapChain* pSwapChain)
{
	if (!g_pd3d12Device || !g_pd3dRtvHeap) return;
	UINT rtvSize = g_pd3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_pd3dRtvHeap->GetCPUDescriptorHandleForHeapStart();
	for (UINT i = 0; i < g_bufferCount; i++)
	{
		g_rtvHandles[i] = rtvHandle;
		pSwapChain->GetBuffer(i, IID_PPV_ARGS(&g_renderTargets[i]));
		if (g_renderTargets[i])
			g_pd3d12Device->CreateRenderTargetView(g_renderTargets[i], nullptr, rtvHandle);
		rtvHandle.ptr += rtvSize;
	}
}

// Hook vtable[13] — IDXGISwapChain::ResizeBuffers
// UE4 calls this on fullscreen toggle / window resize.
// DXGI requires ALL back-buffer references released before the call.
HRESULT APIENTRY hkResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
	ReleaseRenderTargets();
	HRESULT hr = oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
	if (SUCCEEDED(hr))
		RecreateRenderTargets(pSwapChain);
	return hr;
}

// -----------------------------------------------------------------------
// Shared D3D12 ImGui init + render — called from whichever Present hook fires
static void DoRender(IDXGISwapChain* pSwapChain)
{
	static bool bInitialized = false;

	if (!bInitialized)
	{
		if (!g_pd3dCmdQueue) return;  // wait for ECL hook to capture the command queue

		if (FAILED(pSwapChain->GetDevice(__uuidof(ID3D12Device), (void**)&g_pd3d12Device)))
		{
			static bool warned = false;
			if (!warned) { warned = true; std::cout << "[-] GetDevice(ID3D12Device) failed." << std::endl; }
			return;
		}

		DXGI_SWAP_CHAIN_DESC scDesc = {};
		pSwapChain->GetDesc(&scDesc);
		g_GameWindow  = scDesc.OutputWindow;
		g_bufferCount = scDesc.BufferCount;
		DXGI_FORMAT rtFormat = scDesc.BufferDesc.Format;

		if (g_bufferCount == 0 || g_bufferCount > MAX_BUFFERS)
		{
			std::cout << "[-] Unexpected buffer count: " << g_bufferCount << std::endl;
			return;
		}

		// RTV descriptor heap
		{
			D3D12_DESCRIPTOR_HEAP_DESC d = {};
			d.NumDescriptors = g_bufferCount;
			d.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			if (FAILED(g_pd3d12Device->CreateDescriptorHeap(&d, IID_PPV_ARGS(&g_pd3dRtvHeap))))
			{ std::cout << "[-] RTV heap creation failed." << std::endl; return; }
		}

		// SRV descriptor heap (ImGui font texture)
		{
			D3D12_DESCRIPTOR_HEAP_DESC d = {};
			d.NumDescriptors = 1;
			d.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			d.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			if (FAILED(g_pd3d12Device->CreateDescriptorHeap(&d, IID_PPV_ARGS(&g_pd3dSrvHeap))))
			{ std::cout << "[-] SRV heap creation failed." << std::endl; return; }
		}

		// Create one RTV per back buffer
		UINT rtvSize = g_pd3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_pd3dRtvHeap->GetCPUDescriptorHandleForHeapStart();
		for (UINT i = 0; i < g_bufferCount; i++)
		{
			g_rtvHandles[i] = rtvHandle;
			if (FAILED(pSwapChain->GetBuffer(i, IID_PPV_ARGS(&g_renderTargets[i]))))
			{ std::cout << "[-] GetBuffer(" << i << ") failed." << std::endl; return; }
			g_pd3d12Device->CreateRenderTargetView(g_renderTargets[i], nullptr, rtvHandle);
			rtvHandle.ptr += rtvSize;
		}

		// Command allocators (one per back buffer)
		for (UINT i = 0; i < g_bufferCount; i++)
		{
			if (FAILED(g_pd3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_commandAllocators[i]))))
			{ std::cout << "[-] CreateCommandAllocator(" << i << ") failed." << std::endl; return; }
		}

		// Command list
		if (FAILED(g_pd3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
			g_commandAllocators[0], nullptr, IID_PPV_ARGS(&g_pd3dCommandList))))
		{ std::cout << "[-] CreateCommandList failed." << std::endl; return; }
		g_pd3dCommandList->Close();

		// ImGui init
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.MouseDrawCursor = g_ShowMenu;
		ImGui::StyleColorsDark();
		ImGui_ImplWin32_Init(g_GameWindow);

		// Use new InitInfo API so CommandQueue is set — legacy API clears
		// ImGuiBackendFlags_RendererHasTextures and leaves atlas->Builder null, crashing NewFrame.
		ImGui_ImplDX12_InitInfo dx12Init = {};
		dx12Init.Device            = g_pd3d12Device;
		dx12Init.CommandQueue      = g_pd3dCmdQueue;
		dx12Init.NumFramesInFlight = (int)g_bufferCount;
		dx12Init.RTVFormat         = rtFormat;
		dx12Init.DSVFormat         = DXGI_FORMAT_UNKNOWN;
		dx12Init.SrvDescriptorHeap = g_pd3dSrvHeap;
		// Single-descriptor legacy mode: set these so InitLegacySingleDescriptorMode
		// wires up the alloc/free callbacks (SrvDescriptorAllocFn stays null → triggers it).
		dx12Init.LegacySingleSrvCpuDescriptor = g_pd3dSrvHeap->GetCPUDescriptorHandleForHeapStart();
		dx12Init.LegacySingleSrvGpuDescriptor = g_pd3dSrvHeap->GetGPUDescriptorHandleForHeapStart();
		ImGui_ImplDX12_Init(&dx12Init);

		oWndProc = (WNDPROC)SetWindowLongPtr(g_GameWindow, GWLP_WNDPROC, (LONG_PTR)hkWndProc);
		bInitialized = true;
		std::cout << "[+] ImGui D3D12 Initialized!" << std::endl;
		std::cout << "[+] -> Press F8 to toggle menu." << std::endl;
		return;  // render starting next frame
	}

	// Per-frame render
	IDXGISwapChain3* sc3 = nullptr;
	if (FAILED(pSwapChain->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&sc3))) return;
	UINT bbIdx = sc3->GetCurrentBackBufferIndex();
	sc3->Release();
	if (bbIdx >= g_bufferCount) return;

	g_commandAllocators[bbIdx]->Reset();
	g_pd3dCommandList->Reset(g_commandAllocators[bbIdx], nullptr);

	// Barrier: PRESENT → RENDER_TARGET
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource   = g_renderTargets[bbIdx];
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	g_pd3dCommandList->ResourceBarrier(1, &barrier);

	g_pd3dCommandList->OMSetRenderTargets(1, &g_rtvHandles[bbIdx], FALSE, nullptr);
	g_pd3dCommandList->SetDescriptorHeaps(1, &g_pd3dSrvHeap);

	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
	if (g_ShowMenu) GUI::Render();
	ImGui::Render();
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_pd3dCommandList);

	// Barrier: RENDER_TARGET → PRESENT
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
	g_pd3dCommandList->ResourceBarrier(1, &barrier);
	g_pd3dCommandList->Close();

	ID3D12CommandList* pCmds[] = { g_pd3dCommandList };
	g_pd3dCmdQueue->ExecuteCommandLists(1, pCmds);
}

// -----------------------------------------------------------------------
HRESULT APIENTRY hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{
	DoRender(pSwapChain);
	return oPresent(pSwapChain, SyncInterval, Flags);
}

HRESULT APIENTRY hkPresent1(IDXGISwapChain1* pSwapChain, UINT SyncInterval, UINT Flags, const DXGI_PRESENT_PARAMETERS* pPresentParameters)
{
	DoRender(static_cast<IDXGISwapChain*>(pSwapChain));
	return oPresent1(pSwapChain, SyncInterval, Flags, pPresentParameters);
}

// -----------------------------------------------------------------------
void Hooks::Initialize()
{
	void* pPresentAddr    = nullptr;
	void* pPresent1Addr   = nullptr;
	void* pResizeBufAddr  = nullptr;
	void* pECLAddr        = nullptr;

	// --- Dummy D3D11 swap chain → DXGI vtable addresses ---
	{
		WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, DefWindowProc, 0L, 0L,
			GetModuleHandle(NULL), NULL, NULL, NULL, NULL, "DX_ETB_Dummy", NULL };
		RegisterClassEx(&wc);
		HWND hWnd = CreateWindow(wc.lpszClassName, NULL, WS_OVERLAPPEDWINDOW,
			100, 100, 300, 300, NULL, NULL, wc.hInstance, NULL);

		ID3D11Device* pD11Dev = nullptr;
		D3D_FEATURE_LEVEL fl;
		if (SUCCEEDED(D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
			NULL, 0, D3D11_SDK_VERSION, &pD11Dev, &fl, NULL)))
		{
			IDXGIDevice*   pDXGIDevice = nullptr;
			IDXGIAdapter*  pAdapter    = nullptr;
			IDXGIFactory2* pFactory2   = nullptr;
			pD11Dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&pDXGIDevice);
			pDXGIDevice->GetAdapter(&pAdapter);
			pAdapter->GetParent(__uuidof(IDXGIFactory2), (void**)&pFactory2);

			DXGI_SWAP_CHAIN_DESC1 sd1 = {};
			sd1.BufferCount = 2; sd1.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			sd1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			sd1.SampleDesc.Count = 1; sd1.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

			IDXGISwapChain1* pChain = nullptr;
			if (SUCCEEDED(pFactory2->CreateSwapChainForHwnd(pD11Dev, hWnd, &sd1, nullptr, nullptr, &pChain)))
			{
				void** vtbl   = *(void***)pChain;
				pPresentAddr   = vtbl[8];
				pResizeBufAddr = vtbl[13];
				pPresent1Addr  = vtbl[22];
				std::cout << "[+] Present      (idx  8): 0x" << std::hex << (uintptr_t)pPresentAddr   << std::endl;
				std::cout << "[+] ResizeBuffers(idx 13): 0x" << (uintptr_t)pResizeBufAddr << std::endl;
				std::cout << "[+] Present1     (idx 22): 0x" << (uintptr_t)pPresent1Addr  << std::dec << std::endl;
				pChain->Release();
			}
			pFactory2->Release(); pAdapter->Release(); pDXGIDevice->Release(); pD11Dev->Release();
		}
		DestroyWindow(hWnd);
		UnregisterClass(wc.lpszClassName, wc.hInstance);
	}

	// --- Dummy D3D12 command queue → ExecuteCommandLists vtable address ---
	{
		ID3D12Device* pD12Dev = nullptr;
		if (SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&pD12Dev))))
		{
			D3D12_COMMAND_QUEUE_DESC cqDesc = {};
			ID3D12CommandQueue* pDummyCQ = nullptr;
			if (SUCCEEDED(pD12Dev->CreateCommandQueue(&cqDesc, IID_PPV_ARGS(&pDummyCQ))))
			{
				pECLAddr = (*(void***)pDummyCQ)[10];  // ExecuteCommandLists is vtable[10]
				std::cout << "[+] ExecuteCommandLists (idx 10): 0x" << std::hex << (uintptr_t)pECLAddr << std::dec << std::endl;
				pDummyCQ->Release();
			}
			pD12Dev->Release();
		}
	}

	if (!pPresentAddr) { std::cout << "[-] Failed to get Present address!" << std::endl; return; }

	// --- Install hooks ---
	if (MH_Initialize() != MH_OK) { std::cout << "[-] MinHook init failed!" << std::endl; return; }

	if (MH_CreateHook(pPresentAddr, &hkPresent, reinterpret_cast<void**>(&oPresent)) == MH_OK)
		std::cout << "[+] Present hook created." << std::endl;
	else
		std::cout << "[-] CreateHook (Present) failed!" << std::endl;

	if (pResizeBufAddr && MH_CreateHook(pResizeBufAddr, &hkResizeBuffers, reinterpret_cast<void**>(&oResizeBuffers)) == MH_OK)
		std::cout << "[+] ResizeBuffers hook created." << std::endl;

	if (pPresent1Addr && MH_CreateHook(pPresent1Addr, &hkPresent1, reinterpret_cast<void**>(&oPresent1)) == MH_OK)
		std::cout << "[+] Present1 hook created." << std::endl;

	if (pECLAddr)
	{
		if (MH_CreateHook(pECLAddr, &hkExecuteCommandLists, reinterpret_cast<void**>(&oExecuteCommandLists)) != MH_OK)
			std::cout << "[-] CreateHook (ExecuteCommandLists) failed!" << std::endl;
		else
			std::cout << "[+] ExecuteCommandLists hook created." << std::endl;
	}

	UWorld* world = UWorld::GetWorld();
	if (world && world->OwningGameInstance)
	{
		uintptr_t** VTable = *(uintptr_t***)world->OwningGameInstance;
		void* pPE = (void*)VTable[0x44];
		if (MH_CreateHook(pPE, &Hooks::hkProcessEvent, reinterpret_cast<void**>(&oProcessEvent)) != MH_OK)
			std::cout << "[-] CreateHook (ProcessEvent) failed!" << std::endl;
		else
			std::cout << "[+] ProcessEvent hook created successfully!" << std::endl;
	}
	else
	{
		std::cout << "[-] Failed to get UGameInstance." << std::endl;
	}

	if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) { std::cout << "[-] EnableHook failed!" << std::endl; return; }
	std::cout << "[+] Hooks initialized successfully!" << std::endl;
}

// -----------------------------------------------------------------------
void Hooks::Shutdown()
{
	if (oWndProc && g_GameWindow)
		SetWindowLongPtr(g_GameWindow, GWLP_WNDPROC, (LONG_PTR)oWndProc);

	MH_DisableHook(MH_ALL_HOOKS);
	MH_RemoveHook(MH_ALL_HOOKS);

	if (g_pd3d12Device)
	{
		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
	}

	for (UINT i = 0; i < g_bufferCount; i++)
	{
		if (g_renderTargets[i])    { g_renderTargets[i]->Release();    g_renderTargets[i]    = nullptr; }
		if (g_commandAllocators[i]){ g_commandAllocators[i]->Release(); g_commandAllocators[i] = nullptr; }
	}
	if (g_pd3dCommandList) { g_pd3dCommandList->Release(); g_pd3dCommandList = nullptr; }
	if (g_pd3dSrvHeap)     { g_pd3dSrvHeap->Release();     g_pd3dSrvHeap     = nullptr; }
	if (g_pd3dRtvHeap)     { g_pd3dRtvHeap->Release();     g_pd3dRtvHeap     = nullptr; }

	MH_Uninitialize();
}

// -----------------------------------------------------------------------
void Hooks::hkProcessEvent(SDK::UObject* pThis, SDK::UFunction* Function, void* Parms)
{
	if (pThis && Function)
	{
		std::string functionName = Function->GetFullName();

		std::string lowerFunctionName = functionName;
		std::transform(lowerFunctionName.begin(), lowerFunctionName.end(), lowerFunctionName.begin(),
			[](unsigned char c) { return std::tolower(c); });

		if (functionName == "Function BPCharacter_Demo.BPCharacter_Demo_C.ReceiveTick")
		{
			Instances::Update();

			ABPCharacter_Demo_C* pLocalPlayer = Features::GetPawn();
			Features::OnPawnChange(pLocalPlayer);

			// Tick-driven features
			Features::FreezeMonstersTick();
			Features::NoAggroTick();

			if (pLocalPlayer)
			{
				Features::SpeedChanger(pLocalPlayer);
				Features::InfiniteStamina(pLocalPlayer);
				Features::InfiniteSanity(pLocalPlayer);
				Features::GodModeTick(pLocalPlayer);
				Features::DisguiseTick(pLocalPlayer);

				// --- Deferred one-shot actions (game thread only) ---
				if (Features::g_spawnItemPending)
				{
					Features::g_spawnItemPending = false;
					Features::ItemSpawner(Features::g_selectedItemIndex, Features::g_spawnCount);
				}
				if (Features::g_spawnCreaturePending)
				{
					Features::g_spawnCreaturePending = false;
					Features::SpawnCreature(Features::g_selectedCreatureIndex);
				}
				if (Features::g_killMonstersPending)
				{
					Features::g_killMonstersPending = false;
					Features::KillAllMonsters();
				}
				if (Features::g_teleportMonstersPending)
				{
					Features::g_teleportMonstersPending = false;
					Features::TeleportMonstersToMe();
				}
				if (Features::g_skinStealerDisguisePending)
				{
					Features::g_skinStealerDisguisePending = false;
					Features::SkinStealerDisguise();
				}
				if (Features::g_undisguisePending)
				{
					Features::g_undisguisePending = false;
					Features::Undisguise();
				}
				if (Features::g_teleportNearestItemPending)
				{
					Features::g_teleportNearestItemPending = false;
					Features::TeleportToNearestItem();
				}
				if (Features::g_teleportNearestMonsterPending)
				{
					Features::g_teleportNearestMonsterPending = false;
					Features::TeleportToNearestMonster();
				}
				if (Features::g_saveWaypointPending >= 0)
				{
					int slot = Features::g_saveWaypointPending;
					Features::g_saveWaypointPending = -1;
					Features::SaveWaypoint(slot);
				}
				if (Features::g_teleportWaypointPending >= 0)
				{
					int slot = Features::g_teleportWaypointPending;
					Features::g_teleportWaypointPending = -1;
					Features::TeleportToWaypoint(slot);
				}
				if (Features::g_forceDropItemsPending)
				{
					Features::g_forceDropItemsPending = false;
					Features::ForceDropAllItems();
				}
			}
		}

		if (functionName == "Function WB_Chat.WB_Chat_C.Tick")
		{
			Instances::Update();

			ABPCharacter_Demo_C* pLocalPlayer = Features::GetPawn();
			if (pLocalPlayer)
			{
				Features::PlayerFly(pLocalPlayer);
			}
			Features::ChatSpammer();
		}
	}

	return oProcessEvent(pThis, Function, Parms);
}
