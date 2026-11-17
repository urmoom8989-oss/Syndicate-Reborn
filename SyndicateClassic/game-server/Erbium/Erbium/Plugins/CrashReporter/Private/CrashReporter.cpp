#include "pch.h"
#include "../Public/CrashReporter.h"
#include <TlHelp32.h>
#include <winternl.h>
#include <sstream>
#pragma comment(lib, "ntdll.lib")

void FreezeOtherThreads()
{
    auto thrHandle = GetCurrentThread();
    auto currentThr = GetThreadId(thrHandle);
    auto currentPrc = GetProcessIdOfThread(thrHandle);
    HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (h != INVALID_HANDLE_VALUE)
    {
        THREADENTRY32 te;
        te.dwSize = sizeof(te);
        if (Thread32First(h, &te))
    {
            do {
                if (te.dwSize >= FIELD_OFFSET(THREADENTRY32, th32OwnerProcessID) +
                    sizeof(te.th32OwnerProcessID))
    {
                    if (te.th32ThreadID != currentThr && te.th32OwnerProcessID == currentPrc)
                    {
                        auto thr = OpenThread(THREAD_ALL_ACCESS, false, te.th32ThreadID);

                        if (thr != INVALID_HANDLE_VALUE)
                        {
                            SuspendThread(thr);
                            CloseHandle(thr);
                        }
                    }
                }
                te.dwSize = sizeof(te);
            } while (Thread32Next(h, &te));
        }
        CloseHandle(h);
    }
}

DWORD FormatNtStatus(NTSTATUS nsCode, TCHAR** ppszMessage) 
{
    HMODULE ntdll = LoadLibraryA("ntdll.dll");

    if (ntdll == NULL) 
        return 0;

    DWORD outLen = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_FROM_HMODULE,
        ntdll, RtlNtStatusToDosError(nsCode), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)ppszMessage, 0, NULL);

    FreeLibrary(ntdll);

    return outLen;
}

LONG WINAPI ErbiumUnhandledExceptionFilter(LPEXCEPTION_POINTERS ExceptionInfo)
{
    // A deliberate probe (FErbiumProbeScope) handles its own faults.
    if (ErbiumProbing > 0)
        return EXCEPTION_CONTINUE_SEARCH;
    if ((ExceptionInfo->ExceptionRecord->ExceptionCode & 0x80000000) == 0 || (ExceptionInfo->ExceptionRecord->ExceptionCode & 0x30000000) != 0)
        return EXCEPTION_CONTINUE_SEARCH;

    // Preserve the original fault before invoking the game's old DbgHelp DLL.
    // A nested symbol-handler failure must not replace the useful exception.
    static volatile LONG Reporting = 0;
    if (InterlockedCompareExchange(&Reporting, 1, 0) != 0)
        return EXCEPTION_CONTINUE_SEARCH;
    const auto Record = ExceptionInfo->ExceptionRecord;
    if ((Record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION || Record->ExceptionCode == EXCEPTION_IN_PAGE_ERROR)
        && Record->NumberParameters > 1 && Record->ExceptionInformation[1] == 0xFFFFF78000000900)
    {
        InterlockedExchange(&Reporting, 0);
        return EXCEPTION_CONTINUE_SEARCH;
    }
    printf("[CrashReporter] ORIGINAL code=0x%08lx address=%p hostOffset=0x%llx operation=%llu target=0x%llx\n",
        Record->ExceptionCode, Record->ExceptionAddress,
        (unsigned long long)((uintptr_t)Record->ExceptionAddress - ImageBase),
        Record->NumberParameters > 0 ? (unsigned long long)Record->ExceptionInformation[0] : 0,
        Record->NumberParameters > 1 ? (unsigned long long)Record->ExceptionInformation[1] : 0);
    fflush(stdout);
    // Stack exhaustion cannot safely run allocation-heavy symbol reporting.
    if (Record->ExceptionCode == EXCEPTION_STACK_OVERFLOW)
        return EXCEPTION_CONTINUE_SEARCH;


    STACKFRAME64 stackFrame{};

    char symName[1024 * sizeof(TCHAR)];
    char symStorage[sizeof(IMAGEHLP_SYMBOL64) + sizeof(symName)]{};
    auto sym = (IMAGEHLP_SYMBOL64*)symStorage;

    auto currentPrc = GetCurrentProcess();
    auto currentThr = GetCurrentThread();
    DWORD64 displacement = 0;
    stackFrame.AddrPC.Offset = ExceptionInfo->ContextRecord->Rip;
    stackFrame.AddrPC.Mode = AddrModeFlat;
    stackFrame.AddrStack.Offset = ExceptionInfo->ContextRecord->Rsp;
    stackFrame.AddrStack.Mode = AddrModeFlat;
    stackFrame.AddrFrame.Offset = ExceptionInfo->ContextRecord->Rbp;
    stackFrame.AddrFrame.Mode = AddrModeFlat;

    SymCleanup(currentPrc);

    auto InitResult = SymInitialize(currentPrc, nullptr, true);

    if (!InitResult)
        printf("[CrashReporter] Failed to initialize symbol finder!");

    std::stringstream reportStream;
    
    reportStream << "[CrashReporter] Caught unhandled exception (Code: ";
    char code[9];

    switch (ExceptionInfo->ExceptionRecord->ExceptionCode)
    {
#define NAMED_EX(x) case x: reportStream << #x; break;

        NAMED_EX(EXCEPTION_ACCESS_VIOLATION);
        NAMED_EX(EXCEPTION_ARRAY_BOUNDS_EXCEEDED);
        NAMED_EX(EXCEPTION_BREAKPOINT);
        NAMED_EX(EXCEPTION_DATATYPE_MISALIGNMENT);
        NAMED_EX(EXCEPTION_FLT_DENORMAL_OPERAND);
        NAMED_EX(EXCEPTION_FLT_DIVIDE_BY_ZERO);
        NAMED_EX(EXCEPTION_FLT_INEXACT_RESULT);
        NAMED_EX(EXCEPTION_FLT_INVALID_OPERATION);
        NAMED_EX(EXCEPTION_FLT_OVERFLOW);
        NAMED_EX(EXCEPTION_FLT_STACK_CHECK);
        NAMED_EX(EXCEPTION_FLT_UNDERFLOW);
        NAMED_EX(EXCEPTION_ILLEGAL_INSTRUCTION);
        NAMED_EX(EXCEPTION_INT_DIVIDE_BY_ZERO);
        NAMED_EX(EXCEPTION_INT_OVERFLOW);
        NAMED_EX(EXCEPTION_INVALID_DISPOSITION);
        NAMED_EX(EXCEPTION_NONCONTINUABLE_EXCEPTION);
        NAMED_EX(EXCEPTION_PRIV_INSTRUCTION);
        NAMED_EX(EXCEPTION_SINGLE_STEP);
        NAMED_EX(EXCEPTION_STACK_OVERFLOW);

#undef NAMED_EX
    default:
        snprintf(code, 9, "%08x", ExceptionInfo->ExceptionRecord->ExceptionCode);
        reportStream << code;
    }
    reportStream << ")";

    if (ExceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION || ExceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_IN_PAGE_ERROR)
    {
        switch (ExceptionInfo->ExceptionRecord->ExceptionInformation[0])
        {
        case 0:
            reportStream << "\n- Trying to read ";
            break;
        case 1:
            reportStream << "\n- Trying to write ";
            break;
        case 8:
            reportStream << "\n- Trying to execute ";
            break;
        }
        if (ExceptionInfo->ExceptionRecord->ExceptionInformation[1] == 0xFFFFF78000000900) // fn anti debug check ig
            return EXCEPTION_CONTINUE_SEARCH;

        char addr[19];
        snprintf(addr, 19, "0x%016llx", ExceptionInfo->ExceptionRecord->ExceptionInformation[1]);
        reportStream << addr;
    }
    reportStream << "\n\n";

    auto contextCpy = *ExceptionInfo->ContextRecord;
    contextCpy.ContextFlags = CONTEXT_ALL;
    for (int frame = 0; InitResult && frame < 128; frame++)
    {

        auto result = StackWalk
        (
            IMAGE_FILE_MACHINE_AMD64,
            currentPrc,
            currentThr,
            &stackFrame,
            &contextCpy,
            NULL,
            SymFunctionTableAccess64,
            SymGetModuleBase64,
            NULL
        );

        if (result == false)
            break;

        char addr[19];
        snprintf(addr, 19, "0x%016llx", stackFrame.AddrPC.Offset);
        reportStream << addr;

        auto imageBase = SymGetModuleBase64(currentPrc, stackFrame.AddrPC.Offset);
        if (imageBase)
        {
            char path[1024];
            GetModuleFileNameA(HMODULE(imageBase), path, 1024);

            auto filteredPath = strrchr(path, '\\');

            char offsetStr[19];
            snprintf(offsetStr, 19, "0x%llx", stackFrame.AddrPC.Offset - imageBase);
            reportStream << " (" << (filteredPath ? filteredPath + 1 : path) << "+" << offsetStr << ")";
        }
        reportStream << ": ";

        sym->SizeOfStruct = sizeof(IMAGEHLP_SYMBOL64);
        sym->MaxNameLength = sizeof(symName);
        
        BOOL SymResult = SymGetSymFromAddr64(currentPrc, (ULONG64)stackFrame.AddrPC.Offset, &displacement, sym);
        if (SymResult == false || imageBase == ImageBase)
            reportStream << "[unknown]\n";
        else
        {
            UnDecorateSymbolName(sym->Name, (PSTR)symName, sizeof(symName), UNDNAME_COMPLETE);

            reportStream << sym->Name << "()";
            IMAGEHLP_LINE64	ImageHelpLine = { 0 };
            ImageHelpLine.SizeOfStruct = sizeof(ImageHelpLine);
            if (SymGetLineFromAddr64(currentPrc, (ULONG64)stackFrame.AddrPC.Offset, (::DWORD*)&displacement, &ImageHelpLine))
            {
                auto filteredName = strrchr(ImageHelpLine.FileName, '\\');
                reportStream << " [" << (filteredName ? filteredName + 1 : ImageHelpLine.FileName) << ":" << ImageHelpLine.LineNumber << "]";
            }
            reportStream << "\n";
        }
    }
    auto reportStr = reportStream.str();
    printf("%s", reportStr.c_str());

    fflush(stdout);
    SymCleanup(currentPrc);
    Sleep(3000);
    //while (true) {}
    TerminateProcess(GetCurrentProcess(), ExceptionInfo->ExceptionRecord->ExceptionCode);
    //ExitProcess(ExceptionInfo->ExceptionRecord->ExceptionCode);
    return EXCEPTION_CONTINUE_EXECUTION;
}

void FCrashReporter::Register()
{
    AddVectoredExceptionHandler(0, ErbiumUnhandledExceptionFilter);
}
