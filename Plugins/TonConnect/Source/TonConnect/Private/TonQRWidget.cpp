#include "TonQRWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Texture2D.h"
#include "Styling/SlateBrush.h"

void UTonQRWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();

    // Build widget tree here — this fires during CreateWidget(), before AddToViewport()
    // builds the Slate representation. NativeConstruct() fires after Slate is built
    // and can no longer affect the tree layout.
    RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>();
    WidgetTree->RootWidget = RootCanvas;

    // Semi-transparent black backdrop (full-screen)
    UImage* Backdrop = WidgetTree->ConstructWidget<UImage>();
    {
        FSlateBrush Brush;
        Brush.TintColor = FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.75f));
        Backdrop->SetBrush(Brush);
    }
    UCanvasPanelSlot* BackdropSlot = RootCanvas->AddChildToCanvas(Backdrop);
    BackdropSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
    BackdropSlot->SetOffsets(FMargin(0.f));
    BackdropSlot->SetZOrder(0);

    // QR image — centered 300×300
    QRImage = WidgetTree->ConstructWidget<UImage>();
    UCanvasPanelSlot* QRSlot = RootCanvas->AddChildToCanvas(QRImage);
    QRSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
    QRSlot->SetAlignment(FVector2D(0.5f, 0.5f));
    QRSlot->SetAutoSize(false);
    QRSlot->SetSize(FVector2D(300.f, 300.f));
    QRSlot->SetPosition(FVector2D(0.f, 0.f));
    QRSlot->SetZOrder(1);

    if (PendingTexture)
        ApplyTexture();
}

void UTonQRWidget::SetQRTexture(UTexture2D* Texture)
{
    PendingTexture = Texture;
    if (QRImage)
        ApplyTexture();
}

void UTonQRWidget::ApplyTexture()
{
    if (!QRImage || !PendingTexture) return;

    FSlateBrush Brush;
    Brush.SetResourceObject(PendingTexture);
    Brush.ImageSize  = FVector2D(300.f, 300.f);
    Brush.DrawAs     = ESlateBrushDrawType::Image;
    Brush.Tiling     = ESlateBrushTileType::NoTile;
    QRImage->SetBrush(Brush);
}
