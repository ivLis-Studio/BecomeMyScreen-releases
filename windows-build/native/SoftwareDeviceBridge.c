// User-mode bridge only; the signed upstream display driver is unchanged.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <swdevice.h>

// SwDeviceCreate pins the module containing its callback with GetModuleHandleEx.
// A .NET delegate thunk lives in generated memory, not a loaded PE module, and
// therefore fails with HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND). Keep the actual
// OS callback in this DLL, forwarding to the rooted managed delegate afterwards.
typedef struct BMS_DEVICE {
    HSWDEVICE device;
    SW_DEVICE_CREATE_CALLBACK callback;
    PVOID context;
} BMS_DEVICE;

static VOID CALLBACK DeviceCreated(HSWDEVICE device, HRESULT result,
                                   PVOID context, PCWSTR instanceId)
{
    BMS_DEVICE *owner = (BMS_DEVICE *)context;
    owner->callback(device, result, owner->context, instanceId);
}

__declspec(dllexport) HRESULT WINAPI BmsSwDeviceCreate(
    PCWSTR enumerator, PCWSTR parent, const SW_DEVICE_CREATE_INFO *info,
    ULONG propertyCount, const DEVPROPERTY *properties,
    SW_DEVICE_CREATE_CALLBACK callback, PVOID context, BMS_DEVICE **result)
{
    if (!result) return E_POINTER;
    *result = NULL;
    if (!callback || !info) return E_INVALIDARG;
    BMS_DEVICE *owner = (BMS_DEVICE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(BMS_DEVICE));
    if (!owner) return E_OUTOFMEMORY;
    owner->callback = callback;
    owner->context = context;
    HRESULT hr = SwDeviceCreate(enumerator, parent, info, propertyCount, properties,
                                DeviceCreated, owner, &owner->device);
    if (FAILED(hr)) {
        if (owner->device) SwDeviceClose(owner->device);
        HeapFree(GetProcessHeap(), 0, owner);
        return hr;
    }
    *result = owner;
    return hr;
}

__declspec(dllexport) VOID WINAPI BmsSwDeviceClose(BMS_DEVICE *owner)
{
    if (!owner) return;
    // Windows guarantees all callbacks have completed when this returns.
    SwDeviceClose(owner->device);
    HeapFree(GetProcessHeap(), 0, owner);
}
