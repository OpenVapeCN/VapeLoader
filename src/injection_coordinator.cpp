#include "injection_coordinator.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace {
bool enableDebugPrivilege() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
        return false;
    TOKEN_PRIVILEGES privileges{};
    privileges.PrivilegeCount = 1;
    LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &privileges.Privileges[0].Luid);
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    AdjustTokenPrivileges(token, FALSE, &privileges, sizeof(privileges), nullptr, nullptr);
    const bool success = GetLastError() == ERROR_SUCCESS;
    CloseHandle(token);
    return success;
}

const IMAGE_SECTION_HEADER* sectionForRva(const IMAGE_NT_HEADERS64* nt, DWORD rva) {
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section) {
        const DWORD size = std::max(section->Misc.VirtualSize, section->SizeOfRawData);
        if (rva >= section->VirtualAddress && rva < section->VirtualAddress + size) return section;
    }
    return nullptr;
}

const unsigned char* rvaPointer(const std::vector<unsigned char>& image,
                            const IMAGE_NT_HEADERS64* nt, DWORD rva) {
    if (rva < nt->OptionalHeader.SizeOfHeaders) return image.data() + rva;
    const auto* section = sectionForRva(nt, rva);
    if (!section) return nullptr;
    const std::size_t offset = section->PointerToRawData + (rva - section->VirtualAddress);
    return offset < image.size() ? image.data() + offset : nullptr;
}

DWORD reflectiveLoaderRva(const std::vector<unsigned char>& image) {
    if (image.size() < sizeof(IMAGE_DOS_HEADER)) return 0;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
        static_cast<std::size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > image.size()) return 0;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return 0;
    const auto directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    const auto* exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(
        rvaPointer(image, nt, directory.VirtualAddress));
    if (!exports) return 0;
    const auto* names = reinterpret_cast<const DWORD*>(rvaPointer(image, nt, exports->AddressOfNames));
    const auto* ordinals = reinterpret_cast<const WORD*>(rvaPointer(image, nt, exports->AddressOfNameOrdinals));
    const auto* functions = reinterpret_cast<const DWORD*>(rvaPointer(image, nt, exports->AddressOfFunctions));
    if (!names || !ordinals || !functions) return 0;
    for (DWORD index = 0; index < exports->NumberOfNames; ++index) {
        const auto* name = reinterpret_cast<const char*>(rvaPointer(image, nt, names[index]));
        if (name && (strstr(name, "?tim@@") != nullptr || strstr(name, "ReflectiveLoader") != nullptr))
            return functions[ordinals[index]];
    }
    return 0;
}
}

bool InjectionCoordinator::injectReflectiveDll(std::uint32_t processId,
    const std::wstring& dllPath, std::uint16_t controllerPort, std::wstring& error) {
    std::ifstream input(dllPath, std::ios::binary);
    if (!input) {
        error = L"vape_v4.dll was not found beside the controller";
        return false;
    }
    std::vector<unsigned char> image((std::istreambuf_iterator<char>(input)), {});
    const DWORD loaderRva = reflectiveLoaderRva(image);
    if (!loaderRva) {
        error = L"Reflective loader export was not found";
        return false;
    }
    enableDebugPrivilege();
    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, processId);
    if (!process) {
        error = L"Failed to open Minecraft process";
        return false;
    }
    void* remote = VirtualAllocEx(process, nullptr, image.size(), MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE);
    SIZE_T written = 0;
    if (!remote || !WriteProcessMemory(process, remote, image.data(), image.size(), &written) ||
        written != image.size()) {
        error = L"Failed to write reflective image";
        if (remote) VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }
    auto start = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        static_cast<std::byte*>(remote) + loaderRva);
    HANDLE thread = CreateRemoteThread(process, nullptr, 0, start,
        reinterpret_cast<void*>(static_cast<std::uintptr_t>(controllerPort)), 0, nullptr);
    if (!thread) {
        error = L"Failed to start reflective loader";
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }
    WaitForSingleObject(thread, 30000);
    DWORD exitCode = 0;
    GetExitCodeThread(thread, &exitCode);
    CloseHandle(thread);
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    CloseHandle(process);
    if (exitCode == 0) {
        error = L"Reflective loader returned failure";
        return false;
    }
    return true;
}
