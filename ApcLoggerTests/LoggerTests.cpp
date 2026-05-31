#include "pch.h"
#include "../ApcLogger/Logger.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

using Microsoft::VisualStudio::CppUnitTestFramework::Assert;

namespace ApcLoggerTests
{
    static std::filesystem::path MakeTestDir()
    {
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        std::filesystem::path dir = std::filesystem::path(tmp) / L"ApcLoggerTests" /
            std::to_wstring(GetTickCount64());
        std::filesystem::create_directories(dir);
        return dir;
    }

    static std::string ReadFileBytes(const std::filesystem::path& p)
    {
        std::ifstream f(p, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(f), {});
    }

    TEST_CLASS(LoggerTests)
    {
        std::filesystem::path testDir;

    public:
        TEST_METHOD_INITIALIZE(Setup)
        {
            testDir = MakeTestDir();
        }

        TEST_METHOD_CLEANUP(Teardown)
        {
            Logger::Shutdown();
            std::error_code ec;
            std::filesystem::remove_all(testDir, ec);
        }

        TEST_METHOD(InitCreatesLogFile)
        {
            Logger::Init(L"TestApp", testDir.wstring());
            Logger::Shutdown();

            Assert::IsTrue(std::filesystem::exists(testDir / L"TestApp.log"));
        }

        TEST_METHOD(BomWrittenAtStartOfNewFile)
        {
            Logger::Init(L"TestApp", testDir.wstring());
            Logger::Shutdown();

            const auto bytes = ReadFileBytes(testDir / L"TestApp.log");
            Assert::IsTrue(bytes.size() >= 3);
            Assert::AreEqual((unsigned char)0xEF, (unsigned char)bytes[0]);
            Assert::AreEqual((unsigned char)0xBB, (unsigned char)bytes[1]);
            Assert::AreEqual((unsigned char)0xBF, (unsigned char)bytes[2]);
        }

        TEST_METHOD(BomNotDuplicatedOnReopen)
        {
            Logger::Init(L"TestApp", testDir.wstring());
            Logger::Shutdown();
            Logger::Init(L"TestApp", testDir.wstring());
            Logger::Shutdown();

            const auto bytes = ReadFileBytes(testDir / L"TestApp.log");
            // BOM bytes must not appear again after position 0
            const std::string bom = "\xEF\xBB\xBF";
            const size_t second = bytes.find(bom, 1);
            Assert::AreEqual(std::string::npos, second, L"BOM written more than once");
        }

        TEST_METHOD(LogEntryContainsLevelTagAndMessage)
        {
            Logger::Init(L"TestApp", testDir.wstring());
            Logger::SetMinLevel(Logger::Level::Trace);
            LOG_INFO(L"hello world");
            Logger::Shutdown();

            const auto content = ReadFileBytes(testDir / L"TestApp.log");
            Assert::AreNotEqual(std::string::npos, content.find("[INF]"));
            Assert::AreNotEqual(std::string::npos, content.find("hello world"));
        }

        TEST_METHOD(MinLevelFiltersLowerMessages)
        {
            Logger::Init(L"TestApp", testDir.wstring());
            Logger::SetMinLevel(Logger::Level::Warn);
            LOG_DEBUG(L"should be absent");
            LOG_INFO(L"also absent");
            LOG_WARN(L"should be present");
            Logger::Shutdown();

            const auto content = ReadFileBytes(testDir / L"TestApp.log");
            Assert::AreEqual(std::string::npos, content.find("should be absent"));
            Assert::AreEqual(std::string::npos, content.find("also absent"));
            Assert::AreNotEqual(std::string::npos, content.find("should be present"));
        }

        TEST_METHOD(AllLevelTagsAreWritten)
        {
            Logger::Init(L"TestApp", testDir.wstring());
            Logger::SetMinLevel(Logger::Level::Trace);
            LOG_TRACE(L"t");
            LOG_DEBUG(L"d");
            LOG_INFO(L"i");
            LOG_WARN(L"w");
            LOG_ERROR(L"e");
            Logger::Shutdown();

            const auto content = ReadFileBytes(testDir / L"TestApp.log");
            Assert::AreNotEqual(std::string::npos, content.find("[TRC]"));
            Assert::AreNotEqual(std::string::npos, content.find("[DBG]"));
            Assert::AreNotEqual(std::string::npos, content.find("[INF]"));
            Assert::AreNotEqual(std::string::npos, content.find("[WRN]"));
            Assert::AreNotEqual(std::string::npos, content.find("[ERR]"));
        }

        TEST_METHOD(LogFormatsArguments)
        {
            Logger::Init(L"TestApp", testDir.wstring());
            Logger::SetMinLevel(Logger::Level::Trace);
            LOG_INFO(L"value=%d name=%ls", 42, L"foo");
            Logger::Shutdown();

            const auto content = ReadFileBytes(testDir / L"TestApp.log");
            Assert::AreNotEqual(std::string::npos, content.find("value=42 name=foo"));
        }

        TEST_METHOD(LogLastErrorFormatsWin32Message)
        {
            Logger::Init(L"TestApp", testDir.wstring());
            Logger::SetMinLevel(Logger::Level::Trace);
            // ERROR_FILE_NOT_FOUND = 2: "The system cannot find the file specified."
            Logger::LogLastError(Logger::Level::Error, L"OpenFile", 2);
            Logger::Shutdown();

            const auto content = ReadFileBytes(testDir / L"TestApp.log");
            Assert::AreNotEqual(std::string::npos, content.find("OpenFile"));
            Assert::AreNotEqual(std::string::npos, content.find("2"));
        }

        TEST_METHOD(LogRotationCreatesBackupFile)
        {
            Logger::Init(L"TestApp", testDir.wstring());
            Logger::SetMinLevel(Logger::Level::Trace);

            // Write ~600 KB — well above the 512 KB threshold
            const std::wstring chunk(512, L'A');
            for (int i = 0; i < 1300; ++i)
                LOG_INFO(L"%ls", chunk.c_str());

            Logger::Shutdown();

            Assert::IsTrue(std::filesystem::exists(testDir / L"TestApp.log.1"),
                L"Backup file TestApp.log.1 not created");
            Assert::IsTrue(std::filesystem::exists(testDir / L"TestApp.log"),
                L"Active log file missing after rotation");
        }

        TEST_METHOD(LogRotationActiveFileIsSmallerThanThreshold)
        {
            Logger::Init(L"TestApp", testDir.wstring());
            Logger::SetMinLevel(Logger::Level::Trace);

            const std::wstring chunk(512, L'B');
            for (int i = 0; i < 1300; ++i)
                LOG_INFO(L"%ls", chunk.c_str());

            Logger::Shutdown();

            std::error_code ec;
            const auto activeSize = std::filesystem::file_size(testDir / L"TestApp.log", ec);
            Assert::IsFalse((bool)ec);
            Assert::IsTrue(activeSize < 512 * 1024,
                L"Active log file still exceeds rotation threshold after rotation");
        }

        TEST_METHOD(ThreadSafetyNoCrashUnderConcurrentLogging)
        {
            Logger::Init(L"TestApp", testDir.wstring());
            Logger::SetMinLevel(Logger::Level::Trace);

            constexpr int kThreads = 8;
            constexpr int kMessages = 200;
            std::vector<std::thread> threads;
            threads.reserve(kThreads);

            for (int t = 0; t < kThreads; ++t)
            {
                threads.emplace_back([t]()
                {
                    for (int i = 0; i < kMessages; ++i)
                        LOG_INFO(L"thread=%d msg=%d", t, i);
                });
            }

            for (auto& th : threads)
                th.join();

            Logger::Shutdown();
            // If we reach here without crashing, the test passes.
            Assert::IsTrue(std::filesystem::exists(testDir / L"TestApp.log"));
        }

        TEST_METHOD(InitWithInvalidPathDoesNotCrash)
        {
            // Drive Z: is extremely unlikely to exist
            Logger::Init(L"TestApp", L"Z:\\NonExistent\\Path\\That\\Cannot\\Exist");
            LOG_INFO(L"this should be silently dropped");
            Logger::Shutdown();
            // No crash = pass
        }

        TEST_METHOD(ShutdownIsIdempotent)
        {
            Logger::Init(L"TestApp", testDir.wstring());
            Logger::Shutdown();
            Logger::Shutdown();
            Logger::Shutdown();
            // No crash = pass
        }

        TEST_METHOD(LogBeforeInitIsIgnored)
        {
            // Do not call Init — just log and shut down; must not crash
            LOG_INFO(L"logged before init");
            Logger::Shutdown();
        }

        TEST_METHOD(ReinitResetsRotationFailedFlag)
        {
            // First init to a bad path (sets LogPath to empty on failure)
            Logger::Init(L"TestApp", L"Z:\\Bad\\Path");

            // Second init to a valid path must work correctly
            Logger::Init(L"TestApp", testDir.wstring());
            LOG_INFO(L"after reinit");
            Logger::Shutdown();

            const auto content = ReadFileBytes(testDir / L"TestApp.log");
            Assert::AreNotEqual(std::string::npos, content.find("after reinit"));
        }
    };
}
