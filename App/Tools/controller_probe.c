#define DIRECTINPUT_VERSION 0x0800
#define COBJMACROS
#include <windows.h>
#include <dinput.h>
#include <hidsdi.h>
#include <setupapi.h>
#include <stdio.h>
#include <stdlib.h>

struct controller_id
{
    unsigned int vendor;
    unsigned int product;
    BOOL direct_input;
};

static BOOL CALLBACK match_controller(const DIDEVICEINSTANCEW *device, void *context)
{
    struct controller_id *controller = context;
    if (device->guidProduct.Data1 != (DWORD)MAKELONG(controller->vendor, controller->product))
        return DIENUM_CONTINUE;
    controller->direct_input = TRUE;
    return DIENUM_STOP;
}

static HRESULT check_direct_input(struct controller_id *controller)
{
    IDirectInput8W *input = NULL;
    HRESULT result = DirectInput8Create(GetModuleHandleW(NULL), DIRECTINPUT_VERSION,
                                        &IID_IDirectInput8W, (void **)&input, NULL);
    if (FAILED(result)) return result;
    result = IDirectInput8_EnumDevices(input, DI8DEVCLASS_GAMECTRL, match_controller,
                                      controller, DIEDFL_ATTACHEDONLY);
    IDirectInput8_Release(input);
    return result;
}

static BOOL matches_interface(const WCHAR *path, const WCHAR *id)
{
    for (; *path; ++path)
        if (!_wcsnicmp(path, id, wcslen(id))) return TRUE;
    return FALSE;
}

static DWORD check_hid(const struct controller_id *controller, BOOL *present, BOOL *open)
{
    SP_DEVICE_INTERFACE_DATA interface_data = {.cbSize = sizeof(interface_data)};
    WCHAR id[24];
    GUID hid_class;
    HDEVINFO devices;
    DWORD open_error = 0;

    swprintf(id, ARRAYSIZE(id), L"vid_%04x&pid_%04x", controller->vendor, controller->product);
    HidD_GetHidGuid(&hid_class);
    devices = SetupDiGetClassDevsW(&hid_class, NULL, NULL, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
    if (devices == INVALID_HANDLE_VALUE) return GetLastError();

    for (DWORD index = 0; SetupDiEnumDeviceInterfaces(devices, NULL, &hid_class, index, &interface_data); ++index)
    {
        SP_DEVICE_INTERFACE_DETAIL_DATA_W *detail;
        DWORD size = 0;
        HANDLE device;

        SetupDiGetDeviceInterfaceDetailW(devices, &interface_data, NULL, 0, &size, NULL);
        if (!size) continue;
        detail = malloc(size);
        if (!detail)
        {
            SetupDiDestroyDeviceInfoList(devices);
            return ERROR_OUTOFMEMORY;
        }
        detail->cbSize = sizeof(*detail);
        if (!SetupDiGetDeviceInterfaceDetailW(devices, &interface_data, detail, size, NULL, NULL) ||
            !matches_interface(detail->DevicePath, id))
        {
            free(detail);
            continue;
        }
        *present = TRUE;
        /* Match the access and sharing flags used by Wine's DirectInput HID backend. */
        device = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                             FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING, NULL);
        open_error = device == INVALID_HANDLE_VALUE ? GetLastError() : 0;
        free(detail);
        if (device == INVALID_HANDLE_VALUE) continue;
        CloseHandle(device);
        *open = TRUE;
        break;
    }
    SetupDiDestroyDeviceInfoList(devices);
    return *present ? open_error : 0;
}

int main(int argc, char **argv)
{
    struct controller_id controller = {0};
    BOOL present = FALSE, open = FALSE;
    DWORD error;
    char trailing;

    if (argc != 2 || sscanf(argv[1], "%4x:%4x%c", &controller.vendor, &controller.product, &trailing) != 2)
        return 2;
    if (FAILED(check_direct_input(&controller))) return 3;
    error = check_hid(&controller, &present, &open);
    if (!present && error) return 4;
    printf("METALFLIGHT_CONTROLLER 1 %d %d %d %lu\n", controller.direct_input, present, open, error);
    return 0;
}
