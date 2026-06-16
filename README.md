# TonConnect for Unreal Engine

Connect [TON](https://ton.org) wallets (Tonkeeper, MyTonWallet, …) to an Unreal Engine
game via the **TON Connect 2.0** protocol — QR code or deep link, no custom backend,
no external SDKs. This repo is the reusable plugin **plus** a playable Third Person demo.

> **Note:** a personal side project built in my free time. Provided as-is, no warranty or
> guaranteed support. Use it, fork it, send a PR. 🙂

---

## What's inside

| Path | What it is |
|---|---|
| **`Plugins/TonConnect/`** | The reusable plugin — drop it into any UE project. C++ only. |
| **`Source/UETonConnectExample/`** | Minimal Third Person game module that hosts the demo. |
| **`Content/ThirdPerson/`** | Demo level, character, and the wallet UI Blueprints. |

All crypto (x25519, NaCl box, Ed25519, SHA-256) is bundled as source-compiled
third-party code — **no prebuilt `.lib`, no npm, no backend required.**

---

## Quick start

**Requirements:** Unreal Engine **5.7** (plugin also builds on 5.5), Visual Studio 2022.

1. Clone (the repo uses **Git LFS** for `.uasset`/`.umap` — install it first):
   ```bash
   git lfs install
   git clone https://github.com/huynq90/tonconnect-unreal.git
   ```
2. Right-click `UETonConnectExample.uproject` → **Generate Visual Studio project files**.
3. Open the `.sln`, build **Development Editor**, then open the `.uproject`.
4. The demo maps live in the **plugin's** content. Open the **Content Drawer** →
   **Settings** gear (top-right) → tick **Show Plugin Content**. Then open a map
   (under *TonConnect Content → Maps*) and press **Play**:
   - **`TonConnectUIExample`** ⭐ — pop-up wallet UI (Connect → QR → Send → Disconnect).
   - **`TonConnectExample`** — keyboard-driven demo (keys 1–4).

**Test without a real wallet:** Project Settings → *TON Connect* → enable **Use Mock**
(or launch with `-ton.mock`). The whole connect/send flow runs against a mock bridge.

> Full walkthrough, screenshots of the flow, Blueprint & C++ snippets, the events/API
> binding guide, and configuration are in the plugin README:
> **[`Plugins/TonConnect/README.md`](Plugins/TonConnect/README.md)**.

---

## Using the plugin in your own project

1. Copy `Plugins/TonConnect/` into your project's `Plugins/` folder.
2. Add `"TonConnect"` to your module's `PublicDependencyModuleNames` (for C++ use).
3. Regenerate project files and build.

Minimal Blueprint flow: **Get Ton Connect** → `Connect` → bind `OnQRReady`
(show the QR `Texture` / open the `DeepLink`) → bind `OnConnected` → `SendTon`.
See the [plugin README](Plugins/TonConnect/README.md#quick-start-your-own-code) for details.

---

## Features (plugin)

- Wallet connect via QR **and** deep link, session restore, connect timeout
- Native TON, **Jetton** (TEP-74) and **NFT** (TEP-62) transfers
- Fee estimation (emulated), on-chain queries (balance, jettons, NFTs)
- TL-B cell / BOC builder, TonAPI client
- Mock bridge + mock API for testing the full flow with no wallet
- Encrypted session store (Win64 DPAPI, libsodium secretbox fallback)

---

## License

Personal project, provided as-is. See individual third-party libraries under
`Plugins/TonConnect/Source/TonConnect/ThirdParty/` for their respective licenses.
