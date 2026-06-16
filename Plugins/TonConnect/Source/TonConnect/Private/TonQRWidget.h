#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TonQRWidget.generated.h"

class UImage;
class UCanvasPanel;

// Reusable QR display widget — created programmatically, no Blueprint asset required.
// Used by the keyboard demo (ATonConnectDemoActor) to render the QR itself.
UCLASS()
class UTonQRWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void SetQRTexture(UTexture2D* Texture);

protected:
    virtual void NativeOnInitialized() override;

private:
    void ApplyTexture();

    UPROPERTY() UCanvasPanel* RootCanvas = nullptr;
    UPROPERTY() UImage*       QRImage    = nullptr;
    UPROPERTY() UTexture2D*   PendingTexture = nullptr;
};
