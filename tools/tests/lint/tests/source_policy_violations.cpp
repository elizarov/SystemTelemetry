#include <filesystem>
#include <thread>

void SourcePolicyViolations() {
    std::function<void()> callback;
    std::hash<int> hash_value;
    #ifdef CASEDASH_LINT_FIXTURE
    MessageBoxW(nullptr, nullptr, nullptr, 0);
    MessageBoxUtf8(nullptr, nullptr, nullptr, 0);
    auto wide = L"wide";
}
