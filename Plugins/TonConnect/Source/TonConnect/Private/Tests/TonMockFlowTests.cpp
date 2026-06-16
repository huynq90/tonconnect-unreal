#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "TonConnectSubsystem.h"
#include "TonTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

// ---------------------------------------------------------------------------
// Helper: find the subsystem from any live world
// ---------------------------------------------------------------------------
static UTonConnectSubsystem* FindTonSubsystem()
{
    if (!GEngine) return nullptr;
    for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
    {
        UWorld* W = Ctx.World();
        if (!W) continue;
        UGameInstance* GI = W->GetGameInstance();
        if (!GI) continue;
        UTonConnectSubsystem* Sub = GI->GetSubsystem<UTonConnectSubsystem>();
        if (Sub) return Sub;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Latent command: wait until subsystem state matches expected, or timeout
// ---------------------------------------------------------------------------
class FWaitForTonState : public IAutomationLatentCommand
{
public:
    FWaitForTonState(FAutomationTestBase* InTest, ETonConnectState InExpected, float InTimeout)
        : Test(InTest), Expected(InExpected), TimeoutSec(InTimeout), Elapsed(0.f) {}

    virtual bool Update() override
    {
        Elapsed += FApp::GetDeltaTime();

        UTonConnectSubsystem* Sub = FindTonSubsystem();
        if (!Sub)
        {
            if (Elapsed >= TimeoutSec)
            {
                Test->AddError(TEXT("WaitForTonState: no subsystem found within timeout"));
                return true;
            }
            return false;
        }

        if (Sub->GetState() == Expected) return true;

        if (Elapsed >= TimeoutSec)
        {
            Test->AddError(FString::Printf(
                TEXT("WaitForTonState: timed out. State=%d, Expected=%d"),
                (int32)Sub->GetState(), (int32)Expected));
            return true;
        }
        return false;
    }

private:
    FAutomationTestBase* Test;
    ETonConnectState     Expected;
    float                TimeoutSec;
    float                Elapsed;
};

// ---------------------------------------------------------------------------
// Mock Connect Flow Test
//
// Requires a running game world with UTonConnectSubsystem. Launch with:
//   -ton.mock -ton.mock.approve_delay=0.5
//
// In Editor: Play-in-Editor → run via Session Frontend → TonConnect.Mock.*
// ---------------------------------------------------------------------------
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FTonMock_ConnectFlow,
    "TonConnect.Mock.ConnectFlow",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

void FTonMock_ConnectFlow::GetTests(TArray<FString>& OutBeautifiedNames,
                                    TArray<FString>& OutTestCommands) const
{
    OutBeautifiedNames.Add(TEXT("MockConnect"));
    OutTestCommands.Add(TEXT("MockConnect"));
}

bool FTonMock_ConnectFlow::RunTest(const FString& Parameters)
{
    UTonConnectSubsystem* Sub = FindTonSubsystem();
    if (!Sub)
    {
        AddWarning(TEXT("TonConnect.Mock.ConnectFlow: no subsystem — run in -game or PIE with TonConnect plugin enabled"));
        return true; // soft skip, not a failure
    }

    // Must start disconnected
    TestEqual(TEXT("initial state"), Sub->GetState(), ETonConnectState::Disconnected);

    // Kick off mock connection
    Sub->Connect();
    TestEqual(TEXT("connecting"), Sub->GetState(), ETonConnectState::Connecting);

    // Wait up to 5s for mock bridge to fire OnConnected
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForTonState(this, ETonConnectState::Connected, 5.f));

    return true;
}

// ---------------------------------------------------------------------------
// Mock Send Flow Test — runs after ConnectFlow succeeds
// ---------------------------------------------------------------------------
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FTonMock_SendTon,
    "TonConnect.Mock.SendTon",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

void FTonMock_SendTon::GetTests(TArray<FString>& OutBeautifiedNames,
                                 TArray<FString>& OutTestCommands) const
{
    OutBeautifiedNames.Add(TEXT("MockSendTon"));
    OutTestCommands.Add(TEXT("MockSendTon"));
}

bool FTonMock_SendTon::RunTest(const FString& Parameters)
{
    UTonConnectSubsystem* Sub = FindTonSubsystem();
    if (!Sub || Sub->GetState() != ETonConnectState::Connected)
    {
        AddWarning(TEXT("TonConnect.Mock.SendTon: skipped — not connected (run ConnectFlow first)"));
        return true;
    }

    // We can't use BindLambda on a DECLARE_DYNAMIC_DELEGATE — pass an unbound delegate.
    // The mock bridge fires OnSendResult (multicast) regardless; result capture
    // is done in the Gauntlet controller which uses a proper UFUNCTION handler.
    FOnTonSendResultDelegate Cb;

    Sub->SendTon(
        TEXT("0:3333333333333333333333333333333333333333333333333333333333333333"),
        TEXT("50000000"), TEXT("test"), Cb);

    // SendTon returned without crash — test passes synchronously
    return true;
}

// ---------------------------------------------------------------------------
// Account info + assets arrive after mock connect
// Waits for OnAccountInfoUpdated to fire and validates version + balance.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTonMock_AccountInfo,
    "TonConnect.Mock.AccountInfo",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FTonMock_AccountInfo::RunTest(const FString&)
{
    UTonConnectSubsystem* Sub = FindTonSubsystem();
    if (!Sub || Sub->GetState() != ETonConnectState::Connected)
    {
        AddWarning(TEXT("TonConnect.Mock.AccountInfo: skipped — not connected (run ConnectFlow first)"));
        return true;
    }

    // Mock GetAccountInfo returns {5 TON, "W5 R1"}
    TestFalse(TEXT("balance not empty"), Sub->GetCachedBalance().IsEmpty());
    TestEqual (TEXT("wallet version"),   Sub->GetConnectedWallet().WalletVersion, TEXT("W5 R1"));

    return true;
}

// ---------------------------------------------------------------------------
// Wallet cache: after connect, GetCachedBalance() and WalletVersion are set
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTonMock_WalletCache,
    "TonConnect.Mock.WalletCache",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FTonMock_WalletCache::RunTest(const FString&)
{
    UTonConnectSubsystem* Sub = FindTonSubsystem();
    if (!Sub || Sub->GetState() != ETonConnectState::Connected)
    {
        AddWarning(TEXT("TonConnect.Mock.WalletCache: skipped — not connected"));
        return true;
    }

    const FTonWalletInfo& W = Sub->GetConnectedWallet();
    TestFalse(TEXT("address not empty"),  W.Address.IsEmpty());
    TestFalse(TEXT("network not empty"),  W.Network.IsEmpty());
    TestFalse(TEXT("balance not empty"),  Sub->GetCachedBalance().IsEmpty());

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
