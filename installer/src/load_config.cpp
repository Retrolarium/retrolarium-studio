// The PE loader reads this before main(), when imported DLLs are resolved.
// GNU ld emits the load-config directory for the _load_config_used symbol.
// Only the standalone static x64 MinGW installer supplies it; shared host
// Qt builds need their Qt DLLs and retain the CRT's own configuration.
#include <cstddef>
#include <cstdint>

namespace {
struct LoadConfig64 {
    std::uint32_t size;
    std::uint32_t timeDateStamp;
    std::uint16_t majorVersion;
    std::uint16_t minorVersion;
    std::uint32_t globalFlagsClear;
    std::uint32_t globalFlagsSet;
    std::uint32_t criticalSectionDefaultTimeout;
    std::uint64_t deCommitFreeBlockThreshold;
    std::uint64_t deCommitTotalFreeThreshold;
    std::uint64_t lockPrefixTable;
    std::uint64_t maximumAllocationSize;
    std::uint64_t virtualMemoryThreshold;
    std::uint64_t processAffinityMask;
    std::uint32_t processHeapFlags;
    std::uint16_t csdVersion;
    std::uint16_t dependentLoadFlags;
    std::uint64_t editList;
    std::uint64_t securityCookie;
    std::uint64_t seHandlerTable;
    std::uint64_t seHandlerCount;
    // Keep the directory through CodeIntegrity. A 112-byte legacy prefix
    // carries the flag at offset 78 but Windows can ignore that flag: the
    // live DLL-shadowing regression checks the emitted image, not just bytes.
    std::uint64_t guardCfCheckFunctionPointer;
    std::uint64_t guardCfDispatchFunctionPointer;
    std::uint64_t guardCfFunctionTable;
    std::uint64_t guardCfFunctionCount;
    std::uint32_t guardFlags;
    std::uint16_t codeIntegrityFlags;
    std::uint16_t codeIntegrityCatalog;
    std::uint32_t codeIntegrityCatalogOffset;
    std::uint32_t codeIntegrityReserved;
};
static_assert(sizeof(void*) == 8);
static_assert(sizeof(LoadConfig64) == 160);
static_assert(offsetof(LoadConfig64, dependentLoadFlags) == 78);

constexpr LoadConfig64 config() {
    LoadConfig64 value{};
    value.size = sizeof(value);
    value.dependentLoadFlags = 0x800;  // LOAD_LIBRARY_SEARCH_SYSTEM32
    return value;
}
}  // namespace

extern "C" {
extern const LoadConfig64 _load_config_used = config();
}
