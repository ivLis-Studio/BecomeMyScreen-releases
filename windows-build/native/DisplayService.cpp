// Local display-device broker. The UI, USB connection and encoder run as the desktop user.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <swdevice.h>
#include <sddl.h>
#include <shlobj.h>
#include <setupapi.h>
#include <devguid.h>
#include <memory>
#include <string>
#include <array>
#include <utility>
#include <cstdio>

static constexpr wchar_t serviceName[] = L"BecomeMyScreenDisplay";
static constexpr wchar_t pipeName[] = L"\\\\.\\pipe\\BecomeMyScreen.Display.v1";
static HANDLE stopping;
static SERVICE_STATUS_HANDLE serviceStatus;
static std::wstring folder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &p))) throw ERROR_PATH_NOT_FOUND;
    std::wstring result(p); CoTaskMemFree(p); return result;
}
static std::wstring directory() { return folder(FOLDERID_ProgramFiles) + L"\\BecomeMyScreen Display Service"; }
static void check(DWORD error) { if (error != ERROR_SUCCESS) throw error; }
static void require(BOOL ok) { if (!ok) throw GetLastError(); }
struct Handle {
    HANDLE value;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
};
struct ServiceHandle {
    SC_HANDLE value;
    explicit ServiceHandle(SC_HANDLE h) : value(h) {}
    ~ServiceHandle() { if (value) CloseServiceHandle(value); }
    ServiceHandle(const ServiceHandle&) = delete;
};
struct Key { HKEY value{}; ~Key() { if (value) RegCloseKey(value); } };
static constexpr wchar_t vddKey[] = L"SOFTWARE\\MikeTheTech\\VirtualDisplayDriver";
static void configure(unsigned w, unsigned h, unsigned hz) {
    const auto root = directory(), legacy = folder(FOLDERID_ProgramData) + L"\\BecomeMyScreen\\DisplayDriver";
    Key key;
    check(RegCreateKeyExW(HKEY_LOCAL_MACHINE, vddKey, 0, nullptr, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key.value, nullptr));
    wchar_t prior[32768]{}; DWORD size = sizeof(prior);
    DWORD result = RegGetValueW(key.value, nullptr, L"VDDPATH", RRF_RT_REG_SZ, nullptr, prior, &size);
    if (result == ERROR_FILE_NOT_FOUND) {
        DWORD values = 0;
        check(RegQueryInfoKeyW(key.value, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &values, nullptr, nullptr, nullptr, nullptr));
        if (values || GetFileAttributesW(L"C:\\VirtualDisplayDriver\\vdd_settings.xml") != INVALID_FILE_ATTRIBUTES ||
            GetFileAttributesW(L"C:\\VirtualDisplayDriver\\option.txt") != INVALID_FILE_ATTRIBUTES) throw DWORD(ERROR_SHARING_VIOLATION);
    } else {
        check(result);
        if (_wcsicmp(prior, root.c_str()) && _wcsicmp(prior, legacy.c_str())) throw DWORD(ERROR_SHARING_VIOLATION);
    }
    const auto xml = std::string("<vdd_settings><monitors><count>1</count></monitors><gpu><friendlyname>default</friendlyname></gpu>")
        + "<resolutions><resolution><width>" + std::to_string(w) + "</width><height>" + std::to_string(h) +
        "</height><refresh_rate>" + std::to_string(hz) + "</refresh_rate>" + (hz > 60 ? "<refresh_rate>60</refresh_rate>" : "") +
        "</resolution></resolutions><logging><logging>false</logging><debuglogging>false</debuglogging><SendLogsThroughPipe>false</SendLogsThroughPipe></logging>"
        "<colour><SDR10bit>false</SDR10bit><HDRPlus>false</HDRPlus><ColourFormat>RGB</ColourFormat></colour>"
        "<cursor><HardwareCursor>false</HardwareCursor></cursor><edid><CustomEdid>false</CustomEdid></edid></vdd_settings>";
    const auto temp = root + L"\\vdd_settings.tmp";
    {
        Handle file(CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        require(file.value != INVALID_HANDLE_VALUE);
        DWORD n;
        require(WriteFile(file.value, xml.data(), static_cast<DWORD>(xml.size()), &n, nullptr));
        if (n != xml.size()) throw DWORD(ERROR_WRITE_FAULT);
    }
    require(MoveFileExW(temp.c_str(), (root + L"\\vdd_settings.xml").c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH));
    check(RegSetValueExW(key.value, L"VDDPATH", 0, REG_SZ, reinterpret_cast<const BYTE*>(root.c_str()), static_cast<DWORD>((root.size()+1)*sizeof(wchar_t))));
}

// Session zero has no interactive GDI desktop. Use present PnP adapters for the ownership check.
static bool anotherVdd() {
    HDEVINFO set = SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY, nullptr, nullptr, DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) throw GetLastError();
    bool found = false;
    SP_DEVINFO_DATA info{sizeof(info)};
    for (DWORD i = 0; !found && SetupDiEnumDeviceInfo(set, i, &info); ++i) {
        wchar_t ids[8192]{};
        if (SetupDiGetDeviceRegistryPropertyW(set, &info, SPDRP_HARDWAREID, nullptr, reinterpret_cast<BYTE*>(ids), sizeof(ids)-sizeof(wchar_t), nullptr))
            for (const wchar_t* id = ids; *id; id += wcslen(id) + 1)
                if (!_wcsicmp(id,L"MttVDD") || !_wcsicmp(id,L"Root\\MttVDD")) { found = true; break; }
    }
    DWORD error = GetLastError();
    SetupDiDestroyDeviceInfoList(set);
    if (!found && error != ERROR_NO_MORE_ITEMS) throw error;
    return found;
}
struct Request { DWORD op, width, height, refresh; BYTE id[16]; };
static_assert(sizeof(Request)==32);
struct Device {
    HSWDEVICE handle{};
    ~Device() { remove(); }
    void remove() { if (handle) { SwDeviceClose(handle); handle = nullptr; } }
    struct Completion { HANDLE event; HRESULT result{E_PENDING}; };
    static void CALLBACK created(HSWDEVICE, HRESULT hr, void* ctx, PCWSTR) {
        auto& c = *static_cast<Completion*>(ctx); c.result = hr; SetEvent(c.event);
    }
    DWORD apply(const Request& r) {
        if (r.op == 2) { remove(); return ERROR_SUCCESS; }
        if (r.op != 1 || r.width < 64 || r.width > 8192 || r.height < 64 || r.height > 8192 || r.refresh < 15 || r.refresh > 120)
            return ERROR_INVALID_PARAMETER;
        remove();
        for (int i=0; i<25 && anotherVdd(); ++i)
            if (WaitForSingleObject(stopping,200)==WAIT_OBJECT_0) return ERROR_OPERATION_ABORTED;
        if (anotherVdd()) return ERROR_SHARING_VIOLATION;
        configure(r.width,r.height,r.refresh);
        wchar_t id[33]{};
        for (size_t i=0; i<16; ++i) swprintf_s(id+i*2,33-i*2,L"%02x",static_cast<unsigned>(r.id[i]));
        SW_DEVICE_CREATE_INFO info{}; info.cbSize = sizeof(info); info.pszInstanceId = id;
        info.pszzHardwareIds = L"MttVDD\0"; info.pszzCompatibleIds = L"MttVDD\0";
        info.CapabilityFlags = SWDeviceCapabilitiesRemovable | SWDeviceCapabilitiesSilentInstall | SWDeviceCapabilitiesDriverRequired;
        info.pszDeviceDescription = L"BecomeMyScreen Display"; info.pszDeviceLocation = L"Android tablet";
        Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr)); require(event.value != nullptr);
        Completion callback{event.value};
        HRESULT hr = SwDeviceCreate(L"BecomeMyScreen",L"HTREE\\ROOT\\0",&info,0,nullptr,created,&callback,&handle);
        if (SUCCEEDED(hr)) {
            HANDLE events[] = {stopping,event.value};
            DWORD wait = WaitForMultipleObjects(2,events,FALSE,15000);
            hr = wait == WAIT_OBJECT_0+1 ? callback.result : HRESULT_FROM_WIN32(wait==WAIT_TIMEOUT?ERROR_TIMEOUT:ERROR_OPERATION_ABORTED);
        }
        if (FAILED(hr)) { remove(); return HRESULT_FACILITY(hr)==FACILITY_WIN32 ? HRESULT_CODE(hr) : static_cast<DWORD>(hr); }
        return ERROR_SUCCESS;
    }
};

enum class Operation { connect, read, write };
static bool transfer(HANDLE pipe, Operation op, void* buffer, DWORD size, DWORD session = 0) {
    Handle done(CreateEventW(nullptr,TRUE,FALSE,nullptr)); require(done.value!=nullptr);
    OVERLAPPED ov{}; ov.hEvent = done.value;
    DWORD bytes=0;
    BOOL ok = op == Operation::connect ? ConnectNamedPipe(pipe,&ov) : op == Operation::read ?
        ReadFile(pipe,buffer,size,&bytes,&ov) : WriteFile(pipe,buffer,size,&bytes,&ov);
    if (!ok) {
        DWORD error = GetLastError();
        if (op == Operation::connect && error == ERROR_PIPE_CONNECTED) return true;
        if (error != ERROR_IO_PENDING) return false;
        HANDLE events[] = {stopping,done.value};
        DWORD wait;
        do { wait = WaitForMultipleObjects(2,events,FALSE,session ? 1000 : INFINITE); }
        while (wait == WAIT_TIMEOUT && session == WTSGetActiveConsoleSessionId());
        if (wait != WAIT_OBJECT_0+1) {
            CancelIoEx(pipe,&ov); GetOverlappedResult(pipe,&ov,&bytes,TRUE); return false;
        }
        if (!GetOverlappedResult(pipe,&ov,&bytes,FALSE)) return false;
    }
    return op==Operation::connect || bytes==size;
}
static void serve() {
    PSECURITY_DESCRIPTOR sd = nullptr;
    require(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;IU)",SDDL_REVISION_1,&sd,nullptr));
    SECURITY_ATTRIBUTES security{sizeof(security),sd,FALSE};
    Handle pipe(CreateNamedPipeW(pipeName,PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,512,512,0,&security));
    LocalFree(sd); require(pipe.value!=INVALID_HANDLE_VALUE);
    while (WaitForSingleObject(stopping,0)==WAIT_TIMEOUT) {
        if (!transfer(pipe.value,Operation::connect,nullptr,0)) break;
        DWORD session=0;
        if (GetNamedPipeClientSessionId(pipe.value,&session) && session && session==WTSGetActiveConsoleSessionId()) {
            Device device;
            Request r{};
            while (session==WTSGetActiveConsoleSessionId() && transfer(pipe.value,Operation::read,&r,sizeof(r),session)) {
                DWORD result;
                try { result=device.apply(r); } catch (DWORD e) { result=e; }
                if (!transfer(pipe.value,Operation::write,&result,sizeof(result),session)) break;
            }
        }
        DisconnectNamedPipe(pipe.value);
    }
}
static void report(DWORD state,DWORD error=0) {
    SERVICE_STATUS status{}; status.dwServiceType=SERVICE_WIN32_OWN_PROCESS; status.dwCurrentState=state;
    status.dwControlsAccepted=state==SERVICE_RUNNING ? SERVICE_ACCEPT_STOP|SERVICE_ACCEPT_SHUTDOWN : 0;
    status.dwWin32ExitCode=error; status.dwWaitHint=state==SERVICE_STOP_PENDING ? 20000 : 0;
    SetServiceStatus(serviceStatus,&status);
}
static DWORD WINAPI control(DWORD code,DWORD,void*,void*) {
    if (code==SERVICE_CONTROL_STOP || code==SERVICE_CONTROL_SHUTDOWN) { report(SERVICE_STOP_PENDING); SetEvent(stopping); }
    return ERROR_SUCCESS;
}
static void WINAPI serviceMain(DWORD,LPWSTR*) {
    Handle stop(CreateEventW(nullptr,TRUE,FALSE,nullptr)); stopping=stop.value;
    serviceStatus=RegisterServiceCtrlHandlerExW(serviceName,control,nullptr);
    if (!serviceStatus) return;
    DWORD error=0;
    try { require(stopping!=nullptr); report(SERVICE_RUNNING); serve(); }
    catch (DWORD e) { error=e; } catch (...) { error=ERROR_GEN_FAILURE; }
    report(SERVICE_STOPPED,error);
}

static void waitStopped(SC_HANDLE service) {
    SERVICE_STATUS_PROCESS before{}; DWORD bytes=0;
    require(QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&before),sizeof(before),&bytes));
    Handle process(before.dwProcessId ? OpenProcess(SYNCHRONIZE,FALSE,before.dwProcessId) : nullptr);
    SERVICE_STATUS status{};
    if (before.dwCurrentState!=SERVICE_STOPPED && before.dwCurrentState!=SERVICE_STOP_PENDING &&
        !ControlService(service,SERVICE_CONTROL_STOP,&status) && GetLastError()!=ERROR_SERVICE_NOT_ACTIVE) throw GetLastError();
    for (int i=0;i<200;++i) {
        require(QueryServiceStatus(service,&status));
        if (status.dwCurrentState==SERVICE_STOPPED) {
            // Reporting STOPPED does not unmap the service executable. Wait for its
            // process to exit before replacing the image during an in-place update.
            if (process.value && WaitForSingleObject(process.value,20000)!=WAIT_OBJECT_0) throw DWORD(ERROR_TIMEOUT);
            return;
        }
        Sleep(100);
    }
    throw DWORD(ERROR_TIMEOUT);
}
static void replaceServiceImage(const std::wstring& stage,const std::wstring& target) {
    DWORD error=ERROR_SUCCESS;
    for (int i=0;i<60;++i) {
        if (MoveFileExW(stage.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) return;
        error=GetLastError();
        if (error!=ERROR_SHARING_VIOLATION && error!=ERROR_ACCESS_DENIED) break;
        Sleep(100);
    }
    std::fwprintf(stderr,L"Replacing display service image failed: %lu\n",error);
    throw error;
}
static void setup(bool uninstall) {
    const auto root=directory(), target=root+L"\\DisplayService.exe", quoted=L"\""+target+L"\"";
    ServiceHandle manager(OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT|SC_MANAGER_CREATE_SERVICE)); require(manager.value!=nullptr);
    ServiceHandle service(OpenServiceW(manager.value,serviceName,SERVICE_QUERY_STATUS|SERVICE_STOP|SERVICE_START|SERVICE_CHANGE_CONFIG|DELETE));
    if (!service.value && GetLastError()!=ERROR_SERVICE_DOES_NOT_EXIST) throw GetLastError();
    if (uninstall) {
        if (service.value) { waitStopped(service.value); require(DeleteService(service.value)); }
        Key key;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,vddKey,0,KEY_QUERY_VALUE|KEY_SET_VALUE,&key.value)==ERROR_SUCCESS) {
            wchar_t value[32768]{}; DWORD size=sizeof(value);
            if (RegGetValueW(key.value,nullptr,L"VDDPATH",RRF_RT_REG_SZ,nullptr,value,&size)==ERROR_SUCCESS && !_wcsicmp(value,root.c_str()))
                check(RegDeleteValueW(key.value,L"VDDPATH"));
        }
        DeleteFileW((root+L"\\vdd_settings.xml").c_str()); DeleteFileW(target.c_str()); RemoveDirectoryW(root.c_str());
        return;
    }
    if (!CreateDirectoryW(root.c_str(),nullptr) && GetLastError()!=ERROR_ALREADY_EXISTS) throw GetLastError();
    DWORD attributes=GetFileAttributesW(root.c_str());
    if (attributes==INVALID_FILE_ATTRIBUTES || !(attributes&FILE_ATTRIBUTE_DIRECTORY) || (attributes&FILE_ATTRIBUTE_REPARSE_POINT)) throw DWORD(ERROR_ACCESS_DENIED);
    wchar_t self[32768]; DWORD n=GetModuleFileNameW(nullptr,self,32768);
    if (!n || n>=32768) throw DWORD(ERROR_BAD_PATHNAME);
    const auto stage=root+L"\\DisplayService.new";
    require(CopyFileW(self,stage.c_str(),FALSE));
    if (service.value) waitStopped(service.value);
    try { replaceServiceImage(stage,target); }
    catch (...) { if (service.value) StartServiceW(service.value,0,nullptr); throw; }
    if (!service.value) {
        service.value=CreateServiceW(manager.value,serviceName,L"BecomeMyScreen Display",SERVICE_START,SERVICE_WIN32_OWN_PROCESS,
            SERVICE_AUTO_START,SERVICE_ERROR_NORMAL,quoted.c_str(),nullptr,nullptr,nullptr,nullptr,nullptr);
        require(service.value!=nullptr);
    } else {
        require(ChangeServiceConfigW(service.value,SERVICE_NO_CHANGE,SERVICE_AUTO_START,SERVICE_NO_CHANGE,quoted.c_str(),nullptr,nullptr,nullptr,nullptr,nullptr,nullptr));
    }
    require(StartServiceW(service.value,0,nullptr));
}
int wmain(int argc,wchar_t** argv) {
    try {
        if (argc==2 && (!_wcsicmp(argv[1],L"--install") || !_wcsicmp(argv[1],L"--uninstall"))) {
            setup(!_wcsicmp(argv[1],L"--uninstall")); return 0;
        }
        SERVICE_TABLE_ENTRYW entries[]={{const_cast<wchar_t*>(serviceName),serviceMain},{nullptr,nullptr}};
        require(StartServiceCtrlDispatcherW(entries)); return 0;
    } catch (DWORD e) { return static_cast<int>(e); } catch (...) { return ERROR_GEN_FAILURE; }
}
