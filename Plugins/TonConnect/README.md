# TonConnect for Unreal Engine 5.7

TON wallet integration plugin for Unreal Engine using the TON Connect 2.0 protocol.
Connects wallets (Tonkeeper, MyTonWallet, …) to your game via QR code or deep link — no custom backend required.

> **Note:** This is a personal side project built in my free time — provided as-is,
> no warranty or guaranteed support. Use it, fork it, send a PR. 🙂

---

## Prerequisites

| Requirement | Details |
|---|---|
| Unreal Engine | 5.5 or 5.7 (tested) |
| Platform | Win64 (shipping), Android, iOS (runtime) |
| Visual Studio | 2022 (MSVC 14.x) |
| Python 3.8+ | Optional — only for `Scripts/tongen.py` and regenerating the keyboard demo map |
| TON wallet app | Tonkeeper, MyTonWallet, or any TON Connect 2.0 compatible wallet |

No external SDKs, npm packages, or pre-built `.lib` files are required.
All crypto (x25519, NaCl box, Ed25519, SHA-256) is bundled as source-compiled ThirdParty.

---

## Installation

1. Copy `Plugins/TonConnect/` into your project's `Plugins/` folder.
2. Open your `.uproject` in a text editor and verify the plugin entry:

```json
{
  "Name": "TonConnect",
  "Enabled": true
}
```

3. Right-click the `.uproject` → **Generate Visual Studio project files**.
4. Build from VS or from the UE editor (**Compile** button).

The plugin registers automatically. No C++ changes are required in your game module.

---

## Playing the example maps (fastest way to test)

The plugin ships **two ready-to-play maps** in its own content folder. Nothing to build or
script — just open one and press **Play**. Both run end-to-end in **mock mode** (no real
wallet or network needed) or against a real wallet.

### 1. Make plugin content visible

Plugin content is hidden in the Content Browser by default:

> Content Browser → **Settings** (gear, top-right) → enable **Show Plugin Content**

Then browse to **Plugins → TonConnect Content → Maps**. You'll see:

| Map | Demo actor | What it shows |
|---|---|---|
| **`TonConnectUIExample`** | `ATonConnectUIDemoActor` | Full popup wallet UI (recommended) |
| **`TonConnectExample`** | `ATonConnectDemoActor` | Minimal keyboard-driven flow |

### 2. (Optional) enable mock mode to test without a wallet

**Edit → Project Settings → Plugins → TonConnect → Mock → Use Mock = ✔**
(or pass `-ton.mock` on the command line). Leave it off to connect a real wallet by QR.

### 3. Open a map and press Play

---

### Map A — `TonConnectUIExample` (popup UI)  ⭐ recommended

A self-contained pause-menu-style wallet popup, built entirely in C++ Slate (no UMG asset).

**Press `T`** in-game to toggle the popup. The flow:

```
[T] opens popup
   │
   ├─ Choose your wallet   → scrollable picker (name + icon, SSE-capable wallets)
   ├─ Connect Wallet       → QR appears + "Waiting for wallet…" spinner
   │                          (auto-closes QR on connect, shows a "Connected" toast)
   ├─ Connected view       → address, balance, wallet version, network badge
   │     │
   │     └─ Send form with 3 tabs:
   │          • TON    — recipient + amount (TON)
   │          • Jetton — your jetton wallet + recipient + amount (base units)
   │          • NFT    — NFT item address + new owner
   │        Each tab shows a rough fee + an on-chain emulated fee (auto-updates as you type).
   │
   └─ Disconnect           → clears the session
```

Inputs and the Disconnect button lock while a send is in flight. Results surface as
on-screen toasts. In mock mode the whole flow (connect → send → result) runs on timers.

> The network is **not** a setting — it's read from the wallet's connect event and shown as
> a `MAINNET` / `TESTNET` badge once connected (the raw address carries no network tag, so
> only the wallet knows). The **Network** project setting only chooses which bridge/API
> endpoint the plugin talks to.

To use it in your own level: **Place Actors** → search **`Ton Connect UI Demo Actor`** →
drag into the level → Play → press `T`. Set **bShowOnStart** in its Details to open
automatically.

---

### Map B — `TonConnectExample` (keyboard demo)

Minimal actor that prints state to the screen and reacts to number keys
(game window must have focus):

| Key | Action |
|-----|--------|
| `1` | Connect |
| `2` | Send 0.01 TON to a demo address |
| `3` | Disconnect |
| `4` | Send 0.01 TON to self |

Enable **bAutoSendAfterConnect** in the actor's Details to auto-send ~2 s after connecting —
handy for CI/smoke-testing with no key input.

#### Regenerating this map (optional)

The map is already committed, so you normally don't need this. To rebuild it from scratch
(e.g. after editing the setup script), run from the editor:

> **Tools → Execute Python Script** → `Plugins/TonConnect/Scripts/setup_example_level.py`

It creates lighting, a floor, a `PlayerStart`, the demo actor, and sets the demo game mode,
then saves to `/TonConnect/Maps/TonConnectExample`.

---

## Quick Start (your own code)

### Blueprint

```
GameInstance → Get Subsystem (TonConnectSubsystem)
  ├── Bind Event → OnQRReady       → show QR texture in UMG
  ├── Bind Event → OnConnected     → store WalletInfo, update HUD
  ├── Bind Event → OnError         → show error toast
  └── Call → Connect()
```

After `OnConnected` fires:

```
SendTon("EQ...", "1000000000", "Hello TON", ← delegate callback)
  └── OnResult → check Result == Approved, read TxHash
```

### C++

```cpp
UTonConnectSubsystem* TC = GameInstance->GetSubsystem<UTonConnectSubsystem>();

TC->OnConnected.AddLambda([](const FTonWalletInfo& Info)
{
    UE_LOG(LogTemp, Log, TEXT("Connected: %s"), *Info.Address);
});

TC->Connect();

// Later, after OnConnected:
FOnTonSendResultDelegate CB;
CB.BindLambda([](const FTonSendResult& R)
{
    if (R.Result == ETonSendResult::Approved)
        MyGame->WaitForTransaction(R.TxHash);
});
TC->SendTon(TEXT("EQ..."), TEXT("1000000000"), TEXT("payment"), CB);
```

### Estimating a fee before sending

```cpp
// Instant heuristic (no network) — good for a placeholder while typing:
FString Rough = UTonBlueprintLibrary::EstimateFeeQuick(ETonTxKind::NativeTransfer);

// Accurate on-chain emulation (debounced ~0.6s, falls back to the rough estimate on failure):
FOnTonFeeEstimateDelegate FeeCB;
FeeCB.BindLambda([](const FTonFeeEstimate& E)
{
    // E.TotalFeeNano, E.bEmulated (true = real emulation, false = fell back to rough)
});
TC->EstimateFeeEmulated(TEXT("EQ..."), TEXT("1000000000"), ETonTxKind::NativeTransfer, FeeCB);
// Jetton / NFT variants:
//   TC->EstimateFeeEmulatedJetton(JettonWalletAddr, DestAddr, AmountBaseUnits, FeeCB);
//   TC->EstimateFeeEmulatedNft(NftAddr, NewOwnerAddr, FeeCB);
```

---

## Events & API — binding guide

Everything goes through one object: the `UTonConnectSubsystem` (a `GameInstanceSubsystem`).
Get it once and bind its events, then drive the flow with its functions.

```cpp
UTonConnectSubsystem* TC = GetGameInstance()->GetSubsystem<UTonConnectSubsystem>();
```
```
Blueprint:  Get Game Instance → Get Subsystem (class = TonConnectSubsystem)
```

### Events you bind (multicast — `BlueprintAssignable`)

Bind these once (e.g. on `BeginPlay`) and leave them; they fire for the whole session
regardless of who started the action.

| Event | Payload | Fires when | Typical handler |
|---|---|---|---|
| `OnQRReady` | `UTexture2D* QRTexture, FString DeepLink` | A connect was started and the QR is ready | Show the texture in a UMG Image; use `DeepLink` for a tap-to-open button on mobile |
| `OnConnected` | `FTonWalletInfo WalletInfo` | Wallet approved the connection (or session restored) | Store address, switch HUD to "connected", hide QR |
| `OnAccountInfoUpdated` | `FString WalletVersion, FString BalanceNano` | Balance + wallet contract version fetched from TonAPI (right after connect, and on refresh) | Update balance label, show "W5 R1" etc. |
| `OnAssetsUpdated` | `FString JettonInfo, int32 NftCount` | Jetton + NFT holdings loaded (after connect) | Show `"10.00 USDT, 5.00 DOGS"` + NFT count |
| `OnSendResult` | `FTonSendResult Result` | Any `SendTon/Jetton/Nft/ContractMessage` resolves (approved/rejected/timeout/error) | Toast the result, read `Result.TxHash` |
| `OnDisconnected` | *(none)* | Wallet disconnected / session cleared | Reset HUD to "not connected" |
| `OnError` | `FString ErrorMessage` | Connect/bridge/network failure | Show error, allow retry |

```cpp
TC->OnQRReady.AddDynamic(this, &AMyHud::HandleQRReady);
TC->OnConnected.AddDynamic(this, &AMyHud::HandleConnected);
TC->OnAccountInfoUpdated.AddDynamic(this, &AMyHud::HandleAccountInfo);
TC->OnAssetsUpdated.AddDynamic(this, &AMyHud::HandleAssets);
TC->OnSendResult.AddDynamic(this, &AMyHud::HandleSendResult);
TC->OnDisconnected.AddDynamic(this, &AMyHud::HandleDisconnected);
TC->OnError.AddDynamic(this, &AMyHud::HandleError);
```
> Handlers bound with `AddDynamic` must be `UFUNCTION()`. In Blueprint use the red
> **Bind Event to …** nodes. Always **unbind on EndPlay** (`RemoveDynamic`) for actors.

### Single-cast callbacks (passed *into* a function call)

Some calls take a one-shot delegate for *that specific request* (you can use these instead
of, or alongside, the multicast events):

| Delegate type | Used by | Payload |
|---|---|---|
| `FOnTonSendResultDelegate` | `SendTon`, `SendJettonTransfer`, `SendNftTransfer`, `SendContractMessage` | `FTonSendResult` |
| `FOnTonFeeEstimateDelegate` | `EstimateFeeEmulated[Jetton/Nft]` | `FTonFeeEstimate` |
| `FOnTonGetMethodDelegate` | `CallGetMethod` | `FTonGetMethodResult` |

### Async & threading (important)

**Nothing in this API blocks the game thread.** Every call that touches the network or a
wallet returns immediately (`void`) and delivers its result *later* via an event or the
callback you passed in:

- All HTTP (TonAPI) and bridge (SSE) work is asynchronous; **callbacks fire on the game
  thread**, so it's safe to touch UMG/actors directly inside them — no marshalling needed.
- You will **not** get a return value from `Connect`, `SendTon`, `EstimateFee…`, etc. Wire
  up the event/callback and react there.

**Fee estimate is debounced and never immediate:**

- `EstimateFeeEmulated[Jetton/Nft]` waits **~0.6 s** (to coalesce rapid typing) *then* makes
  async TonAPI calls (`seqno` → `/wallet/emulate`). The result lands in your
  `FOnTonFeeEstimateDelegate` typically a few hundred ms after that — **not synchronously**.
- Calls share one debounce slot: if you call again before the previous one resolves, the
  **older request is dropped and its callback never fires** — only the latest request
  resolves. Don't assume one callback per call; assume one callback for the latest input.
- Need a number *right now* (e.g. a placeholder while the emulation is in flight)? Use the
  synchronous heuristic `UTonBlueprintLibrary::EstimateFeeQuick(Kind)` — it returns instantly,
  no network. The emulated callback also falls back to this value (`bEmulated = false`) if
  emulation fails.

### What `OnQRReady` gives you

`OnQRReady(UTexture2D* QRTexture, FString DeepLink)` delivers two things:

- **`QRTexture`** — a ready-to-display **square texture** of the QR code. Drop it into a UMG
  **Image** brush (`SetBrushFromTexture`). Use this for **scan-to-connect** (second device
  scans the screen). The plugin does **not** render any QR popup itself — you own the display.
- **`DeepLink`** — the `tc://` universal URL (same data the QR encodes). Use this for
  **tap-to-connect on the same device** (mobile): wire a button to
  `FPlatformProcess::LaunchURL(DeepLink)` (C++) or the **Launch URL** node (Blueprint) — it
  opens the installed wallet app directly. Also handy for a "Copy link" action. May be empty
  if the chosen wallet has no universal link.

You can also pull the link any time without the event:

```cpp
FString Link = TC->GetConnectDeepLink(); // BlueprintPure; empty when not connecting
```

> The demo popup (`TonConnectUIExample`) shows this in action: the QR page has an
> **"Open in wallet ↗"** button that appears whenever a deep-link is available.

---

### Flow 1 — Connect

```
(optional) FetchWalletList(cb)         → cb(bSuccess, TArray<FTonWalletListEntry>)   // build a picker
(optional) SelectWallet(app, url, br)                                                // which wallet to pair
Connect()
   │
   ├─►  OnQRReady(QRTexture, DeepLink) // show QR (scan) or open DeepLink (mobile tap) → user approves
   │
   ├─►  OnConnected(WalletInfo)        // address, network, version, device, features
   │
   ├─►  OnAccountInfoUpdated(ver, bal) // balance + contract version from TonAPI
   │
   └─►  OnAssetsUpdated(jettons, nfts) // jetton + NFT holdings
        (on any failure → OnError(msg))
```

- `void Connect()` — starts pairing with the wallet chosen via `SelectWallet()` (defaults to Tonkeeper).
- `void SelectWallet(AppName, UniversalUrl, BridgeUrl)` — pick a wallet from the list before `Connect()`.
- `void FetchWalletList(TFunction<void(bool, TArray<FTonWalletListEntry>)>)` — wallet registry for a picker (C++; cached on disk).

### Flow 2 — Restore a previous session (no QR)

```
RestoreSession()
   ├─►  OnConnected(WalletInfo)        // if the stored session is still valid (+ account/assets events)
   └─►  OnError(msg)                   // otherwise (expired/none) → fall back to Connect()
```

- `void RestoreSession()` — call on startup to silently reconnect; keys are read from the encrypted store.

### Flow 3 — Send a transaction

```
SendTon(to, amountNano, comment, cb)               // native TON
SendJettonTransfer(jettonWallet, to, amount, comment, cb)   // TEP-74
SendNftTransfer(nftItem, newOwner, cb)             // TEP-62
SendContractMessage(spec, values, to, amountNano, cb)       // arbitrary contract
   │
   │   user confirms in the wallet…
   │
   └─►  OnSendResult(Result)  (and the cb you passed)
            Result.Result  = Approved | Rejected | Timeout | Error
            Result.TxHash  = 64-hex hash (when Approved)
```

Optional confirmation wait (C++ only):
```cpp
TC->WaitForTransaction(Result.TxHash, 30.f, [](bool bFound, FTonTxEntry Tx){ /* … */ });
```

### Flow 4 — Estimate a fee before sending (debounced ~0.6 s)

```
EstimateFeeEmulated(to, amountNano, Kind, cb)      // native
EstimateFeeEmulatedJetton(jettonWallet, to, amount, cb)
EstimateFeeEmulatedNft(nftItem, newOwner, cb)
   └─►  cb(FTonFeeEstimate)   // .TotalFeeNano, .bEmulated (false = fell back to heuristic)

UTonBlueprintLibrary::EstimateFeeQuick(Kind) → FString   // instant, no network
```

### Read-only state (poll any time, no events needed)

| Function | Returns |
|---|---|
| `GetState()` | `ETonConnectState` — Disconnected / Connecting / Connected / Disconnecting |
| `GetConnectedWallet()` | `FTonWalletInfo` — address, network, version, device, features |
| `GetCachedBalance()` | `FString` last known balance (nanoTON) |
| `GetConnectDeepLink()` | `FString` current `tc://` link (also from `OnQRReady`); empty when not connecting |
| `CallGetMethod(addr, method, cb)` | runs a get-method → `FTonGetMethodResult` (typed stack) |

---

### What's inside `FTonWalletInfo` (the `OnConnected` payload)

| Field | Example | Notes |
|---|---|---|
| `Address` / `RawAddress` | `0QD…` / `0:abc…` | friendly + raw forms |
| `Network` | `mainnet` / `testnet` | **authoritative**, from the wallet — use this, not a setting |
| `WalletName` | `Tonkeeper` | `device.appName` |
| `WalletAppName` | `tonkeeper` | wallet-list slug you paired with |
| `WalletVersion` | `W5 R1`, `V4R2` | contract version (code-hash detect + TonAPI) |
| `AppVersion` / `Platform` | `5.0.1` / `android` | from `device` |
| `MaxMessages` | `255` (W5), `4` (V4) | max messages per `sendTransaction` |
| `bSupportsSignData` | `true`/`false` | SignData feature |
| `TonProofSignature` | *(hex)* | only if the connect asked for ton_proof |

### Minimal end-to-end (C++)

```cpp
void AMyHud::BeginPlay()
{
    Super::BeginPlay();
    TC = GetGameInstance()->GetSubsystem<UTonConnectSubsystem>();
    TC->OnQRReady.AddDynamic(this,   &AMyHud::HandleQRReady);
    TC->OnConnected.AddDynamic(this, &AMyHud::HandleConnected);
    TC->OnSendResult.AddDynamic(this,&AMyHud::HandleSendResult);
    TC->OnError.AddDynamic(this,     &AMyHud::HandleError);

    TC->RestoreSession();   // silent reconnect; OnError → user taps "Connect"
}

void AMyHud::OnConnectButton() { TC->Connect(); }              // → OnQRReady → OnConnected
void AMyHud::HandleQRReady(UTexture2D* Tex) { /* show Tex */ }
void AMyHud::HandleConnected(const FTonWalletInfo& W) { /* show W.Address, W.Network */ }

void AMyHud::OnPayButton()
{
    FOnTonSendResultDelegate Cb; // or just rely on the OnSendResult multicast
    TC->SendTon(TEXT("EQ…"), TEXT("1000000000"), TEXT("buy gold"), Cb);
}
void AMyHud::HandleSendResult(const FTonSendResult& R)
{
    if (R.Result == ETonSendResult::Approved) { /* R.TxHash */ }
}
```

> See `Private/TonConnectUIDemoActor.cpp` for a complete, working reference that binds
> every event above and drives the popup UI.

---

## Features

### Connection
| Feature | Detail |
|---|---|
| Wallet picker | Live TON Connect wallet registry (cached on disk), SSE-capable wallets with name + icon |
| QR code | Auto-generated UTexture2D, ready for a UMG Image widget |
| Deep link | Full URL in log for mobile testing |
| Connect-event data | Wallet app name/version/platform, `maxMessages`, SignData support, ton_proof |
| Wallet version | Detected from `walletStateInit` code hash (works pre-deploy) — e.g. `W5 R1`, `V4 R2` |
| Network badge | Authoritative network read from the wallet's connect event (not from settings) |
| Session restore | Keys encrypted at rest (DPAPI on Win64, Keychain on iOS, secretbox elsewhere) |
| Session TTL | Auto-expires after 21 days |

### Transactions
| Method | Description |
|---|---|
| `SendTon` | Native TON transfer with optional text comment |
| `SendJettonTransfer` | TEP-74 jetton transfer (requires sender's jetton wallet address) |
| `SendNftTransfer` | TEP-62 NFT item transfer |
| `SendContractMessage` | Arbitrary contract call from a `UTonMessageSpec` DataAsset |
| `WaitForTransaction` | Poll chain for tx confirmation (C++ only, 2 s interval) |

### Fee Estimation
| Method | Description |
|---|---|
| `EstimateFeeQuick` | Instant per-kind heuristic (nanoTON), no network — a rough floor only |
| `EstimateFeeEmulated` | Debounced on-chain emulation via TonAPI (`/wallet/emulate`) for a native transfer |
| `EstimateFeeEmulatedJetton` | Debounced emulation for a TEP-74 jetton transfer |
| `EstimateFeeEmulatedNft` | Debounced emulation for a TEP-62 NFT transfer |

> Emulation builds an unsigned external message for the connected wallet contract
> (v4r2 and v5r1 / W5 supported) and asks TonAPI to emulate it. If the wallet version is
> unsupported or the call fails, it transparently falls back to the quick estimate
> (`bEmulated = false`).

### On-chain Queries
| Method | Description |
|---|---|
| `GetBalance` | TON balance in nanoTON |
| `GetJettonBalances` | All jetton holdings with symbol, decimals, wallet address |
| `GetNfts` | NFT items with collection address and metadata name |
| `CallGetMethod` | Read-only smart contract method call, returns typed stack |

### Contract ABI
| Feature | Detail |
|---|---|
| `UTonMessageSpec` | Blueprint DataAsset — define op code + ordered field list |
| `FTonCellBuilder` | Build TL-B cells from spec + `TMap<FString,FString>` at runtime |
| **Tact ABI import** | Drag `.abi.json` into Content Browser → auto-creates `UTonMessageSpec` assets |
| **TL-B import** | Drag `.tlb` into Content Browser → same |
| `Scripts/tongen.py` | CLI tool: prints field definitions from a Tact ABI file |
| **Example files** | `Examples/tact/` — JettonWallet, NftCollection, GameContract · `Examples/tlb/` — TEP-74, TEP-62, TEP-81, TEP-89 |

### Developer / Testing
| Feature | Detail |
|---|---|
| Mock mode | `-ton.mock` CLI flag or `Use Mock = true` in Developer Settings |
| Mock bridge | Auto-approves or auto-rejects after a configurable delay |
| Mock API | Fixture wallet list, USDT jetton, NFT item, seqno + emulate |
| `UTonConnectDeveloperSettings` | Project Settings → TonConnect → network endpoint, connect timeout, mock toggle |

---

## Configuration

Open **Edit → Project Settings → Plugins → TonConnect**:

| Setting | Default | Description |
|---|---|---|
| Network | Mainnet | `Mainnet` / `Testnet` pick the bridge + TonAPI endpoints; `Custom` uses the overrides below |
| Bridge URL Override | `https://bridge.tonapi.io/bridge` | SSE bridge endpoint (only used when Network = Custom) |
| TonAPI URL Override | `https://tonapi.io/v2` | REST API base (only used when Network = Custom) |
| Manifest URL | *(public manifest)* | `tonconnect-manifest.json` URL shown to the wallet |
| Connect Timeout (seconds) | 180 | Give up + fire `OnError` if no wallet/bridge response in this window (0 = wait forever) |
| Use Mock | false | Enable mock mode (no real wallet or network needed) |
| Mock Connect Result | Approve | Auto-approve / auto-reject the connect |
| Mock Send Result | Approve | Auto-approve / auto-reject send requests |
| Mock Delay Seconds | 1.0 | Simulated round-trip delay |

> **Network is an endpoint choice, not a guarantee.** A wallet may still connect on a
> different network; the *actual* connected network comes from the connect event and is
> exposed via `FTonWalletInfo::Network` (and the popup's badge).

---

## Platform Notes

| Platform | Session Store | Status |
|---|---|---|
| Win64 | DPAPI (`CryptProtectData`) | Shipping-ready |
| iOS | Keychain (`SecItemAdd`) | Implemented, needs device test |
| Android | Encrypted file (secretbox) | Working, hardware Keystore TODO |

---

## Third-party Libraries (bundled)

| Library | Purpose | License |
|---|---|---|
| **tweetnacl** | x25519 key exchange, NaCl box, Ed25519 | Public domain |
| **sha256** | SHA-256 for address checksum + cell hashing | Public domain |
| **qrcodegen** | QR texture generation | MIT |

No runtime dependencies on Boost, libsodium, or any external service beyond the TON bridge + TonAPI.

---

## Directory Structure

```
Plugins/TonConnect/
├── Content/
│   └── Maps/
│       ├── TonConnectUIExample.umap  ← popup wallet UI demo (press T)
│       └── TonConnectExample.umap    ← keyboard demo (1/2/3/4)
├── Examples/
│   ├── tact/
│   │   ├── JettonWallet.abi.json   ← TEP-74 wallet messages (transfer, burn, notification…)
│   │   ├── NftCollection.abi.json  ← TEP-62 NFT messages (transfer, mint…)
│   │   └── GameContract.abi.json   ← Custom game contract template
│   └── tlb/
│       ├── tep74_jetton.tlb        ← TEP-74 canonical TL-B scheme
│       ├── tep62_nft.tlb           ← TEP-62 canonical TL-B scheme
│       ├── tep81_dns.tlb           ← .ton DNS auction messages
│       └── tep89_jetton_voting.tlb ← DAO/governance voting messages
├── Scripts/
│   ├── setup_example_level.py  ← regenerates the keyboard demo map
│   └── tongen.py               ← CLI: Tact ABI → UTonMessageSpec field definitions
├── Source/
│   ├── TonConnect/            ← Runtime module
│   │   ├── Public/
│   │   │   ├── TonConnectSubsystem.h     ← connect, send, estimate, queries
│   │   │   ├── TonConnectUIDemoActor.h   ← popup UI demo actor
│   │   │   ├── TonBlueprintLibrary.h     ← FormatTon, ParseTonAmount, EstimateFeeQuick…
│   │   │   ├── TonTypes.h     ← FTonWalletInfo, FTonSendResult, FTonFeeEstimate, delegates
│   │   │   ├── ITonApiClient.h
│   │   │   ├── ITonBridgeTransport.h
│   │   │   ├── ISessionStore.h
│   │   │   ├── TonSession.h
│   │   │   ├── TonUtils.h     ← Address conversion, hex/base64 helpers
│   │   │   ├── Cells/         ← TonCell, TonBoc, TonWalletMessage (external-message builder)
│   │   │   └── Contract/      ← TonMessageSpec, TonCellBuilder
│   │   └── Private/           ← Implementation (Transport, Api, Store, Cells, UI/STonConnectPanel)
│   └── TonConnectEditor/      ← Editor module (Win64 only): .json / .tlb import factories
└── TonConnect.uplugin
```

See `MANUAL_TEST.md` for a step-by-step manual test checklist.
