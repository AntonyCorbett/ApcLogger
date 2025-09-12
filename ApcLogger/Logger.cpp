#include "pch.h"
#include "Logger.h"

#include <windows.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <cstdarg>

namespace
{
    std::mutex TheMutex;
    std::filesystem::path LogPath;
    std::ofstream OutputStream;
    Logger::Level MinLevel =
#if defined(_DEBUG)
        Logger::Level::Debug;
#else
        Logger::Level::Info;
#endif

    constexpr size_t MaxBytesBeforeRotate = 512 * 1024; // 512 KB  // NOLINT(bugprone-implicit-widening-of-multiplication-result)

    const wchar_t* LevelToTag(const Logger::Level lvl)
    {
        switch (lvl)
        {
        case Logger::Level::Trace: return L"TRC";
        case Logger::Level::Debug: return L"DBG";
        case Logger::Level::Info:  return L"INF";
        case Logger::Level::Warn:  return L"WRN";
        case Logger::Level::Error: return L"ERR";
        default: return L"UNK";  // NOLINT(clang-diagnostic-covered-switch-default)
        }
    }

    std::wstring FormatTimestamp()
    {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        wchar_t buf[64]{};

    	// yyyy-mm-dd hh:mm:ss.mmm
        (void)swprintf_s(buf, L"%04u-%02u-%02u %02u:%02u:%02u.%03u",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        return std::wstring{ buf };
    }

    std::wstring VFormat(const wchar_t* fmt, va_list args)
    {
        va_list argsCopy;
        va_copy(argsCopy, args);
        const int len = _vscwprintf(fmt, argsCopy);
        va_end(argsCopy);
        if (len <= 0)
        {
            return L"";
        }

        std::wstring out;
        out.resize(static_cast<size_t>(len) + 1); // room for null
        _vsnwprintf_s(out.data(), out.size(), _TRUNCATE, fmt, args);
        out.resize(wcslen(out.c_str())); // shrink to actual length
        return out;
    }
   
    std::string ToUtf8(const std::wstring& w)
    {
        if (w.empty())
        {
            return std::string{};
        }

        const int needed = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
        std::string s(static_cast<size_t>(needed), '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), needed, nullptr, nullptr);
        return s;
    }

    void EnsureOpen()
    {
        if (OutputStream.is_open()) return;

        // Write UTF-8 BOM on new file
        const bool newFile = !std::filesystem::exists(LogPath);
        OutputStream.open(LogPath, std::ios::out | std::ios::app | std::ios::binary);
        if (newFile && OutputStream.is_open())
        {
	        constexpr unsigned char bom[3]{ 0xEF, 0xBB, 0xBF };
            OutputStream.write(reinterpret_cast<const char*>(bom), 3);
        }
    }

    void TryRotateIfNeeded()
    {
        std::error_code errorCode;
        const auto size = std::filesystem::file_size(LogPath, errorCode);
        if (errorCode || size < MaxBytesBeforeRotate) return;

        if (OutputStream.is_open())
        {
            OutputStream.flush();
            OutputStream.close();
        }

        std::filesystem::path rotated1 = LogPath;
        rotated1 += L".1";
        std::filesystem::remove(rotated1, errorCode);
        std::filesystem::rename(LogPath, rotated1, errorCode);

        EnsureOpen();
    }

    void WriteLine(const std::wstring& line)
    {
        // Mirror to debugger
        OutputDebugStringW(line.c_str());
        OutputDebugStringW(L"\r\n");

        // Write to file (UTF-8)
        EnsureOpen();
        if (!OutputStream.is_open())
        {
            return;
        }

        TryRotateIfNeeded();

        const auto utf8 = ToUtf8(line + L"\r\n");
        OutputStream.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
        OutputStream.flush();
    }
}

namespace Logger
{
    void Init(const std::wstring& appName)
    {
        const std::filesystem::path base = std::filesystem::temp_directory_path();
        const auto dir = base / appName;

        {
            std::lock_guard<std::mutex> lock(TheMutex);

            std::error_code errorCode;
            std::filesystem::create_directories(dir, errorCode);

            LogPath = dir / (appName + L".log");
            EnsureOpen();
        }

        // Log AFTER releasing the mutex to avoid re-entrancy
        Log(Level::Info, L"Logger initialized. File: %ls", LogPath.c_str());
    }

    void SetMinLevel(const Level level)
    {
        std::lock_guard<std::mutex> lock(TheMutex);
        MinLevel = level;
    }

    void Log(const Level level, const wchar_t* fmt, ...)
    {
        if (level < MinLevel)
        {
            return;
        }

        va_list args;
        va_start(args, fmt);
        const std::wstring msg = VFormat(fmt, args);
        va_end(args);

        const std::wstring line = FormatTimestamp() + L" [" + LevelToTag(level) + L"] " + msg;
        std::lock_guard<std::mutex> lock(TheMutex);
        WriteLine(line);
    }

    void LogLastError(const Level level, const wchar_t* context, unsigned long errorCode)
    {
        if (errorCode == 0)
        {
            errorCode = GetLastError();
        }

        LPWSTR buf = nullptr;
        constexpr DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
        constexpr DWORD langId = MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT);
        const DWORD chars = FormatMessageW(flags, nullptr, errorCode, langId, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);

        if (chars == 0 || buf == nullptr)
        {
            Log(level, L"%ls failed with error %lu", context, errorCode);
            return;
        }

        std::wstring msg(buf, buf + wcslen(buf));
        LocalFree(buf);

        // Trim trailing newlines that FormatMessage adds
        while (!msg.empty() && (msg.back() == L'\r' || msg.back() == L'\n'))
        {
            msg.pop_back();
        }

        Log(level, L"%ls failed with error %lu: %ls", context, errorCode, msg.c_str());
    }

    void Shutdown()
    {
        std::lock_guard<std::mutex> lock(TheMutex);
        if (OutputStream.is_open())
        {
            OutputStream.flush();
            OutputStream.close();
        }
    }
}
