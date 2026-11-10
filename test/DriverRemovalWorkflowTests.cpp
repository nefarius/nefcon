#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/DriverRemovalWorkflow.hpp"
#include <cstdio>
#include <initializer_list>
namespace
{
int failures = 0;
#define CHECK(expression) do { if (!(expression)) { std::fprintf(stderr, "FAILED line %d: %s\n", __LINE__, #expression); ++failures; } } while (false)
void CompletedStagesPreserveRebootEvidence()
{
    for (bool restart : {false, true})
        for (bool purge : {false, true})
        {
            int stage = 0;
            const auto exitCode = DriverRemovalWorkflow::Run(restart,
                [&](bool& pending) { CHECK(stage++ == 0); CHECK(!pending); pending = false; return ERROR_SUCCESS; },
                [&](bool& reboot) { CHECK(stage++ == 1); CHECK(!reboot); reboot = purge; return ERROR_SUCCESS; });
            CHECK(stage == 2);
            CHECK(exitCode == static_cast<DWORD>(restart || purge ? ERROR_SUCCESS_REBOOT_REQUIRED : ERROR_SUCCESS));
        }
}
void PendingRemovalDefersPurge()
{
    bool purged = false;
    const auto exitCode = DriverRemovalWorkflow::Run(false,
        [](bool& pending) { pending = true; return ERROR_SUCCESS; },
        [&](bool&) { purged = true; return ERROR_SUCCESS; });
    CHECK(exitCode == ERROR_SUCCESS_REBOOT_REQUIRED);
    CHECK(!purged);
}
void RemovalFailureNeverPurgesOrClaimsSuccess()
{
    bool purged = false;
    const auto exitCode = DriverRemovalWorkflow::Run(true,
        [](bool& pending) { pending = true; return ERROR_ACCESS_DENIED; },
        [&](bool&) { purged = true; return ERROR_SUCCESS; });
    CHECK(exitCode == ERROR_ACCESS_DENIED);
    CHECK(!purged);
}
void PurgeFailureWinsOverRebootSuccess()
{
    const auto exitCode = DriverRemovalWorkflow::Run(true,
        [](bool&) { return ERROR_SUCCESS; },
        [](bool& reboot) { reboot = true; return ERROR_ACCESS_DENIED; });
    CHECK(exitCode == ERROR_ACCESS_DENIED);
}
void SubsequentPassCanFinishDeferredPurge()
{
    bool purged = false;
    const auto exitCode = DriverRemovalWorkflow::Run(false,
        [](bool& pending) { pending = false; return ERROR_SUCCESS; },
        [&](bool&) { purged = true; return ERROR_SUCCESS; });
    CHECK(exitCode == ERROR_SUCCESS && purged);
}
}
int main()
{
    CompletedStagesPreserveRebootEvidence();
    PendingRemovalDefersPurge();
    RemovalFailureNeverPurgesOrClaimsSuccess();
    PurgeFailureWinsOverRebootSuccess();
    SubsequentPassCanFinishDeferredPurge();
    std::printf("Driver removal workflow: %d failure(s).\n", failures);
    return failures == 0 ? 0 : 1;
}
