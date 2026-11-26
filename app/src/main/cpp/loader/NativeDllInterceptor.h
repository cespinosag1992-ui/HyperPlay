#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "BionicPeLoader.h"
#include "NativeSteamStub.h"
#include "PeHeaderInspector.h"

namespace hyperplay {

struct CpuContext;

struct InterceptTelemetry {
    uint64_t kernel32Calls = 0;
    uint64_t ntdllCalls = 0;
    uint64_t d3dCalls = 0;
    uint64_t vulkanCalls = 0;
    uint64_t xinputCalls = 0;
    uint64_t xaudioCalls = 0;
    uint64_t steamCalls = 0;
    uint64_t otherCalls = 0;
};

/// Resultado del parcheo de la IAT: un import sin resolver deja en el slot un RVA de la tabla de
/// nombres, no una dirección. Ejecutar con `unresolved != 0` salta a memoria inválida.
struct IatHijackStats {
    size_t total = 0;
    size_t hijacked = 0;
    size_t unresolved = 0;
    size_t outOfImage = 0;
    /// Nombres "dll!funcion" (o motivo) de los slots que no se pudieron parchear.
    std::vector<std::string> unresolvedImports;
};

class NativeDllInterceptor {
public:
    NativeDllInterceptor() = delete;

    /// Inicializa las tablas de interceptación asociadas a una imagen cargada.
    static void init(LoadedPeImage* image);

    /// Intercepta las entradas de la IAT en memoria con trampolines nativos aarch64.
    static size_t hijackIat(LoadedPeImage& image);

    /// Igual que [hijackIat] pero informa de los imports sin resolver y de los slots fuera de la imagen.
    static IatHijackStats hijackIatChecked(LoadedPeImage& image);

    /// Intercepta la IAT de una DLL local (e.g. UnityPlayer.dll), usando wrappers nativos
    /// y asignando stubs dinámicos de diagnóstico por slot para los imports secundarios no mapeados.
    static IatHijackStats hijackLocalDllIat(LoadedPeImage& image);

    /// Obtiene o asigna un slot en la tabla de stubs dinámicos de diagnóstico.
    static void* getOrCreateDynamicStub(const std::string& dllName, const std::string& funcName);

    /// Resuelve un símbolo por nombre de DLL y nombre de función.
    [[nodiscard]] static void* resolveNativeSymbol(const std::string& dllName, const std::string& funcName);

    /// Comprueba si una dirección corresponde a un trampolín host registrado.
    [[nodiscard]] static bool isRegisteredTrampoline(uintptr_t target) noexcept;

    /// Despachador de trampolines Win64 -> AAPCS64 (Microsoft x64 calling convention).
    static bool dispatchThunk(void* user, uintptr_t target, CpuContext& ctx);

    /// Obtiene las estadísticas de telemetría de llamadas interceptadas.
    [[nodiscard]] static InterceptTelemetry getTelemetry() noexcept;

    /// Reinicia los contadores de telemetría.
    static void resetTelemetry() noexcept;

    /// Configura el puntero al TEB del hilo invitado actual para sincronizar TLS Win32/TEB.
    static void setGuestTeb(void* teb, bool is64Bit) noexcept;
    /// Para todos los clientes de audio WASAPI (al cerrar el juego).
    static void stopAllAudio() noexcept;

    /// Registra hooks para las rutinas internas de gestión de memoria de UnityPlayer.dll
    static void registerUnityMemoryHooks(uintptr_t unityPlayerBase);

    // Wrappers POSIX/Bionic expuestos como trampolines para Win32:
    // Kernel32 / Memory
    static void* win_VirtualAlloc(void* lpAddress, size_t dwSize, uint32_t flAllocationType, uint32_t flProtect);
    static int32_t win_VirtualFree(void* lpAddress, size_t dwSize, uint32_t dwFreeType);
    static int32_t win_VirtualProtect(void* lpAddress, size_t dwSize, uint32_t flNewProtect, uint32_t* lpflOldProtect);
    static size_t win_VirtualQuery(const void* lpAddress, void* lpBuffer, size_t dwLength);
    static void* win_HeapCreate(uint32_t flOptions, size_t dwInitialSize, size_t dwMaximumSize);
    static int32_t win_HeapDestroy(void* hHeap);

    // Kernel32 / Environment & Directory
    static uint32_t win_GetEnvironmentVariableW(const wchar_t* lpName, wchar_t* lpBuffer, uint32_t nSize);
    static uint32_t win_GetEnvironmentVariableA(const char* lpName, char* lpBuffer, uint32_t nSize);
    static int32_t win_SetEnvironmentVariableW(const wchar_t* lpName, const wchar_t* lpValue);
    static uint32_t win_GetCurrentDirectoryW(uint32_t nBufferLength, wchar_t* lpBuffer);
    static int32_t win_SetCurrentDirectoryW(const wchar_t* lpPathName);
    static uint32_t win_GetDriveTypeW(const wchar_t* lpRootPathName);
    static uint32_t win_GetLogicalDriveStringsW(uint32_t nBufferLength, wchar_t* lpBuffer);
    static int32_t win_GetDiskFreeSpaceExW(const wchar_t* lpDirectoryName, uint64_t* lpFreeBytesAvailableToCaller,
                                          uint64_t* lpTotalNumberOfBytes, uint64_t* lpTotalNumberOfFreeBytes);
    static int32_t win_GetVolumeInformationW(const wchar_t* lpRootPathName, wchar_t* lpVolumeNameBuffer,
                                            uint32_t nVolumeNameSize, uint32_t* lpVolumeSerialNumber,
                                            uint32_t* lpMaximumComponentLength, uint32_t* lpFileSystemFlags,
                                            wchar_t* lpFileSystemNameBuffer, uint32_t nFileSystemNameSize);

    // Kernel32 / Process & Thread & Synchronization
    static int32_t win_DisableThreadLibraryCalls(void* hLibModule);
    static int32_t win_TryEnterCriticalSection(void* lpCriticalSection);
    static void win_InitializeSRWLock(void* srwLock);
    static void win_GetCurrentProcessorNumberEx(void* procNumber);
    static void win_FlushProcessWriteBuffers();
    static int32_t win_FlushInstructionCache(void* process, const void* address, size_t size);
    // Pool de hilos (trabajos): cada Submit lanza un hilo invitado que llama callback(instancia, ctx, trabajo).
    static void* win_CreateThreadpoolWork(void* pfnwk, void* pv, void* pcbe);
    static void win_SubmitThreadpoolWork(void* pwk);
    static void win_WaitForThreadpoolWorkCallbacks(void* pwk, int32_t fCancelPendingCallbacks);
    static void win_CloseThreadpoolWork(void* pwk);
    // Puertos de finalización de E/S (IOCP): cola de paquetes; las E/S de archivo son síncronas, así que
    // solo llegan los paquetes de PostQueuedCompletionStatus (lo que usa el pool de hilos de .NET).
    static void* win_CreateIoCompletionPort(void* fileHandle, void* existingPort, uint64_t key, uint32_t threads);
    static int32_t win_GetQueuedCompletionStatus(void* port, uint32_t* bytes, uint64_t* key, void** overlapped,
                                                 uint32_t ms);
    static int32_t win_GetQueuedCompletionStatusEx(void* port, void* entries, uint32_t count, uint32_t* removed,
                                                   uint32_t ms, int32_t alertable);
    static int32_t win_PostQueuedCompletionStatus(void* port, uint32_t bytes, uint64_t key, void* overlapped);
    static void* win_AddVectoredExceptionHandler(uint32_t first, void* handler);
    static uint32_t win_RemoveVectoredExceptionHandler(void* handle);
    static int32_t win_SetThreadStackGuarantee(uint32_t* stackSizeInBytes);
    static uint32_t win_GetUserDefaultLCID();
    static int32_t win_GetUserDefaultLocaleName(char16_t* lpLocaleName, int32_t cchLocaleName);
    static int32_t win_LoadStringW(void* hInstance, uint32_t uID, char16_t* lpBuffer, int32_t cchBufferMax);
    static int32_t win_EnumSystemLocalesW(void* lpLocaleEnumProc, uint32_t dwFlags);
    static int32_t win_IsValidLocale(uint32_t locale, uint32_t dwFlags);
    static int32_t win_CompareStringW(uint32_t locale, uint32_t dwCmpFlags, const wchar_t* lpString1, int32_t cchCount1,
                                     const wchar_t* lpString2, int32_t cchCount2);
    static int32_t win_GetLocaleInfoW(uint32_t locale, uint32_t lcType, wchar_t* lpLCData, int32_t cchData);
    static int32_t win_GetLocaleInfoEx(const wchar_t* lpLocaleName, uint32_t lcType, wchar_t* lpLCData, int32_t cchData);
    static uint32_t win_GetTimeZoneInformation(void* lpTimeZoneInformation);
    static int32_t win_SystemTimeToTzSpecificLocalTime(const void* lpTimeZoneInformation, const void* lpUniversalTime, void* lpLocalTime);
    static int32_t win_TzSpecificLocalTimeToSystemTime(const void* lpTimeZoneInformation, const void* lpLocalTime, void* lpUniversalTime);
    static int32_t win_FileTimeToSystemTime(const void* lpFileTime, void* lpSystemTime);
    static uint32_t win_GetFullPathNameW(const wchar_t* lpFileName, uint32_t nBufferLength, wchar_t* lpBuffer, wchar_t** lpFilePart);
    static int32_t win_GetFileInformationByHandle(void* hFile, void* lpFileInformation);
    static int32_t win_GetFileInformationByHandleEx(void* hFile, int32_t fileInformationClass, void* lpFileInformation, uint32_t dwBufferSize);
    static int32_t win_GetProcessTimes(void* hProcess, void* lpCreationTime, void* lpExitTime, void* lpKernelTime, void* lpUserTime);
    static int32_t win_GetSystemTimes(void* lpIdleTime, void* lpKernelTime, void* lpUserTime);
    static uint32_t win_FormatMessageW(uint32_t dwFlags, const void* lpSource, uint32_t dwMessageId,
                                      uint32_t dwLanguageId, wchar_t* lpBuffer, uint32_t nSize, void* arguments);

    // Kernel32 / File I/O
    static void* win_CreateFileW(const wchar_t* lpFileName, uint32_t dwDesiredAccess, uint32_t dwShareMode,
                                 void* lpSecurityAttributes, uint32_t dwCreationDisposition,
                                 uint32_t dwFlagsAndAttributes, void* hTemplateFile);
    static void* win_CreateFileA(const char* lpFileName, uint32_t dwDesiredAccess, uint32_t dwShareMode,
                                 void* lpSecurityAttributes, uint32_t dwCreationDisposition,
                                 uint32_t dwFlagsAndAttributes, void* hTemplateFile);
    static int32_t win_ReadFile(void* hFile, void* lpBuffer, uint32_t nNumberOfBytesToRead,
                                uint32_t* lpNumberOfBytesRead, void* lpOverlapped);
    static int32_t win_WriteFile(void* hFile, const void* lpBuffer, uint32_t nNumberOfBytesToWrite,
                                 uint32_t* lpNumberOfBytesWritten, void* lpOverlapped);
    static int32_t win_CloseHandle(void* hObject);
    static uint32_t win_GetFileSize(void* hFile, uint32_t* lpFileSizeHigh);
    static int32_t win_GetFileSizeEx(void* hFile, int64_t* lpFileSize);
    static uint32_t win_SetFilePointer(void* hFile, int32_t lDistanceToMove, int32_t* lpDistanceToMoveHigh, uint32_t dwMoveMethod);

    // File Mapping (VFS / mmap)
    static void* win_CreateFileMappingW(void* hFile, void* lpAttributes, uint32_t flProtect,
                                        uint32_t dwMaxSizeHigh, uint32_t dwMaxSizeLow, const wchar_t* lpName);
    static void* win_CreateFileMappingA(void* hFile, void* lpAttributes, uint32_t flProtect,
                                        uint32_t dwMaxSizeHigh, uint32_t dwMaxSizeLow, const char* lpName);
    static void* win_MapViewOfFile(void* hFileMappingObject, uint32_t dwDesiredAccess,
                                   uint32_t dwFileOffsetHigh, uint32_t dwFileOffsetLow, size_t dwNumberOfBytesToMap);
    static void* win_MapViewOfFileEx(void* hFileMappingObject, uint32_t dwDesiredAccess,
                                     uint32_t dwFileOffsetHigh, uint32_t dwFileOffsetLow,
                                     size_t dwNumberOfBytesToMap, void* lpBaseAddress);
    static int32_t win_UnmapViewOfFile(const void* lpBaseAddress);
    static int32_t win_FlushViewOfFile(const void* lpBaseAddress, size_t dwNumberOfBytesToFlush);
    static void* win_OpenFileMappingW(uint32_t dwDesiredAccess, int32_t bInheritHandle, const wchar_t* lpName);
    static void* win_OpenFileMappingA(uint32_t dwDesiredAccess, int32_t bInheritHandle, const char* lpName);

    // Threads & Sync
    static void* win_CreateThread(void* lpThreadAttributes, size_t dwStackSize, void* lpStartAddress,
                                  void* lpParameter, uint32_t dwCreationFlags, uint32_t* lpThreadId);
    static uint32_t win_GetCurrentThreadId();
    static void win_Sleep(uint32_t dwMilliseconds);
    static uint32_t win_SleepEx(uint32_t dwMilliseconds, int32_t bAlertable);
    static void win_InitializeCriticalSection(void* lpCriticalSection);
    static void win_EnterCriticalSection(void* lpCriticalSection);
    static void win_LeaveCriticalSection(void* lpCriticalSection);
    static void win_DeleteCriticalSection(void* lpCriticalSection);

    // Time & System Info
    static uint32_t win_GetTickCount();
    static int32_t win_QueryPerformanceCounter(int64_t* lpPerformanceCount);
    static int32_t win_QueryPerformanceFrequency(int64_t* lpFrequency);
    static uint32_t win_timeGetTime();
    static uint32_t win_timeBeginPeriod(uint32_t uPeriod);
    static uint32_t win_timeEndPeriod(uint32_t uPeriod);
    static uint32_t win_timeGetDevCaps(void* ptc, uint32_t cbtc);

    // Module & Symbol Resolution
    static void* win_GetModuleHandleW(const wchar_t* lpModuleName);
    static void* win_GetModuleHandleA(const char* lpModuleName);
    static void* win_GetProcAddress(void* hModule, const char* lpProcName);
    static void* win_LoadLibraryW(const wchar_t* lpLibFileName);
    static void* win_LoadLibraryA(const char* lpLibFileName);

    // System Info, Heap & TLS
    static void win_GetSystemInfo(void* lpSystemInfo);
    static uint64_t win_GetTickCount64();
    static void* win_HeapAlloc(void* hHeap, uint32_t dwFlags, size_t dwBytes);
    static int32_t win_HeapFree(void* hHeap, uint32_t dwFlags, void* lpMem);
    static uint32_t win_TlsAlloc();
    static int32_t win_InitOnceBeginInitialize(void* lpInitOnce, uint32_t dwFlags,
                                               int32_t* fPending, void** lpContext);
    static int32_t win_InitOnceComplete(void* lpInitOnce, uint32_t dwFlags, void* lpContext);
    static void* win_TlsGetValue(uint32_t dwTlsIndex);
    static int32_t win_TlsSetValue(uint32_t dwTlsIndex, void* lpTlsValue);

    // Window & Messages
    static void* win_CreateWindowExW(uint32_t dwExStyle, const wchar_t* lpClassName, const wchar_t* lpWindowName,
                                     uint32_t dwStyle, int32_t X, int32_t Y, int32_t nWidth, int32_t nHeight,
                                     void* hWndParent, void* hMenu, void* hInstance, void* lpParam);
    static void* win_CreateWindowExA(uint32_t dwExStyle, const char* lpClassName, const char* lpWindowName,
                                     uint32_t dwStyle, int32_t X, int32_t Y, int32_t nWidth, int32_t nHeight,
                                     void* hWndParent, void* hMenu, void* hInstance, void* lpParam);
    static int32_t win_ShowWindow(void* hWnd, int32_t nCmdShow);
    static int32_t win_PeekMessageW(void* lpMsg, void* hWnd, uint32_t wMsgFilterMin, uint32_t wMsgFilterMax, uint32_t wRemoveMsg);
    static int32_t win_DispatchMessageW(const void* lpMsg);
    static void* win_GetForegroundWindow();
    static void* win_GetActiveWindow();
    static void* win_GetDesktopWindow();
    static void* win_GetShellWindow();
    static int32_t win_SetForegroundWindow(void* hWnd);
    static void* win_SetActiveWindow(void* hWnd);
    static void* win_GetFocus();
    static void* win_SetFocus(void* hWnd);
    static int32_t win_GetClientRect(void* hWnd, void* lpRect);
    static int32_t win_GetWindowRect(void* hWnd, void* lpRect);
    static int32_t win_AdjustWindowRect(void* lpRect, uint32_t dwStyle, int32_t bMenu);
    static int32_t win_AdjustWindowRectEx(void* lpRect, uint32_t dwStyle, int32_t bMenu, uint32_t dwExStyle);
    static int32_t win_GetSystemMetrics(int32_t nIndex);
    static int32_t win_SetWindowPos(void* hWnd, void* hWndInsertAfter, int32_t X, int32_t Y, int32_t cx, int32_t cy, uint32_t uFlags);
    static int32_t win_SetWindowTextW(void* hWnd, const wchar_t* lpString);
    static int32_t win_SetWindowTextA(void* hWnd, const char* lpString);
    static void* win_GetDC(void* hWnd);
    static int32_t win_ReleaseDC(void* hWnd, void* hDC);
    static int32_t win_ValidateRect(void* hWnd, const void* lpRect);
    static int32_t win_InvalidateRect(void* hWnd, const void* lpRect, int32_t bErase);
    static int32_t win_UpdateWindow(void* hWnd);
    static void* win_SetCursor(void* hCursor);
    static int32_t win_ShowCursor(int32_t bShow);
    static int64_t win_DefWindowProcW(void* hWnd, uint32_t Msg, uint64_t wParam, int64_t lParam);
    static int64_t win_DefWindowProcA(void* hWnd, uint32_t Msg, uint64_t wParam, int64_t lParam);
    static int32_t win_PostMessageW(void* hWnd, uint32_t Msg, uint64_t wParam, int64_t lParam);
    static void win_PostQuitMessage(int32_t nExitCode);
    static uint32_t win_RegisterWindowMessageW(const void* lpString);
    static uint32_t win_RegisterWindowMessageA(const char* lpString);

    // Desktop & Window Station
    static void* win_GetThreadDesktop(uint32_t dwThreadId);
    static void* win_GetProcessWindowStation();
    static int32_t win_SetThreadDesktop(void* hDesktop);
    static int32_t win_SetProcessWindowStation(void* hWinSta);
    static int32_t win_GetUserObjectInformationA(void* hObj, int32_t nIndex, void* pvInfo, uint32_t nLength, uint32_t* lpnLengthNeeded);
    static int32_t win_GetUserObjectInformationW(void* hObj, int32_t nIndex, void* pvInfo, uint32_t nLength, uint32_t* lpnLengthNeeded);

    // Message Wait & Pump
    static uint32_t win_MsgWaitForMultipleObjects(uint32_t nCount, void* const* pHandles, int32_t bWaitAll, uint32_t dwMilliseconds, uint32_t dwWakeMask);
    static uint32_t win_MsgWaitForMultipleObjectsEx(uint32_t nCount, void* const* pHandles, uint32_t dwMilliseconds, uint32_t dwWakeMask, uint32_t dwFlags);
    static int32_t win_TranslateMessage(const void* lpMsg);
    static int32_t win_GetMessageW(void* lpMsg, void* hWnd, uint32_t wMsgFilterMin, uint32_t wMsgFilterMax);
    static int32_t win_GetMessageA(void* lpMsg, void* hWnd, uint32_t wMsgFilterMin, uint32_t wMsgFilterMax);
    static int32_t win_PeekMessageA(void* lpMsg, void* hWnd, uint32_t wMsgFilterMin, uint32_t wMsgFilterMax, uint32_t wRemoveMsg);
    static int32_t win_DispatchMessageA(const void* lpMsg);
    static int64_t win_SendMessageW(void* hWnd, uint32_t Msg, uint64_t wParam, int64_t lParam);
    static int64_t win_SendMessageA(void* hWnd, uint32_t Msg, uint64_t wParam, int64_t lParam);
    static int32_t win_PostMessageA(void* hWnd, uint32_t Msg, uint64_t wParam, int64_t lParam);
    static int64_t win_SendMessageTimeoutA(void* hWnd, uint32_t Msg, uint64_t wParam, int64_t lParam, uint32_t fuFlags, uint32_t uTimeout, uintptr_t* lpdwResult);

    // Displays & Monitors
    static int32_t win_EnumDisplayDevicesA(const char* lpDevice, uint32_t iDevNum, void* lpDisplayDevice, uint32_t dwFlags);
    static int32_t win_EnumDisplayDevicesW(const wchar_t* lpDevice, uint32_t iDevNum, void* lpDisplayDevice, uint32_t dwFlags);
    static int32_t win_EnumWindows(void* lpEnumFunc, int64_t lParam);

    // Raw Input & Keyboard & Mouse
    static uint32_t win_GetRawInputDeviceList(void* pRawInputDeviceList, uint32_t* puiNumDevices, uint32_t cbSize);
    static int32_t win_RegisterRawInputDevices(const void* pRawInputDevices, uint32_t uiNumDevices, uint32_t cbSize);
    static uint32_t win_GetRawInputDeviceInfoW(void* hDevice, uint32_t uiCommand, void* pData, uint32_t* pcbSize);
    static uint32_t win_GetRawInputData(void* hRawInput, uint32_t uiCommand, void* pData, uint32_t* pcbSize, uint32_t cbSizeHeader);
    static uint32_t win_GetRawInputBuffer(void* pData, uint32_t* pcbSize, uint32_t cbSizeHeader);
    static int16_t win_GetKeyState(int32_t nVirtKey);
    static int16_t win_GetAsyncKeyState(int32_t vKey);
    static uint32_t win_MapVirtualKeyW(uint32_t uCode, uint32_t uMapType);
    static uint32_t win_MapVirtualKeyA(uint32_t uCode, uint32_t uMapType);
    static uint32_t win_MapVirtualKeyExA(uint32_t uCode, uint32_t uMapType, void* dwhkl);
    static int32_t win_GetKeyNameTextW(int32_t lParam, wchar_t* lpString, int32_t cchSize);
    static int32_t win_ToUnicode(uint32_t wVirtKey, uint32_t wScanCode, const uint8_t* lpKeyState, wchar_t* pwszBuff, int32_t cchBuff, uint32_t wFlags);
    static void* win_GetKeyboardLayout(uint32_t idThread);
    static int32_t win_GetKeyboardLayoutNameW(wchar_t* pwszKLID);

    // Mouse & Cursor & Capture
    static void* win_SetCapture(void* hWnd);
    static int32_t win_ReleaseCapture();
    static void* win_LoadCursorA(void* hInstance, const char* lpCursorName);
    static void* win_LoadCursorW(void* hInstance, const wchar_t* lpCursorName);
    static int32_t win_DestroyCursor(void* hCursor);
    static void* win_LoadIconA(void* hInstance, const char* lpIconName);
    static void* win_LoadIconW(void* hInstance, const wchar_t* lpIconName);
    static int32_t win_DestroyIcon(void* hIcon);
    static void* win_LoadImageW(void* hInst, const wchar_t* name, uint32_t type, int32_t cx, int32_t cy, uint32_t fuLoad);
    static void* win_LoadImageA(void* hInst, const char* name, uint32_t type, int32_t cx, int32_t cy, uint32_t fuLoad);
    static int32_t win_TrackMouseEvent(void* lpEventTrack);
    static int32_t win_EnableMouseInPointer(int32_t fEnable);
    static int32_t win_IsMouseInPointerEnabled();

    // Window State & Geometry
    static int32_t win_GetWindowPlacement(void* hWnd, void* lpwndpl);
    static int32_t win_SetWindowPlacement(void* hWnd, const void* lpwndpl);
    static int32_t win_AdjustWindowRectExForDpi(void* lpRect, uint32_t dwStyle, int32_t bMenu, uint32_t dwExStyle, uint32_t dpi);
    static int32_t win_IsWindowVisible(void* hWnd);
    static int32_t win_IsIconic(void* hWnd);
    static int32_t win_MoveWindow(void* hWnd, int32_t X, int32_t Y, int32_t nWidth, int32_t nHeight, int32_t bRepaint);
    static int32_t win_PtInRect(const void* lprc, int32_t x, int32_t y);
    static int32_t win_OffsetRect(void* lprc, int32_t dx, int32_t dy);
    static int32_t win_CopyRect(void* lprcDst, const void* lprcSrc);

    // Misc User32
    static int64_t win_GetMessageExtraInfo();
    static uint32_t win_GetDoubleClickTime();
    static uint32_t win_GetCaretBlinkTime();
    static int32_t win_OpenClipboard(void* hWnd);
    static int32_t win_CloseClipboard();
    static int32_t win_EmptyClipboard();
    static void* win_GetClipboardData(uint32_t uFormat);
    static void* win_SetClipboardData(uint32_t uFormat, void* hMem);
    static int32_t win_IsClipboardFormatAvailable(uint32_t format);
    static int32_t win_MessageBoxA(void* hWnd, const char* lpText, const char* lpCaption, uint32_t uType);
    static int32_t win_MessageBoxW(void* hWnd, const wchar_t* lpText, const wchar_t* lpCaption, uint32_t uType);
    static intptr_t win_DialogBoxParamW(void* hInstance, const wchar_t* lpTemplateName, void* hWndParent, void* lpDialogFunc, intptr_t dwInitParam);
    static int32_t win_EndDialog(void* hDlg, intptr_t nResult);
    static int32_t win_SetDlgItemTextW(void* hDlg, int32_t nIDDlgItem, const wchar_t* lpString);
    static int32_t win_SetDlgItemTextA(void* hDlg, int32_t nIDDlgItem, const char* lpString);
    static int64_t win_SendDlgItemMessageW(void* hDlg, int32_t nIDDlgItem, uint32_t Msg, uint64_t wParam, int64_t lParam);
    static void* win_RegisterDeviceNotificationW(void* hRecipient, void* NotificationFilter, uint32_t Flags);
    static int32_t win_UnregisterDeviceNotification(void* Handle);
    // Hooks de ventana (Rewired los instala para recibir WM_INPUT): HHOOK válido, sin efecto.
    static void* win_SetWindowsHookExW(int32_t idHook, void* lpfn, void* hmod, uint32_t threadId);
    static int32_t win_UnhookWindowsHookEx(void* hhk);
    static intptr_t win_CallNextHookEx(void* hhk, int32_t nCode, uintptr_t wParam, intptr_t lParam);
    static int32_t win_RegisterTouchWindow(void* hwnd, uint32_t flags);

    // GDI32
    static int32_t win_ChoosePixelFormat(void* hdc, const void* ppfd);
    static int32_t win_SetPixelFormat(void* hdc, int32_t format, const void* ppfd);
    static int32_t win_GetDeviceCaps(void* hdc, int32_t index);
    static int32_t win_SwapBuffers(void* hdc);

    // DisplayConfig
    static int32_t win_GetDisplayConfigBufferSizes(uint32_t flags, uint32_t* numPathArrayElements, uint32_t* numModeInfoArrayElements);
    static int32_t win_QueryDisplayConfig(uint32_t flags, uint32_t* numPathArrayElements, void* pathArray, uint32_t* numModeInfoArrayElements, void* modeInfoArray, void* currentTopologyId);
    static int32_t win_DisplayConfigGetDeviceInfo(void* requestPacket);

    // Proceso, Hilos y Consola (Win32 KERNEL32)
    static uint32_t win_GetCurrentProcessId();
    static void* win_GetCurrentProcess();
    static void win_GetStartupInfoW(void* lpStartupInfo);
    static const char* win_GetCommandLineA();
    static const void* win_GetCommandLineW();
    static const void* win_GetEnvironmentStringsW();
    static int32_t win_FreeEnvironmentStringsW(void* penv);
    static void* win_GetStdHandle(uint32_t nStdHandle);
    static int32_t win_SetStdHandle(uint32_t nStdHandle, void* hHandle);
    static int32_t win_WriteConsoleW(void* hConsoleOutput, const void* lpBuffer, uint32_t nNumberOfCharsToWrite, uint32_t* lpNumberOfCharsWritten, void* lpReserved);
    static uint32_t win_GetConsoleOutputCP();
    static uint32_t win_GetConsoleCP();
    static int32_t win_GetConsoleMode(void* hConsoleHandle, uint32_t* lpMode);
    static uint32_t win_GetFileType(void* hFile);
    static uint32_t win_GetFileAttributesA(const char* lpFileName);
    static uint32_t win_GetFileAttributesW(const void* lpFileName);
    static int32_t win_GetFileAttributesExA(const char* lpFileName, uint32_t fInfoLevelId, void* lpFileInformation);
    static int32_t win_GetFileAttributesExW(const void* lpFileName, uint32_t fInfoLevelId, void* lpFileInformation);
    static uint32_t win_GetModuleFileNameW(void* hModule, void* lpFilename, uint32_t nSize);
    // PSAPI / K32*: módulos PE reales del proceso (exe + DLL locales).
    static int32_t win_EnumProcessModules(void* hProcess, void** lphModule, uint32_t cb, uint32_t* lpcbNeeded);
    static int32_t win_GetModuleInformation(void* hProcess, void* hModule, void* lpmodinfo, uint32_t cb);
    static uint32_t win_GetModuleFileNameExW(void* hProcess, void* hModule, void* lpFilename, uint32_t nSize);
    static uint32_t win_GetModuleFileNameA(void* hModule, char* lpFilename, uint32_t nSize);
    static uint32_t win_GetTempPathW(uint32_t nBufferLength, void* lpBuffer);
    static uint32_t win_GetTempPathA(uint32_t nBufferLength, char* lpBuffer);
    static int32_t win_SetConsoleCtrlHandler(void* HandlerRoutine, int32_t Add);
    static int32_t win_OpenProcessToken(void* ProcessHandle, uint32_t DesiredAccess, void** TokenHandle);
    static int32_t win_GetModuleHandleExW(uint32_t dwFlags, const void* lpModuleName, void** phModule);
    static void win_ExitProcess(uint32_t uExitCode);
    static int32_t win_TerminateProcess(void* hProcess, uint32_t uExitCode);
    static int32_t win_IsProcessorFeaturePresent(uint32_t ProcessorFeature);
    static int32_t win_IsDebuggerPresent();
    static void* win_CommandLineToArgvW(const void* lpCmdLine, int32_t* pNumArgs);
    static int32_t win_GlobalMemoryStatusEx(void* lpBuffer);
    static int32_t win_CreatePipe(void** phReadPipe, void** phWritePipe, void* lpPipeAttributes, uint32_t nSize);
    static uint32_t win_SetErrorMode(uint32_t uMode);
    static int32_t win_RoInitialize(uint32_t initType);
    static int32_t win_SetHandleInformation(void* hObject, uint32_t dwMask, uint32_t dwFlags);
    static void* win_LocalFree(void* hMem);
    static void* win_LocalAlloc(uint32_t uFlags, size_t uBytes);
    static void* win_GlobalLock(void* hMem);
    static int32_t win_GlobalUnlock(void* hMem);
    static int32_t win_DuplicateHandle(void* hSourceProcessHandle, void* hSourceHandle,
                                       void* hTargetProcessHandle, void** lpTargetHandle,
                                       uint32_t dwDesiredAccess, int32_t bInheritHandle,
                                       uint32_t dwOptions);
    static int32_t win_InitializeProcThreadAttributeList(void* lpAttributeList, uint32_t dwAttributeCount,
                                                         uint32_t dwFlags, size_t* lpSize);
    static int32_t win_UpdateProcThreadAttribute(void* lpAttributeList, uint32_t dwFlags,
                                                 uintptr_t Attribute, void* lpValue, size_t cbSize,
                                                 void* lpPreviousValue, size_t* lpReturnSize);
    static void win_DeleteProcThreadAttributeList(void* lpAttributeList);

    // Errores, Excepciones y Unwind (Win64 SEH / CRT Init)
    static uint32_t win_GetLastError();
    static void win_SetLastError(uint32_t dwErrCode);
    static void* win_SetUnhandledExceptionFilter(void* lpTopLevelExceptionFilter);
    static int32_t win_UnhandledExceptionFilter(void* ExceptionInfo);
    static void win_RaiseException(uint32_t dwExceptionCode, uint32_t dwExceptionFlags, uint32_t nNumberOfArguments, const void* lpArguments);
    static void win_RtlCaptureContext(void* ContextRecord);
    static void* win_RtlLookupFunctionEntry(uint64_t ControlPc, uint64_t* ImageBase, void* HistoryTable);
    static void* win_RtlVirtualUnwind(uint32_t HandlerType, uint64_t ImageBase, uint64_t ControlPc, void* FunctionEntry, void* ContextRecord, void** HandlerData, uint64_t* EstablisherFrame, void* ContextPointers);
    static void win_RtlUnwindEx(void* TargetFrame, void* TargetIp, void* ExceptionRecord, void* ReturnValue, void* ContextRecord, void* HistoryTable);
    static void* win_RtlPcToFileHeader(void* PcValue, void** BaseOfImage);
    static void* win_EncodePointer(void* Ptr);
    static void* win_DecodePointer(void* Ptr);
    static void win_InitializeSListHead(void* ListHead);

    // Sincronización, TLS y FLS
    static int32_t win_InitializeCriticalSectionAndSpinCount(void* lpCriticalSection, uint32_t dwSpinCount);
    static int32_t win_InitializeCriticalSectionEx(void* lpCriticalSection, uint32_t dwSpinCount, uint32_t Flags);
    static void win_AcquireSRWLockExclusive(void* SRWLock);
    static void win_ReleaseSRWLockExclusive(void* SRWLock);
    static int32_t win_TryAcquireSRWLockExclusive(void* SRWLock);
    static void win_AcquireSRWLockShared(void* SRWLock);
    static void win_ReleaseSRWLockShared(void* SRWLock);
    static int32_t win_TryAcquireSRWLockShared(void* SRWLock);
    static int32_t win_SleepConditionVariableSRW(void* ConditionVariable, void* SRWLock, uint32_t dwMilliseconds, uint32_t Flags);
    static int32_t win_SleepConditionVariableCS(void* ConditionVariable, void* CriticalSection, uint32_t dwMilliseconds);
    static void win_WakeAllConditionVariable(void* ConditionVariable);
    static void win_WakeConditionVariable(void* ConditionVariable);
    /// Sincronización real por dirección (Win32 WaitOnAddress/WakeByAddress), sin stub de éxito falso.
    static uint32_t win_WaitOnAddress(const void* Address, const void* CompareAddress,
                                      size_t AddressSize, uint32_t dwMilliseconds);
    static void win_WakeByAddress(const void* Address);
    static void win_WakeByAddressAll(const void* Address);
    static void* win_CreateEventA(void* lpEventAttributes, int32_t bManualReset, int32_t bInitialState, const char* lpName);
    static void* win_CreateEventW(void* lpEventAttributes, int32_t bManualReset, int32_t bInitialState, const wchar_t* lpName);
    static void* win_CreateEventExW(void* lpEventAttributes, const wchar_t* lpName, uint32_t dwFlags, uint32_t dwDesiredAccess);
    // Waitable timers (FMOD los usa para su hilo de mezcla en modo sondeo): un evento que un
    // hilo de servicio señala al vencer (y reprograma si es periódico).
    static void* win_CreateWaitableTimerW(void* attrs, int32_t manualReset, const void* name);
    static void* win_CreateWaitableTimerExW(void* attrs, const void* name, uint32_t flags, uint32_t access);
    static int32_t win_SetWaitableTimer(void* hTimer, const int64_t* dueTime, int32_t periodMs, void* completion,
                                        void* arg, int32_t resume);
    static int32_t win_CancelWaitableTimer(void* hTimer);
    static int32_t win_SetEvent(void* hEvent);
    static int32_t win_ResetEvent(void* hEvent);
    static void* win_CreateSemaphoreA(void* lpSemaphoreAttributes, int32_t lInitialCount, int32_t lMaximumCount, const char* lpName);
    static void* win_CreateSemaphoreW(void* lpSemaphoreAttributes, int32_t lInitialCount, int32_t lMaximumCount, const wchar_t* lpName);
    static void* win_CreateSemaphoreExA(void* lpSemaphoreAttributes, int32_t lInitialCount, int32_t lMaximumCount, const char* lpName, uint32_t dwFlags, uint32_t dwDesiredAccess);
    static void* win_CreateSemaphoreExW(void* lpSemaphoreAttributes, int32_t lInitialCount, int32_t lMaximumCount, const wchar_t* lpName, uint32_t dwFlags, uint32_t dwDesiredAccess);
    static int32_t win_ReleaseSemaphore(void* hSemaphore, int32_t lReleaseCount, int32_t* lpPreviousCount);
    static void* win_OpenSemaphoreW(uint32_t dwDesiredAccess, int32_t bInheritHandle, const wchar_t* lpName);
    static void* win_CreateMutexA(void* lpMutexAttributes, int32_t bInitialOwner, const char* lpName);
    static void* win_CreateMutexW(void* lpMutexAttributes, int32_t bInitialOwner, const wchar_t* lpName);
    static void* win_CreateMutexExA(void* lpMutexAttributes, const char* lpName, uint32_t dwFlags, uint32_t dwDesiredAccess);
    static void* win_CreateMutexExW(void* lpMutexAttributes, const wchar_t* lpName, uint32_t dwFlags, uint32_t dwDesiredAccess);
    static int32_t win_ReleaseMutex(void* hMutex);
    static void* win_OpenMutexW(uint32_t dwDesiredAccess, int32_t bInheritHandle, const wchar_t* lpName);
    static uint32_t win_WaitForSingleObject(void* hHandle, uint32_t dwMilliseconds);
    static uint32_t win_WaitForSingleObjectEx(void* hHandle, uint32_t dwMilliseconds, int32_t bAlertable);
    static uint32_t win_WaitForMultipleObjects(uint32_t nCount, void* const* lpHandles, int32_t bWaitAll, uint32_t dwMilliseconds);
    static uint32_t win_WaitForMultipleObjectsEx(uint32_t nCount, void* const* lpHandles, int32_t bWaitAll, uint32_t dwMilliseconds, int32_t bAlertable);
    static uint32_t win_ResumeThread(void* hThread);
    static uint32_t win_SuspendThread(void* hThread);
    /// Llama a los VEH registrados y al filtro de nivel superior con EXCEPTION_POINTERS en memoria
    /// invitada. Devuelve EXCEPTION_CONTINUE_EXECUTION (-1) o EXCEPTION_CONTINUE_SEARCH (0).
    static int32_t dispatchGuestException(class OryonEmitterBackend& backend, uint64_t exceptionPointers);
    /// Contexto x64 real de un hilo invitado detenido (o en código nativo) para el GC de Mono.
    static int32_t win_GetThreadContext(void* hThread, void* lpContext);
    static int32_t win_SetThreadContext(void* hThread, const void* lpContext);
    static void* win_OpenThread(uint32_t dwDesiredAccess, int32_t bInheritHandle, uint32_t dwThreadId);
    static uint32_t win_GetThreadId(void* hThread);
    static int32_t win_SetThreadDescription(void* hThread, const wchar_t* lpThreadDescription);
    static int32_t win_CoInitialize(void* pvReserved);
    static int32_t win_CoInitializeEx(void* pvReserved, uint32_t dwCoInit);
    static void win_CoUninitialize();
    static int32_t win_CoCreateInstance(const void* rclsid, void* pUnkOuter, uint32_t dwClsContext,
                                        const void* riid, void** ppv);
    static int32_t win_GetExitCodeThread(void* hThread, uint32_t* lpExitCode);
    static int32_t win_GetExitCodeProcess(void* hProcess, uint32_t* lpExitCode);
    static int32_t win_GetLogicalProcessorInformation(void* Buffer, uint32_t* ReturnedLength);
    static int32_t win_GetLogicalProcessorInformationEx(uint32_t RelationshipType, void* Buffer, uint32_t* ReturnedLength);
    static int32_t win_BCryptGenRandom(void* hAlgorithm, uint8_t* pbBuffer, uint32_t cbBuffer, uint32_t dwFlags);
    static int32_t win_BCryptOpenAlgorithmProvider(void** phAlgorithm, const wchar_t* pszAlgId, const wchar_t* pszImplementation, uint32_t dwFlags);
    static int32_t win_BCryptCloseAlgorithmProvider(void* hAlgorithm, uint32_t dwFlags);
    static int32_t win_CryptAcquireContextW(void** phProv, const wchar_t* szContainer, const wchar_t* szProvider, uint32_t dwProvType, uint32_t dwFlags);
    static int32_t win_CryptAcquireContextA(void** phProv, const char* szContainer, const char* szProvider, uint32_t dwProvType, uint32_t dwFlags);
    static int32_t win_CryptReleaseContext(void* hProv, uint32_t dwFlags);
    static int32_t win_CryptGenRandom(void* hProv, uint32_t dwLen, uint8_t* pbBuffer);
    static uint8_t win_SystemFunction036(void* randomBuffer, uint32_t randomBufferLength);
    static uint32_t win_GetFileVersionInfoSizeW(const wchar_t* fileName, uint32_t* handle);
    static uint32_t win_GetFileVersionInfoSizeA(const char* fileName, uint32_t* handle);
    static int32_t win_GetFileVersionInfoW(const wchar_t* fileName, uint32_t handle, uint32_t len, void* data);
    static int32_t win_GetFileVersionInfoA(const char* fileName, uint32_t handle, uint32_t len, void* data);
    static int32_t win_VerQueryValueW(const void* block, const wchar_t* subBlock, void** buffer, uint32_t* len);
    static int32_t win_VerQueryValueA(const void* block, const char* subBlock, void** buffer, uint32_t* len);
    static int32_t win_SwitchToThread();
    static void* win_GetCurrentThread();
    static int32_t win_SetThreadPriority(void* hThread, int32_t nPriority);
    static int32_t win_GetThreadPriority(void* hThread);
    static void* win_InterlockedPushEntrySList(void* ListHead, void* ListEntry);
    static void* win_InterlockedPopEntrySList(void* ListHead);
    static void* win_InterlockedFlushSList(void* ListHead);
    static int32_t win_TlsFree(uint32_t dwTlsIndex);
    static uint32_t win_FlsAlloc(void* lpCallback);
    static void* win_FlsGetValue(uint32_t dwFlsIndex);
    static int32_t win_FlsSetValue(uint32_t dwFlsIndex, void* lpFlsData);
    static int32_t win_FlsFree(uint32_t dwFlsIndex);

    // Memoria, Tiempo y Sistema de Archivos
    static void win_GetSystemTimeAsFileTime(void* lpSystemTimeAsFileTime);
    static void win_GetLocalTime(void* lpSystemTime);
    static int32_t win_SystemTimeToFileTime(const void* lpSystemTime, void* lpFileTime);
    static void* win_GetProcessHeap();
    static size_t win_HeapSize(void* hHeap, uint32_t dwFlags, const void* lpMem);
    static void* win_HeapReAlloc(void* hHeap, uint32_t dwFlags, void* lpMem, size_t dwBytes);
    static int32_t win_FlushFileBuffers(void* hFile);
    static int32_t win_SetFilePointerEx(void* hFile, int64_t liDistanceToMove, int64_t* lpNewFilePointer, uint32_t dwMoveMethod);
    static void* win_FindFirstFileExW(const void* lpFileName, int32_t fInfoLevelId, void* lpFindFileData, int32_t fSearchOp, void* lpSearchFilter, uint32_t dwAdditionalFlags);
    static int32_t win_FindNextFileW(void* hFindFile, void* lpFindFileData);
    static int32_t win_FindClose(void* hFindFile);
    static void* win_LoadLibraryExW(const void* lpLibFileName, void* hFile, uint32_t dwFlags);
    static int32_t win_FreeLibrary(void* hLibModule);

    // Strings y Locale
    static int32_t win_MultiByteToWideChar(uint32_t CodePage, uint32_t dwFlags, const char* lpMultiByteStr, int32_t cbMultiByte, void* lpWideCharStr, int32_t cchWideChar);
    static int32_t win_WideCharToMultiByte(uint32_t CodePage, uint32_t dwFlags, const void* lpWideCharStr, int32_t cchWideChar, char* lpMultiByteStr, int32_t cbMultiByte, const char* lpDefaultChar, int32_t* lpUsedDefaultChar);
    static int32_t win_IsValidCodePage(uint32_t CodePage);
    static uint32_t win_GetACP();
    static uint32_t win_GetOEMCP();
    static int32_t win_GetCPInfo(uint32_t CodePage, void* lpCPInfo);
    static int32_t win_GetStringTypeW(uint32_t dwInfoType, const void* lpSrcStr, int32_t cchSrc, uint16_t* lpCharType);
    static int32_t win_LCMapStringW(uint32_t Locale, uint32_t dwMapFlags, const void* lpSrcStr, int32_t cchSrc, void* lpDestStr, int32_t cchDest);

    // User32 Window & Monitor Extensions
    static void* win_MonitorFromWindow(void* hwnd, uint32_t dwFlags);
    static void* win_MonitorFromPoint(int64_t pt, uint32_t dwFlags);
    static void* win_MonitorFromRect(const void* lprc, uint32_t dwFlags);
    static int32_t win_GetMonitorInfoW(void* hMonitor, void* lpmi);
    static int32_t win_GetMonitorInfoA(void* hMonitor, void* lpmi);
    static int32_t win_EnumDisplayMonitors(void* hdc, const void* lprcClip, void* lpfnEnum, int64_t dwData);
    static int32_t win_EnumDisplaySettingsW(const void* lpszDeviceName, uint32_t iModeNum, void* lpDevMode);
    static int32_t win_EnumDisplaySettingsA(const void* lpszDeviceName, uint32_t iModeNum, void* lpDevMode);
    static int32_t win_GetWindowLongW(void* hWnd, int32_t nIndex);
    static int32_t win_GetWindowLongA(void* hWnd, int32_t nIndex);
    static int64_t win_GetWindowLongPtrW(void* hWnd, int32_t nIndex);
    static int64_t win_GetWindowLongPtrA(void* hWnd, int32_t nIndex);
    static int32_t win_SetWindowLongW(void* hWnd, int32_t nIndex, int32_t dwNewLong);
    static int32_t win_SetWindowLongA(void* hWnd, int32_t nIndex, int32_t dwNewLong);
    static int64_t win_SetWindowLongPtrW(void* hWnd, int32_t nIndex, int64_t dwNewLong);
    static int64_t win_SetWindowLongPtrA(void* hWnd, int32_t nIndex, int64_t dwNewLong);
    static uint16_t win_RegisterClassW(const void* lpWndClass);
    static uint16_t win_RegisterClassExW(const void* lpWndClassEx);
    static int32_t win_UnregisterClassW(const void* lpClassName, void* hInstance);
    static int32_t win_DestroyWindow(void* hWnd);
    static uintptr_t win_SetTimer(void* hWnd, uintptr_t nIDEvent, uint32_t uElapse, void* lpTimerFunc);
    static int32_t win_KillTimer(void* hWnd, uintptr_t uIDEvent);
    static int32_t win_SystemParametersInfoW(uint32_t uiAction, uint32_t uiParam, void* pvParam, uint32_t fWinIni);
    static int32_t win_GetCursorPos(void* lpPoint);
    static int32_t win_SetCursorPos(int32_t X, int32_t Y);
    static int32_t win_ClipCursor(const void* lpRect);
    static int32_t win_ClientToScreen(void* hWnd, void* lpPoint);
    static int32_t win_ScreenToClient(void* hWnd, void* lpPoint);
    static int32_t win_AllowSetForegroundWindow(uint32_t dwProcessId);

    /// CreateDirectoryW/A: mkdir sobre la ruta virtual (D:\ juego, C:\users -> _HyperPlaySaves).
    static int32_t win_CreateDirectoryW(const void* lpPathName, void* lpSecurityAttributes);
    static int32_t win_CreateDirectoryA(const char* lpPathName, void* lpSecurityAttributes);

    // SHLWAPI: rutas con semántica Win32 real (Win32PathUtils.h). Las variantes W usan WCHAR de 16 bits.
    static int32_t win_PathCanonicalizeW(void* pszBuf, const void* pszPath);
    static int32_t win_PathCanonicalizeA(char* pszBuf, const char* pszPath);
    static void* win_PathCombineW(void* pszDest, const void* pszDir, const void* pszFile);
    static char* win_PathCombineA(char* pszDest, const char* pszDir, const char* pszFile);
    static int32_t win_PathAppendW(void* pszPath, const void* pszMore);
    static int32_t win_PathAppendA(char* pszPath, const char* pszMore);
    static int32_t win_PathRemoveFileSpecW(void* pszPath);
    static int32_t win_PathRemoveFileSpecA(char* pszPath);
    static void* win_PathFindFileNameW(void* pszPath);
    static char* win_PathFindFileNameA(char* pszPath);
    static void* win_PathFindExtensionW(void* pszPath);
    static char* win_PathFindExtensionA(char* pszPath);
    static int32_t win_PathFileExistsW(const void* pszPath);
    static int32_t win_PathFileExistsA(const char* pszPath);
    static int32_t win_PathIsRelativeW(const void* pszPath);
    static int32_t win_PathIsRelativeA(const char* pszPath);

    // Shell32 & Known Folders
    /// shell32!DllGetVersion: rellena DLLVERSIONINFO/DLLVERSIONINFO2 con la versión emulada.
    static int32_t win_DllGetVersion(void* pdvi);
    static int32_t win_SHGetKnownFolderPath(const void* rfid, uint32_t dwFlags, void* hToken, void** ppszPath);
    static int32_t win_SHGetFolderPathW(void* hwnd, int32_t csidl, void* hToken, uint32_t dwFlags, void* pszPath);
    static int32_t win_SHGetFolderPathA(void* hwnd, int32_t csidl, void* hToken, uint32_t dwFlags, char* pszPath);
    static int32_t win_SHGetSpecialFolderPathW(void* hwnd, void* pszPath, int32_t csidl, int32_t fCreate);
    static int32_t win_SHGetSpecialFolderPathA(void* hwnd, char* pszPath, int32_t csidl, int32_t fCreate);
    static int32_t win_ShellExecuteW(void* hwnd, const void* lpOperation, const void* lpFile, const void* lpParameters, const void* lpDirectory, int32_t nShowCmd);
    static int32_t win_ShellExecuteA(void* hwnd, const char* lpOperation, const char* lpFile, const char* lpParameters, const char* lpDirectory, int32_t nShowCmd);

    // Ole32 / COM Task Memory & GUIDs
    static void* win_CoTaskMemAlloc(size_t cb);
    static void win_CoTaskMemFree(void* pv);
    static void* win_CoTaskMemRealloc(void* pv, size_t cb);
    static int32_t win_CoCreateGuid(void* pguid);
    static int32_t win_PropVariantClear(void* pvar);

    // DirectX / Direct3D Native Bionic Bridges
    static int32_t win_CreateDXGIFactory(const void* riid, void** ppFactory);
    static int32_t win_CreateDXGIFactory1(const void* riid, void** ppFactory);
    static int32_t win_CreateDXGIFactory2(uint32_t flags, const void* riid, void** ppFactory);
    static int32_t win_D3D11CreateDevice(void* pAdapter, int32_t DriverType, void* Software,
                                         uint32_t Flags, const void* pFeatureLevels, uint32_t FeatureLevels,
                                         uint32_t SDKVersion, void** ppDevice, void* pFeatureLevel,
                                         void** ppImmediateContext);
    static int32_t win_D3D11CreateDeviceAndSwapChain(void* pAdapter, int32_t DriverType, void* Software,
                                                     uint32_t Flags, const void* pFeatureLevels, uint32_t FeatureLevels,
                                                     uint32_t SDKVersion, const void* pSwapChainDesc,
                                                     void** ppSwapChain, void** ppDevice,
                                                     void* pFeatureLevel, void** ppImmediateContext);
    static void* win_Direct3DCreate9(uint32_t SDKVersion);
};

}  // namespace hyperplay
