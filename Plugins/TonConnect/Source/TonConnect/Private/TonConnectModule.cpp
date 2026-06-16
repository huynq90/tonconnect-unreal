#include "Modules/ModuleManager.h"

class FTonConnectModule : public IModuleInterface
{
public:
    virtual void StartupModule() override {}
    virtual void ShutdownModule() override {}
};

IMPLEMENT_MODULE(FTonConnectModule, TonConnect)
