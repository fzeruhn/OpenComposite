// When oc_conformance ends abnormally (abort, an uncaught C++ exception, a CRT invalid-parameter call, an access
// violation and the like), writes why and a symbolised stack to <out>.crash.txt, which run.py attaches to the record.
// The exit code stays what it would have been, so run.py's statuses don't change.

#include "conformance.h"

#ifdef _WIN32
#include <windows.h>

#include <dbghelp.h>

#include <csignal>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <mutex>
#include <string>
#include <typeinfo>

namespace {

std::string reportPath;
std::once_flag written;

void WriteStack(FILE* f, CONTEXT* context)
{
	HANDLE process = GetCurrentProcess();
	SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
	SymInitialize(process, nullptr, TRUE);

	void* frames[64] = {};
	USHORT count = 0;
#if defined(_M_X64)
	if (context) {
		// Walk from the faulting context, not from the handler
		CONTEXT copy = *context;
		STACKFRAME64 frame = {};
		frame.AddrPC.Offset = copy.Rip;
		frame.AddrPC.Mode = AddrModeFlat;
		frame.AddrFrame.Offset = copy.Rbp;
		frame.AddrFrame.Mode = AddrModeFlat;
		frame.AddrStack.Offset = copy.Rsp;
		frame.AddrStack.Mode = AddrModeFlat;
		while (count < 64 && StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame, &copy, nullptr,
		           SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
			if (!frame.AddrPC.Offset)
				break;
			frames[count++] = (void*)frame.AddrPC.Offset;
		}
	} else
#endif
	{
		count = CaptureStackBackTrace(1, 64, frames, nullptr);
	}

	alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256] = {};
	auto* symbol = (SYMBOL_INFO*)buffer;
	for (USHORT i = 0; i < count; i++) {
		DWORD64 address = (DWORD64)frames[i];
		symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
		symbol->MaxNameLen = 255;
		char module[MAX_PATH] = "?";
		HMODULE mod = nullptr;
		if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		        (LPCSTR)address, &mod)) {
			GetModuleFileNameA(mod, module, MAX_PATH);
		}
		const char* base = std::strrchr(module, '\\');
		base = base ? base + 1 : module;
		DWORD64 displacement = 0;
		IMAGEHLP_LINE64 line = { sizeof(IMAGEHLP_LINE64) };
		DWORD lineDisplacement = 0;
		bool haveSymbol = SymFromAddr(process, address, &displacement, symbol);
		bool haveLine = SymGetLineFromAddr64(process, address, &lineDisplacement, &line);
		std::fprintf(f, "  %2u %s!%s+0x%llx", i, base, haveSymbol ? symbol->Name : "?", (unsigned long long)displacement);
		if (haveLine)
			std::fprintf(f, " (%s:%lu)", line.FileName, line.LineNumber);
		std::fprintf(f, "\n");
	}
}

void Report(const char* why, CONTEXT* context)
{
	std::call_once(written, [&] {
		FILE* f = std::fopen(reportPath.c_str(), "w");
		if (!f)
			return;
		std::fprintf(f, "%s (thread %lu)\n", why, GetCurrentThreadId());
		WriteStack(f, context);
		std::fclose(f);
	});
}

LONG WINAPI OnException(EXCEPTION_POINTERS* info)
{
	// Vectored: sees first-chance exceptions too, so only the ones that end a program. A report for one that
	// something handles is harmless: run.py only reads it when the child ended abnormally.
	switch (info->ExceptionRecord->ExceptionCode) {
	case EXCEPTION_ACCESS_VIOLATION:
	case EXCEPTION_ILLEGAL_INSTRUCTION:
	case EXCEPTION_INT_DIVIDE_BY_ZERO:
	case EXCEPTION_STACK_OVERFLOW:
	case EXCEPTION_PRIV_INSTRUCTION:
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
	case 0xC0000409: // STATUS_STACK_BUFFER_OVERRUN, raised by some fail-fast paths
		break;
	default:
		return EXCEPTION_CONTINUE_SEARCH;
	}
	char why[160];
	std::snprintf(why, sizeof(why), "exception 0x%08lx at %p", info->ExceptionRecord->ExceptionCode,
	    info->ExceptionRecord->ExceptionAddress);
	Report(why, info->ContextRecord);
	return EXCEPTION_CONTINUE_SEARCH;
}

void OnAbort(int)
{
	Report("abort()", nullptr);
	TerminateProcess(GetCurrentProcess(), 3);
}

void OnTerminate()
{
	std::string why = "std::terminate";
	if (std::exception_ptr e = std::current_exception()) {
		try {
			std::rethrow_exception(e);
		} catch (const std::exception& ex) {
			why += std::string(": uncaught ") + typeid(ex).name() + ": " + ex.what();
		} catch (...) {
			why += ": uncaught non-std exception";
		}
	}
	Report(why.c_str(), nullptr);
	TerminateProcess(GetCurrentProcess(), 0xC0000409);
}

void OnInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t)
{
	Report("CRT invalid parameter (a _s function, a bad handle or the like)", nullptr);
	TerminateProcess(GetCurrentProcess(), 0xC0000409);
}

} // namespace

void conformance::InstallCrashReport(const std::string& outPath)
{
	reportPath = outPath + ".crash.txt";
	AddVectoredExceptionHandler(1, OnException);
	std::signal(SIGABRT, OnAbort);
	_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
	std::set_terminate(OnTerminate);
	_set_invalid_parameter_handler(OnInvalidParameter);
}

#else

void conformance::InstallCrashReport(const std::string&) {}

#endif
