// The comparison harness (loader/compare_linux.cpp) without the game: hand-assembled
// "original" functions in an executable block standing in for the title's code, and
// "decompiled" versions (compare_test_code.cpp) that are right, wrong, hang, crash,
// pop the wrong number of bytes or call host code.

#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "compare.h"
#include "functions.h"
#include "hooks.h"
#include "test_support.h"

extern "C" {
extern void (*compare_test_host_function)();
// Good, off by one, no pop, host call, hang, crash, square (compare_test_code.cpp).
void compare_test_code(const void** out);
}

namespace {

using Add = uint32_t(XB_THISCALL*)(uint32_t*, uint32_t);
using Square = float(XB_CDECL*)(float);

// mov eax,[ecx]; add eax,[esp+4]; mov [ecx+4],eax; ret 4
constexpr uint8_t kAdd[] = {0x8B, 0x01, 0x03, 0x44, 0x24, 0x04, 0x89, 0x41, 0x04, 0xC2, 0x04, 0x00};
// kAdd with a conditional branch inside the bytes the hook overwrites:
// mov eax,[ecx]; test eax,eax; jne +0; add eax,[esp+4]; mov [ecx+4],eax; ret 4
constexpr uint8_t kAddBranch[] = {0x8B, 0x01, 0x85, 0xC0, 0x75, 0x00, 0x03, 0x44,
                                  0x24, 0x04, 0x89, 0x41, 0x04, 0xC2, 0x04, 0x00};
// fld dword [esp+4]; fmul dword [esp+4]; ret
constexpr uint8_t kSquare[] = {0xD9, 0x44, 0x24, 0x04, 0xD8, 0x4C, 0x24, 0x04, 0xC3};

void host_function() { std::printf("host code ran in a test process\n"); }

}  // namespace

int main() {
    using game::Result;
    using game::Status;
    compare_test_host_function = host_function;

    // The "title": one original per slot, 32 bytes apart.
    uint8_t* image = loader::alloc_code(4096);
    CHECK(image != nullptr);
    if (!image) return 1;
    const auto slot = [&](int i) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(image + 32 * i)); };
    for (int i = 0; i < 6; ++i) std::memcpy(image + 32 * i, kAdd, sizeof(kAdd));
    std::memcpy(image + 32 * 6, kSquare, sizeof(kSquare));
    std::memcpy(image + 32 * 7, kAddBranch, sizeof(kAddBranch));
    std::memcpy(image + 32 * 8, kAdd, sizeof(kAdd));

    const void* code[7];
    compare_test_code(code);
    static const game::Function functions[] = {
        {"good", slot(0), 0, code[0], 4, Result::Eax, Status::Compare},
        {"off_by_one", slot(1), 0, code[1], 4, Result::Eax, Status::Compare},
        {"no_pop", slot(2), 0, code[2], 4, Result::Eax, Status::Compare},
        {"host_call", slot(3), 0, code[3], 4, Result::Eax, Status::Compare},
        {"hang", slot(4), 0, code[4], 4, Result::Eax, Status::Compare},
        {"crash", slot(5), 0, code[5], 4, Result::Eax, Status::Compare},
        {"square", slot(6), 0, code[6], 4, Result::St0, Status::Compare},
        {"branch_at_entry", slot(7), 0, code[0], 4, Result::Eax, Status::Compare},
        {"settled", slot(8), 0, code[1], 4, Result::Eax, Status::Compare},  // off by one, not tested
    };
    const std::filesystem::path results =
        std::filesystem::temp_directory_path() / ("compare_test_" + std::to_string(getpid()) + ".csv");
    loader::compare::Config config;
    config.enabled = true;
    config.image_begin = slot(0);
    config.image_end = slot(0) + 4096;
    config.libraries = {"libcompare_test_code", "libc.so", "libm.so", "ld-linux"};
    config.results = results;
    config.timeout_ms = 500;
    // Earlier runs matched "settled" often enough: it is not even hooked.
    config.enough_matches = 2;
    {
        std::ofstream out(results);
        char line[96];
        std::snprintf(line, sizeof(line), "%08x,settled,compare,9,2,2,0,0,0,\"\"\n", slot(8));
        out << "address,name,status,calls,tested,matched,drift,mismatched,inconclusive,last_issue\n" << line;
    }
    loader::compare::install(functions, config);

    // The originals keep running for the caller, whatever the test found. From a
    // thread of its own, as title code runs: what the C runtime keeps per thread
    // (its descriptor, which records the thread's id) is not the function's doing.
    std::thread([&] {
    for (int i = 0; i < 6; ++i) {
        // Data in the heap: the stack below the caller is skipped, the rest compared.
        auto* data = new uint32_t[2]{37, 0};
        const uint32_t sum = reinterpret_cast<Add>(uintptr_t{slot(i)})(data, 5);
        CHECK(sum == 42 && data[1] == 42, "%s: %u, %u", functions[i].name, sum, data[1]);
        delete[] data;
    }
    auto* data = new uint32_t[2]{37, 0};
    const uint32_t sum = reinterpret_cast<Add>(uintptr_t{slot(7)})(data, 5);  // through the moved jne
    CHECK(sum == 42 && data[1] == 42, "branch_at_entry: %u, %u", sum, data[1]);
    delete[] data;
    data = new uint32_t[2]{37, 0};
    const uint32_t settled = reinterpret_cast<Add>(uintptr_t{slot(8)})(data, 5);
    CHECK(settled == 42 && data[1] == 42, "settled: %u, %u", settled, data[1]);
    delete[] data;
    const float square = reinterpret_cast<Square>(uintptr_t{slot(6)})(1.5f);
    CHECK(square == 2.25f, "%f", static_cast<double>(square));
    }).join();

    const auto expect = [](const char* name, uint64_t matched, uint64_t mismatched, uint64_t inconclusive) {
        const loader::compare::Counts c = loader::compare::counts(name);
        CHECK(c.calls == 1 && c.tested == 1, "%s: %llu calls, %llu tested", name,
              static_cast<unsigned long long>(c.calls), static_cast<unsigned long long>(c.tested));
        CHECK(c.matched + c.drift == matched && c.mismatched == mismatched && c.inconclusive == inconclusive,
              "%s: matched %llu drift %llu mismatched %llu inconclusive %llu", name,
              static_cast<unsigned long long>(c.matched), static_cast<unsigned long long>(c.drift),
              static_cast<unsigned long long>(c.mismatched), static_cast<unsigned long long>(c.inconclusive));
    };
    expect("good", 1, 0, 0);
    expect("off_by_one", 0, 1, 0);
    expect("no_pop", 0, 1, 0);
    expect("host_call", 0, 0, 1);
    expect("hang", 0, 0, 1);
    expect("crash", 0, 1, 0);
    expect("square", 1, 0, 0);
    expect("branch_at_entry", 1, 0, 0);
    CHECK(loader::compare::counts("settled").calls == 0, "settled: hooked");

    // A test still running when the game is killed (Ctrl+C, a crash) leaves no test
    // processes behind: they would keep the game's window and sockets open.
    prctl(PR_SET_CHILD_SUBREAPER, 1);  // orphaned test processes come back to us
    const pid_t game = fork();
    if (game == 0) {
        uint32_t data[2] = {37, 0};
        reinterpret_cast<Add>(uintptr_t{slot(4)})(data, 5);  // "hang": tested, runs 500 ms
        _exit(0);
    }
    usleep(200 * 1000);
    kill(game, SIGKILL);
    waitpid(game, nullptr, 0);
    bool left = true;
    for (int i = 0; i < 40 && left; ++i) {
        while (waitpid(-1, nullptr, WNOHANG) > 0) {
        }
        left = waitpid(-1, nullptr, WNOHANG) == 0;  // -1 (ECHILD): none left
        if (left) usleep(50 * 1000);
    }
    CHECK(!left, "test processes outlived the killed game");
    // End our leftover children (subreaper), nothing else; a grandchild only becomes
    // ours when its parent is gone, hence the rounds.
    for (int round = 0; left && round < 20; ++round) {
        for (const auto& entry : std::filesystem::directory_iterator("/proc")) {
            std::ifstream stat(entry.path() / "stat");
            std::string pid, comm, state;
            pid_t ppid = 0;
            if (stat >> pid >> comm >> state >> ppid && ppid == getpid()) kill(std::stoi(pid), SIGKILL);
        }
        usleep(50 * 1000);
        while (waitpid(-1, nullptr, WNOHANG) > 0) {
        }
        left = waitpid(-1, nullptr, WNOHANG) == 0;
    }

    // Counts add up across flushes (and runs). "good" settles at its second match:
    // the third call is not tested.
    loader::compare::flush();
    for (int i = 0; i < 2; ++i) {
        uint32_t data[2] = {1, 0};
        reinterpret_cast<Add>(uintptr_t{slot(0)})(data, 2);
    }
    loader::compare::flush();
    std::string row;
    {
        std::ifstream in(results);
        for (std::string line; std::getline(in, line);)
            if (line.find(",good,") != std::string::npos) row = line;
    }
    CHECK(row.find(",good,compare,3,2,2,0,0,0,") != std::string::npos, "row: %s", row.c_str());
    std::filesystem::remove(results);

    if (test::g_failures) return 1;
    std::printf("compare_test: ok\n");
    return 0;
}
