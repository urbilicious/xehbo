#include "stdafx.h"

Detour<HRESULT> *XuiPNGTextureLoaderDetour = new Detour<HRESULT>;
Detour<PVOID> *MmDbgReadCheckDetour = new Detour<PVOID>;
CXamShutdownNavButton btnXeOnlineMenu;

namespace xbox {
	namespace hooks {
		namespace hud {
			HRESULT xuiRegisterClass(const XUIClass *pClass, HXUICLASS *phClass)
			{
				if (wcscmp(pClass->szClassName, L"ShutdownNavButton") == 0)
					btnXeOnlineMenu.Register();

				return XuiRegisterClass(pClass, phClass);
			}

			HRESULT xuiUnregisterClass(LPCWSTR szClassName)
			{
				if (wcscmp(szClassName, L"ShutdownNavButton") == 0)
					btnXeOnlineMenu.Unregister();

				return XuiUnregisterClass(szClassName);
			}

			HRESULT xuiSceneCreate(PWCHAR szBasePath, PWCHAR szScenePath, void* pvInitData, HXUIOBJ* phScene)
			{
				//xbox::utilities::log("XuiCreateScene: Loading %ls", szScenePath);

				HRESULT result = XuiSceneCreate(szBasePath, szScenePath, pvInitData, phScene);

				if (wcscmp(szScenePath, L"GuideMain.xur") == 0)
				{
					server::main::updateUserTime();

					// set our message
					std::wstring wHudMessage = global::isAuthed ? L"Status: Enabled" : L"Status: Disabled";
					wHudMessage.append(L" | " + (global::wStatusMsg.empty() ? global::wTimeMsg.str() : global::wStatusMsg));

					// get Tabscene
					HXUIOBJ hTabscene;
					XuiElementGetFirstChild(*phScene, &hTabscene);

					// set our info
					HXUIOBJ txtTimeRemaining;
					XuiCreateObject(XUI_CLASS_TEXT, &txtTimeRemaining);
					XuiElementSetBounds(txtTimeRemaining, 375.0, 21.0);
					XuiElementSetPosition(txtTimeRemaining, &D3DXVECTOR3(243, 375, 0));

					XUIElementPropVal propVal; DWORD propId;
					propVal.SetColorVal(0xFFEBEBEB);
					XuiObjectGetPropertyId(txtTimeRemaining, L"TextColor", &propId);
					XuiObjectSetProperty(txtTimeRemaining, propId, 0, &propVal);

					// Set font size
					propVal.SetVal(12.0f);
					XuiObjectGetPropertyId(txtTimeRemaining, L"PointSize", &propId);
					XuiObjectSetProperty(txtTimeRemaining, propId, 0, &propVal);

					// set text and add to scene
					XuiTextElementSetText(txtTimeRemaining, wHudMessage.c_str());
					XuiElementAddChild(hTabscene, txtTimeRemaining);
				}
				else if (wcsstr(szScenePath, L"SettingsTabSigned") != 0)
				{
					HXUIOBJ btnXamShutdown;
					XuiElementGetChildById(*phScene, L"btnXamShutdown", &btnXamShutdown);
					btnXeOnlineMenu.Attach(btnXamShutdown);
				}

				return result;
			}

			HRESULT xuiPNGTextureLoader(IXuiDevice *pDevice, LPCWSTR szFileName, XUIImageInfo *pImageInfo, IDirect3DTexture9 **ppTex)
			{
				//xbox::utilities::log("XuiPNGTextureLoader: %ls", szFileName);
				WCHAR sectionFile[50];

				if (wcscmp(szFileName, L"skin://Blade_grey.png") == 0)
					XamBuildResourceLocator(global::modules::client, L"xui", L"Blade_grey.png", sectionFile, 50);
				else if (wcscmp(szFileName, L"xam://xenonLogo.png") == 0)
					XamBuildResourceLocator(global::modules::client, L"xui", L"xenonLogo.png", sectionFile, 50);

				return XuiPNGTextureLoaderDetour->CallOriginal(pDevice, wcslen(sectionFile) > 5 ? sectionFile : szFileName, pImageInfo, ppTex);
			}

			HRESULT setupCustomSkin(HANDLE hModule, PWCHAR wModuleName, PWCHAR const cdModule, PWCHAR hdRes, DWORD dwSize)
			{
				XamBuildResourceLocator(global::modules::client, L"xui", L"skin.xur", hdRes, 80);
				DWORD stat = XuiLoadVisualFromBinary(hdRes, 0);
				xbox::utilities::log("setupCustomSkin called, %X", stat);
				return stat;
			}

			HRESULT initialize(PLDR_DATA_TABLE_ENTRY ModuleHandle)
			{
				//static VOID(__cdecl *reinitVisual)() = (VOID(__cdecl *)())0x816CE528;
				//*(DWORD*)0x816CE570 = 0x4E800020;
				//reinitVisual();
				if (xbox::utilities::patchModuleImport(ModuleHandle, MODULE_XAM, 842, (DWORD)xuiRegisterClass) != S_OK) return E_FAIL;
				if (xbox::utilities::patchModuleImport(ModuleHandle, MODULE_XAM, 855, (DWORD)xuiSceneCreate) != S_OK) return E_FAIL;
				if (xbox::utilities::patchModuleImport(ModuleHandle, MODULE_XAM, 866, (DWORD)xuiUnregisterClass) != S_OK) return E_FAIL;
				return S_OK;
			}
		}

		namespace security {
			DWORD dwNumCIV = 0;

			DWORD xSecurityCreateProcess(DWORD dwHardwareThread)
			{
				return ERROR_SUCCESS;
			}

			VOID xSecurityCloseProcess() {}

			VOID __cdecl APCWorker(void* Arg1, void* Arg2, void* Arg3)
			{
				// Call our completion routine if we have one
				if (Arg2)
					((LPOVERLAPPED_COMPLETION_ROUTINE)Arg2)((DWORD)Arg3, 0, (LPOVERLAPPED)Arg1);

				dwNumCIV++;
			}

			DWORD xSecurityVerify(DWORD dwMilliseconds, LPOVERLAPPED lpOverlapped, LPOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine)
			{
				// Queue our completion routine
				if (lpCompletionRoutine)
				{
					NtQueueApcThread((HANDLE)-2, (PIO_APC_ROUTINE)APCWorker, lpOverlapped, (PIO_STATUS_BLOCK)lpCompletionRoutine, 0);
				}

				// All done
				return ERROR_SUCCESS;
			}

			DWORD xSecurityGetFailureInfo(PXSECURITY_FAILURE_INFORMATION pFailureInformation)
			{
				if (pFailureInformation->dwSize != 0x18) {
					dwNumCIV = 0;
					return ERROR_NOT_ENOUGH_MEMORY;
				}

				pFailureInformation->dwBlocksChecked = dwNumCIV;
				pFailureInformation->dwFailedReads = 0;
				pFailureInformation->dwFailedHashes = 0;
				pFailureInformation->dwTotalBlocks = dwNumCIV;
				pFailureInformation->fComplete = TRUE;
				return ERROR_SUCCESS;
			}

			DWORD xexGetProcedureAddress(HANDLE hand, DWORD dwOrdinal, PVOID* pvAddress)
			{
				// Check our module handle
				if (hand == global::modules::xam)
				{
					switch (dwOrdinal)
					{
					case 0x9BB:
						*pvAddress = xSecurityCreateProcess;
						return 0;
					case 0x9BC:
						*pvAddress = xSecurityCloseProcess;
						return 0;
					case 0x9BD:
						*pvAddress = xSecurityVerify;
						return 0;
					case 0x9BE:
						*pvAddress = xSecurityGetFailureInfo;
						return 0;
					}
				}

				// Call our real function if we aren't interested
				return XexGetProcedureAddress(hand, dwOrdinal, pvAddress);
			}
		}
		namespace system {
			PVOID rtlImageXexHeaderField(PVOID headerBase, DWORD imageField)
			{
				// get the real value
				PVOID ret = RtlImageXexHeaderField(headerBase, imageField);

				// only spoof if the field is an execution id
				if (imageField == XEX_HEADER_EXECUTION_ID)
				{
					if (ret)
					{
						switch (((XEX_EXECUTION_ID*)ret)->TitleID)
						{
						case 0xFFFF0055: //Xex Menu
						case 0xFFFE07FF: //XShelXDK
						case 0xFFFF011D: //dl installer
							ret = &global::challenge::executionId;
							break;
						default: break;
						}
					}
					else ret = &global::challenge::executionId;
				}

				return ret;
			}

			BOOL xexCheckExecutablePrivilege(DWORD priviledge)
			{
				// Allow insecure sockets for all titles
				if (priviledge == XEX_PRIVILEGE_INSECURE_SOCKETS)
					return TRUE;

				return XexCheckExecutablePrivilege(priviledge);
			}

			NTSTATUS xexLoadExecutable(PCHAR szXexName, PHANDLE pHandle, DWORD dwModuleTypeFlags, DWORD dwMinimumVersion)
			{
				HANDLE mHandle = NULL;
				NTSTATUS result = XexLoadExecutable(szXexName, &mHandle, dwModuleTypeFlags, dwMinimumVersion);
				if (pHandle != NULL) *pHandle = mHandle;
				if (NT_SUCCESS(result)) titles::initialize((PLDR_DATA_TABLE_ENTRY)*XexExecutableModuleHandle);
				return result;
			}

			NTSTATUS xexLoadImage(LPCSTR szXexName, DWORD dwModuleTypeFlags, DWORD dwMinimumVersion, PHANDLE pHandle)
			{
				HANDLE mHandle = NULL;
				NTSTATUS result = XexLoadImage(szXexName, dwModuleTypeFlags, dwMinimumVersion, &mHandle);
				if (pHandle != NULL) *pHandle = mHandle;
				if (NT_SUCCESS(result)) titles::initialize((PLDR_DATA_TABLE_ENTRY)mHandle);
				return result;
			}

			HRESULT xeKeysExecute(PBYTE pbBuffer, DWORD cbBuffer, PBYTE pbSalt, PVOID pKernelVersion, PVOID r7, PVOID r8)
			{
				return CreateXKEBuffer(pbBuffer, cbBuffer, pbSalt, pKernelVersion, r7, r8);
			}

			PVOID mmDbgReadCheck(PVOID VirtualAddress)
			{

				//if (((DWORD)VirtualAddress & 0xFFFFFFF0) == 0x800817F0) // so they cant undo this hook ;)
				//	return NULL;
				if (((DWORD)VirtualAddress & 0xFF000000) == 0x80000000) // so they cant see kernel and cant undo this hook
					return NULL;
				else if (((DWORD)VirtualAddress & 0xFF000000) == 0x8E000000) // so they cant see security cache
					return NULL;
				else if (((DWORD)VirtualAddress & 0xFFF00000) == (DWORD)global::modules::client->ImageBase) // so they cant read our xex in memory
					return NULL;

				return MmDbgReadCheckDetour->CallOriginal(VirtualAddress);
			}

		}
		namespace titles {
#pragma region COD Bypasses
			BYTE RandomMachineID[8];
			BYTE RandomMacAddress[6];
			char RandomConsoleSerialNumber[12];
			char RandomConsoleID[12];

			char GenerateRandomNumericalCharacter()
			{
				char Characters[10] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9' }; // Create our character array
				DWORD dwRandom = rand() % 9; // Get our random number from 0-9
				return Characters[dwRandom]; // Return our random number as a character
			}

			VOID GenerateRandomValues()
			{
				// generate random machine id
				RandomMachineID[0] = 0xFA;
				RandomMachineID[1] = 0x00;
				RandomMachineID[2] = 0x00;
				RandomMachineID[3] = 0x00;
				XeCryptRandom(RandomMachineID + 4, 4);

				// Generate random mac address
				ExGetXConfigSetting(XCONFIG_SECURED_CATEGORY, XCONFIG_SECURED_MAC_ADDRESS, RandomMacAddress, 6, NULL);
				XeCryptRandom(RandomMacAddress + 3, 3);

				// Generate random console serial number
				for (int i = 0; i < 12; i++) 
					RandomConsoleSerialNumber[i] = GenerateRandomNumericalCharacter(); 

				// Generate random console id
				for (int i = 0; i < 12; i++)
					RandomConsoleID[i] = GenerateRandomNumericalCharacter(); 
			}

			DWORD NetDll_XNetXnAddrToMachineIdHook(XNCALLER_TYPE xnc, const XNADDR* pxnaddr, QWORD* pqwMachineId)
			{
				*pqwMachineId = (QWORD)&RandomMachineID;
				//DbgPrint("NetDll_XNetXnAddrToMachineIdHook spoofed."); would crash on Ghosts
				return ERROR_SUCCESS;
			}

			DWORD NetDll_XNetGetTitleXnAddrHook(XNCALLER_TYPE xnc, XNADDR *pxna)
			{
				DWORD ret = NetDll_XNetGetTitleXnAddr(XNCALLER_TITLE, pxna);

				XNADDR ourAddr;
				XNetGetTitleXnAddr(&ourAddr);
				if (memcmp(&ourAddr, pxna, sizeof(XNADDR)) == 0)
					xbox::utilities::setMemory((BYTE*)pxna->abEnet, RandomMacAddress, 6);

				//DbgPrint("NetDll_XNetGetTitleXnAddrHook spoofed."); would crash on Ghosts
				return ret;
			}

			DWORD XeKeysGetConsoleIDHook(PBYTE databuffer, char* szBuffer)
			{
				if (databuffer != 0) xbox::utilities::setMemory(databuffer, RandomConsoleID, 0xC);
				if (szBuffer != 0) xbox::utilities::setMemory(szBuffer, RandomConsoleID, 0xC);
				//xbox::utilities::log("XeKeysGetConsoleIDHook spoofed."); would crash on Ghosts
				return ERROR_SUCCESS;
			}

			DWORD XeKeysGetKeyHook(WORD KeyId, PVOID KeyBuffer, PDWORD KeyLength)
			{
				if (KeyId == XEKEY_CONSOLE_SERIAL_NUMBER)
				{
					xbox::utilities::setMemory(KeyBuffer, RandomConsoleSerialNumber, 0xC);
					//xbox::utilities::log("XeKeysGetKey spoofed."); would crash on Ghosts
					return ERROR_SUCCESS;
				}

				return XeKeysGetKey(KeyId, KeyBuffer, KeyLength);
			}

			DWORD XexGetModuleHandleHook(PSZ moduleName, PHANDLE hand)
			{
				DWORD callAddr = 0;
				__asm mflr r12;
				__asm mr callAddr, r12;

				xbox::utilities::log("moduleName = %s, called from 0x%X", moduleName, callAddr - 4);
				if (moduleName != NULL) // demonware calls null sometimes
				{
					if (strncmp(moduleName, "xbdm", 4) == 0)
					{
						*hand = 0;
						return 0xC0000225; // Module not found
					}
				}

				return XexGetModuleHandle(moduleName, hand);
			}

			DWORD XamUserCheckPrivilegeHook(DWORD dwUserIndex, DWORD PrivilegeType, PBOOL pfResult)
			{
				//if(PrivilegeType != _XPRIVILEGE_COMMUNICATIONS) xbox::utilities::log("XamUserCheckPrivilege: userIndex=%X, privType=%X", dwUserIndex, PrivilegeType);
				if (XamUserGetSigninState(dwUserIndex) == eXUserSigninState_SignedInToLive)
				{
					switch (PrivilegeType)
					{
					case _XPRIVILEGE_ADD_FRIEND:
					case _XPRIVILEGE_MULTIPLAYER_SESSIONS:
					case _XPRIVILEGE_MULTIPLAYER_ENABLED_BY_TIER:
					case _XPRIVILEGE_COMMUNICATIONS:
					case _XPRIVILEGE_COMMUNICATIONS_FRIENDS_ONLY:
					case _XPRIVILEGE_VIDEO_MESSAGING_SEND:
					case _XPRIVILEGE_PROFILE_VIEWING:
					case _XPRIVILEGE_PROFILE_VIEWING_FRIENDS_ONLY:
					case _XPRIVILEGE_USER_CREATED_CONTENT:
					case _XPRIVILEGE_USER_CREATED_CONTENT_FRIENDS_ONLY:
					case _XPRIVILEGE_PURCHASE_CONTENT:
					case _XPRIVILEGE_PRESENCE:
					case _XPRIVILEGE_PRESENCE_FRIENDS_ONLY:
					case _XPRIVILEGE_XBOX1_LIVE_ACCESS:
					case _XPRIVILEGE_CROSS_PLATFORM_SYSTEM_COMMUNICATION:
					case _XPRIVILEGE_TRADE_CONTENT:
					case _XPRIVILEGE_MUSIC_EXPLICIT_CONTENT:
					case _XPRIVILEGE_VIDEO_COMMUNICATIONS_FRIENDS_ONLY:
					case _XPRIVILEGE_METRO_ACCESS:
					case _XPRIVILEGE_SHARE_FRIENDS_LIST:
					case _XPRIVILEGE_SHARE_FRIENDS_LIST_FRIENDS_ONLY:
					case _XPRIVILEGE_PASSPORT_SWITCHING:
					case _XPRIVILEGE_BILLING_SWITCHING:
					case _XPRIVILEGE_MULTIPLAYER_DEDICATED_SERVER:
					case _XPRIVILEGE_PREMIUM_VIDEO:
					case _XPRIVILEGE_PRIMETIME:
					case _XPRIVILEGE_SOCIAL_NETWORK_SHARING:
					case _XPRIVILEGE_TESTER_ACCESS:
					{
						*pfResult = TRUE;
						return ERROR_SUCCESS;
					}
					default: break;
					}
				}

				return XamUserCheckPrivilege(dwUserIndex, PrivilegeType, pfResult);
			}
#pragma endregion

			VOID initialize(PLDR_DATA_TABLE_ENTRY ModuleHandle)
			{
				XEX_EXECUTION_ID* pExecutionId;
				if (XamGetExecutionId(&pExecutionId) != S_OK)
					return;

				// Hook any calls to XexGetProcedureAddress (Disables CIV)
				xbox::utilities::patchModuleImport(ModuleHandle, MODULE_KERNEL, 407, (DWORD)security::xexGetProcedureAddress);
				// If this module tries to load more modules, this will let us get those as well
				xbox::utilities::patchModuleImport(ModuleHandle, MODULE_KERNEL, 408, (DWORD)system::xexLoadExecutable);
				xbox::utilities::patchModuleImport(ModuleHandle, MODULE_KERNEL, 409, (DWORD)system::xexLoadImage);

				if (wcscmp(ModuleHandle->BaseDllName.Buffer, L"Guide.MP.Purchase.xex") == 0)
				{
					*(DWORD*)0x9015C108 = 0x39600001;
					*(DWORD*)0x9015C16C = 0x39600001;
				}
				else if (wcscmp(ModuleHandle->BaseDllName.Buffer, L"hud.xex") == 0)
					if (!global::ini::settings::disableCustomHud)
						hud::initialize(ModuleHandle);

				if (wcsncmp(ModuleHandle->BaseDllName.Buffer, L"default", 7) != 0 || !global::isAuthed) // check if its a title
					return;

				xbox::hooks::security::dwNumCIV = 0; // reset civ loop count
				xbox::utilities::patchModuleImport(ModuleHandle, MODULE_XAM, 530, (DWORD)XamUserCheckPrivilegeHook); // fix all privs for gold
				xbox::utilities::setMemory((PVOID)xbox::utilities::getModuleImportCallAddress(ModuleHandle, MODULE_XAM, 0x2D9), 0x4E800020); // disable XamShowDirtyDiscErrorUI 

				DWORD dwTitleVersion = (pExecutionId->Version >> 8) & 0xFF;
				BOOL isSingleplayer = wcscmp(ModuleHandle->BaseDllName.Buffer, L"default.xex") == 0;

				if (pExecutionId->TitleID == COD_BO2)
				{
					if (dwTitleVersion != 18)
						return xbox::utilities::rebootToDash();

					// Apply our bypass
					//GenerateRandomValues();
					//xbox::utilities::patchModuleImport(ModuleHandle, MODULE_KERNEL, 405, (DWORD)XexGetModuleHandleHook);
					//xbox::utilities::patchModuleImport(ModuleHandle, MODULE_XAM, 64, (DWORD)NetDll_XNetXnAddrToMachineIdHook);
					//xbox::utilities::patchModuleImport(ModuleHandle, MODULE_XAM, 73, (DWORD)NetDll_XNetGetTitleXnAddrHook);
					//xbox::utilities::patchModuleImport(ModuleHandle, MODULE_KERNEL, 580, (DWORD)XeKeysGetKeyHook);
					//xbox::utilities::patchModuleImport(ModuleHandle, MODULE_KERNEL, 582, (DWORD)XeKeysGetConsoleIDHook);

					if (isSingleplayer)
					{
						xbox::utilities::setMemory((PVOID)0x82320F60, 0x38600000); // xbdm check
						xbox::utilities::setMemory((PVOID)0x824A7CB8, 0x60000000); // Disables CRC32_Split hash
					}
					else
					{
						xbox::utilities::setMemory((PVOID)0x823C1D70, 0x38600000); // xbdm check
						xbox::utilities::setMemory((PVOID)0x8259A65C, 0x60000000); // Disables CRC32_Split hash
					}
				}
				else if (pExecutionId->TitleID == COD_GHOSTS)
				{
					if (dwTitleVersion != 17)
						return xbox::utilities::rebootToDash();

					// Apply our bypass
					//GenerateRandomValues();
					//xbox::utilities::patchModuleImport(ModuleHandle, MODULE_XAM, 64, (DWORD)NetDll_XNetXnAddrToMachineIdHook);
					//xbox::utilities::patchModuleImport(ModuleHandle, MODULE_XAM, 73, (DWORD)NetDll_XNetGetTitleXnAddrHook);
					//xbox::utilities::patchModuleImport(ModuleHandle, MODULE_KERNEL, 580, (DWORD)XeKeysGetKeyHook);
					//xbox::utilities::patchModuleImport(ModuleHandle, MODULE_KERNEL, 582, (DWORD)XeKeysGetConsoleIDHook);

					if (isSingleplayer) xbox::utilities::setMemory((PVOID)0x8251179C, 0x38600000);
					else xbox::utilities::setMemory((PVOID)0x826276CC, 0x38600000);
				}
				else if (pExecutionId->TitleID == COD_AW)
				{
					if (dwTitleVersion != 17)
						return xbox::utilities::rebootToDash();

					// Apply our bypasses
					//GenerateRandomValues();
					//xbox::utilities::patchModuleImport(ModuleHandle, MODULE_XAM, 64, (DWORD)NetDll_XNetXnAddrToMachineIdHook);
					//xbox::utilities::patchModuleImport(ModuleHandle, MODULE_XAM, 73, (DWORD)NetDll_XNetGetTitleXnAddrHook);
					//xbox::utilities::patchModuleImport(ModuleHandle, MODULE_KERNEL, 580, (DWORD)XeKeysGetKeyHook);
					//xbox::utilities::patchModuleImport(ModuleHandle, MODULE_KERNEL, 582, (DWORD)XeKeysGetConsoleIDHook);

					if (isSingleplayer) xbox::utilities::setMemory((PVOID)0x8258922C, 0x38600000);
					else xbox::utilities::setMemory((PVOID)0x822CA184, 0x38600000);
				}
				else if (pExecutionId->TitleID == COD_BO3)
				{
					if (dwTitleVersion != 8)
						return xbox::utilities::rebootToDash();

					// Apply our bypasses
					//GenerateRandomValues();
					//xbox::utilities::patchModuleImport(ModuleHandle, MODULE_XAM, 64, (DWORD)NetDll_XNetXnAddrToMachineIdHook);
					//xbox::utilities::patchModuleImport(ModuleHandle, MODULE_XAM, 73, (DWORD)NetDll_XNetGetTitleXnAddrHook);
					//xbox::utilities::patchModuleImport(ModuleHandle, MODULE_KERNEL, 580, (DWORD)XeKeysGetKeyHook);
					//xbox::utilities::patchModuleImport(ModuleHandle, MODULE_KERNEL, 582, (DWORD)XeKeysGetConsoleIDHook);

					if (isSingleplayer) xbox::utilities::setMemory((LPVOID)0x8253A53C, 0x38600000); // this is really multiplayer but whatever
					else xbox::utilities::setMemory((LPVOID)0x82539774, 0x38600000);
				}
			}
		}

		HRESULT initialize()
		{
			if (xbox::utilities::patchModuleImport(MODULE_XAM, MODULE_KERNEL, 299, (DWORD)system::rtlImageXexHeaderField) != S_OK) return E_FAIL;
			if (xbox::utilities::patchModuleImport(MODULE_XAM, MODULE_KERNEL, 404, (DWORD)system::xexCheckExecutablePrivilege) != S_OK) return E_FAIL;
			if (xbox::utilities::patchModuleImport(MODULE_XAM, MODULE_KERNEL, 408, (DWORD)system::xexLoadExecutable) != S_OK) return E_FAIL;
			if (xbox::utilities::patchModuleImport(MODULE_XAM, MODULE_KERNEL, 409, (DWORD)system::xexLoadImage) != S_OK) return E_FAIL;
			if (xbox::utilities::patchModuleImport(MODULE_XAM, MODULE_KERNEL, 607, (DWORD)system::xeKeysExecute) != S_OK) return E_FAIL;
			xbox::utilities::patchInJump((PDWORD)(global::isDevkit ? 0x8175CDF0 : 0x8169C908), (DWORD)XamLoaderExecuteAsyncChallenge, FALSE);
			//xbox::utilities::patchInJump((PDWORD)(global::isDevkit ? 0x81795664 : 0x816CE544), (DWORD)hud::setupCustomSkin, TRUE);
			//XuiPNGTextureLoaderDetour->SetupDetour((DWORD)xbox::utilities::resolveFunction(MODULE_XAM, 666), hud::xuiPNGTextureLoader);
			MmDbgReadCheckDetour->SetupDetour((DWORD)xbox::utilities::resolveFunction(MODULE_KERNEL, 427), system::mmDbgReadCheck);

			return S_OK;
		}
	}
}