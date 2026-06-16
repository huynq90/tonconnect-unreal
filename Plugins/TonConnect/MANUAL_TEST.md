# TonConnect — Manual Test Checklist

Run these tests top-to-bottom. Each section can be tested independently.  
Tick off `[x]` as you go. Failures go in the **Notes** column.

---

## 0. Setup

- [ ] Plugin compiles cleanly (no errors in Output Log on Launch)
- [ ] `UTonConnectDeveloperSettings` visible in **Project Settings → TonConnect**
- [ ] `TonConnectSubsystem` accessible from Blueprint via **Get Subsystem**

### Quick smoke-test with the demo actor

The fastest way to verify the plugin is working:

1. Open any level → **Place Actor** → search `TonConnect Demo Actor` → drag into viewport
2. Project Settings → TonConnect → `bUseMock = true`
3. Hit **Play**
4. Top of screen shows: `State: Connecting…` → `State: Connected  |  EQD_mock…`
5. Press **2** → top line shows `State: Connected`, log shows `✔ Send Approved`
6. Press **3** → top line shows `State: Disconnected`

All 6 steps passing = plugin is wired up correctly. Continue below for detailed tests.

---

## 1. Mock Mode — no wallet needed

**How to enable:** Project Settings → TonConnect → `bUseMock = true`  
(or launch with `-ton.mock` CLI flag)

### 1.1 Connect flow

| # | Action | Expected | Notes |
|---|---|---|---|
| 1 | Call `Connect()` | `OnQRReady` fires with a non-null `UTexture2D*` | |
| 2 | Wait ~1s | `OnConnected` fires with `Address = "EQD_mock_wallet_..."` | |
| 3 | Check `GetState()` | Returns `ETonConnectState::Connected` | |
| 4 | Check `GetConnectedWallet().Network` | `"mainnet"` | |

### 1.2 SendTon — approve

| # | Action | Expected | Notes |
|---|---|---|---|
| 5 | Call `SendTon("EQ...", "1000000000", "test", callback)` | Callback fires within 2s | |
| 6 | Check `Result` | `ETonSendResult::Approved` | |
| 7 | Check `TxHash` | Non-empty 64-char hex string | |
| 8 | `OnSendResult` (global delegate) | Also fires with same result | |

### 1.3 SendTon — reject

| # | Action | Expected | Notes |
|---|---|---|---|
| 9 | Set `MockBehavior = AutoReject` in Project Settings | | |
| 10 | Call `SendTon(...)` | Callback fires within 2s | |
| 11 | Check `Result` | `ETonSendResult::Rejected` | |

### 1.4 Session persistence

| # | Action | Expected | Notes |
|---|---|---|---|
| 12 | Connect (mock), then close and relaunch | `LoadSession` log line appears | |
| 13 | Call `RestoreSession()` | `OnConnected` fires with same address | |

### 1.5 Disconnect

| # | Action | Expected | Notes |
|---|---|---|---|
| 14 | Call `Disconnect()` | `OnDisconnected` fires | |
| 15 | Check `GetState()` | `ETonConnectState::Disconnected` | |
| 16 | Relaunch — RestoreSession() | `OnError` fires (session was cleared) | |

---

## 2. Mock Mode — API queries

| # | Action | Expected | Notes |
|---|---|---|---|
| 17 | `GetBalance(addr, cb)` via `ApiClient` | `"5000000000"` (5 TON) | |
| 18 | `GetJettonBalances(addr, cb)` | 1 entry, Symbol=`"USDT"`, Decimals=6 | |
| 19 | `GetNfts(addr, cb)` | 1 entry, Name=`"Mock NFT #1"` | |
| 20 | `GetHistory(addr, 10, cb)` | 1 entry, Comment=`"mock transfer"` | |
| 21 | `GetTransaction("any_hash", cb)` | `bFound=true`, immediate | |
| 22 | `CallGetMethod(addr, "balance", cb)` | `bSuccess=true`, Stack["0"]="0" | |

---

## 3. Mock Mode — Jetton & NFT transfers

| # | Action | Expected | Notes |
|---|---|---|---|
| 23 | `SendJettonTransfer(walletAddr, dest, "10000000", "", cb)` | Approved, no comment | |
| 24 | `SendJettonTransfer(walletAddr, dest, "10000000", "hi", cb)` | Approved, with comment | |
| 25 | `SendNftTransfer(nftAddr, dest, cb)` | Approved | |

---

## 4. Mock Mode — WaitForTransaction

| # | Action | Expected | Notes |
|---|---|---|---|
| 26 | `WaitForTransaction("mock_hash", 10.0, cb)` | `bFound=true` within first poll (2s) | |
| 27 | Set mock to return `bFound=false` (modify TonMockApiClient::GetTransaction temporarily) | Times out after 10s, `bFound=false` | |

---

## 5. Mock Mode — UTonMessageSpec + TonCellBuilder

| # | Action | Expected | Notes |
|---|---|---|---|
| 28 | Create a `UTonMessageSpec` asset: Opcode=0x1234, Fields=[{Name="amount", Type=Coins}] | Asset saves OK | |
| 29 | Call `SendContractMessage(Spec, {{"amount","1000000000"}}, dest, "50000000", cb)` | Approved | |
| 30 | With null Spec | `OnResult` fires with `ETonSendResult::Error` | |

---

## 6. Editor Import

Ready-made example files are in `Plugins/TonConnect/Examples/`.

### 6a. Tact ABI (.json)

| # | File | Action | Expected |
|---|---|---|---|
| 31 | `Examples/tact/JettonWallet.abi.json` | Drag into Content Browser | 5 assets: `TokenTransfer`, `TokenBurn`, etc. with correct opcodes |
| 32 | `Examples/tact/GameContract.abi.json` | Drag into Content Browser | 5 assets: `RegisterPlayer`, `UpdateScore`, etc. |
| 33 | `Examples/tact/NftCollection.abi.json` | Drag into Content Browser | 5 assets including `Transfer` (opcode `0x5fcc3d14`) |
| 34 | Open `TokenTransfer` asset | Check fields | queryId(UInt64), amount(Coins), destination(Address), responseDestination(Address), forwardTonAmount(Coins) |
| 35 | Open `UpdateScore` asset | Check fields | playerId(UInt64), score(UInt32), timestamp(UInt32) |

### 6b. TL-B scheme (.tlb)

| # | File | Action | Expected |
|---|---|---|---|
| 36 | `Examples/tlb/tep74_jetton.tlb` | Drag into Content Browser | Assets: `transfer`(0x0f8a7ea5), `burn`(0x595f07bc), `excesses`(0xd53276db), etc. |
| 37 | Open `transfer` asset | Check fields | query_id(UInt64), amount(Coins), destination(Address), response_destination(Address), forward_ton_amount(Coins) |
| 38 | `Examples/tlb/tep62_nft.tlb` | Drag into Content Browser | Assets: `transfer`(0x5fcc3d14), `get_static_data`(0x2fcb26a2), etc. |
| 39 | `Examples/tlb/tep81_dns.tlb` | Drag into Content Browser | DNS auction messages with correct opcodes |
| 40 | `Examples/tlb/tep89_jetton_voting.tlb` | Drag into Content Browser | Governance messages (vote, delegate, lock) |

### 6c. tongen.py (CLI)

```bash
# From the plugin root:
python Scripts/tongen.py Examples/tact/GameContract.abi.json
```

Expected output:
```
// --- RegisterPlayer (opcode 0x00000001) ---
// FTonFieldSpec { "playerId",   ETonFieldType::UInt64, 0 }
// FTonFieldSpec { "playerAddr", ETonFieldType::Address, 0 }

// --- UpdateScore (opcode 0x00000002) ---
...
```

### 6d. SendContractMessage end-to-end (mock)

| # | Action | Expected |
|---|---|---|
| 41 | Import `GameContract.abi.json`, assign `UpdateScore` spec in BP | Asset reference loads OK |
| 42 | In mock mode: `SendContractMessage(UpdateScoreSpec, {{"playerId","42"},{"score","9999"},{"timestamp","1718000000"}}, gameAddr, "50000000", cb)` | Approved |
```

---

## 7. Real Wallet — Testnet (requires Tonkeeper testnet)

> Enable testnet in Tonkeeper: Settings → Dev Tools → Switch to Testnet  
> Set Project Settings → Network ID = `-3`

| # | Action | Expected | Notes |
|---|---|---|---|
| 37 | Disable mock, set Network ID = `-3` | | |
| 38 | Call `Connect()` | QR texture appears | |
| 39 | Scan QR with Tonkeeper testnet | Phone shows connection approval dialog | |
| 40 | Approve on phone | `OnConnected` fires within 5s, Address starts with `EQ` or `UQ` | |
| 41 | `GetConnectedWallet().Network` | `"testnet"` | |
| 42 | `GetBalance(address, cb)` | Returns testnet balance in nanoTON | |

### 7.1 SendTon

| # | Action | Expected | Notes |
|---|---|---|---|
| 43 | `SendTon(testAddress, "10000000", "game payment", cb)` | Phone shows confirm dialog (0.01 TON) | |
| 44 | Approve on phone | `ETonSendResult::Approved`, TxHash non-empty | |
| 45 | Call `WaitForTransaction(TxHash, 60.0, cb)` | `bFound=true` within ~10–30s | |
| 46 | Reject on phone | `ETonSendResult::Rejected` | |
| 47 | Ignore / let expire | `ETonSendResult::Timeout` (after 5 min) | |

### 7.2 Wallet mismatch

| # | Action | Expected | Notes |
|---|---|---|---|
| 48 | Set Network ID = `-1` (mainnet), scan with testnet wallet | `OnError` fires with "network mismatch" | |

---

## 8. Session TTL

> These tests require manually adjusting the stored timestamp.

| # | Action | Expected | Notes |
|---|---|---|---|
| 49 | Connect and save session. Hex-edit saved file: set timestamp to `0x00000000` (epoch) | | |
| 50 | Restart, call `RestoreSession()` | Session rejected (TTL expired), `OnError` fires | |

---

## 9. Stress / Edge Cases

| # | Action | Expected | Notes |
|---|---|---|---|
| 51 | Call `Connect()` twice without disconnecting | Second call logs warning, returns without starting new session | |
| 52 | Call `SendTon` before `OnConnected` | `OnResult` fires immediately with `ETonSendResult::Error`, message "Not connected" | |
| 53 | `SendJettonTransfer` with invalid DestAddr `"not_an_address"` | `OnResult` fires with `Error` | |
| 54 | `Disconnect()` while a send is pending | No crash, pending callback never fires (map cleared) | |
| 55 | PIE restart (Deinitialize + Initialize) | No timer leaks, no dangling pointers | |

---

---

## 10. UE Automation Tests (Session Frontend)

Pure C++ unit tests — no wallet or game world needed. Run from the editor.

### How to run

1. Open project in UE Editor
2. **Window → Session Frontend → Automation** tab
3. Filter by `TonConnect.` to see all plugin tests
4. Select all → **Start Tests**

### Test list

| Filter path | What it covers |
|---|---|
| `TonConnect.Format.FormatTon` | `FormatTon("1000000000", 2)` = `"1.00 TON"`, zero, 0-decimals |
| `TonConnect.Format.ParseTonAmount` | `"1.5"` → `"1500000000"`, `"0.000000001"` → `"1"` |
| `TonConnect.Format.FormatJetton` | 6-decimal USDT format + no-symbol variant |
| `TonConnect.Format.TruncateAddress` | Short address returned unchanged; long address gets `…` |
| `TonConnect.Format.IsValidTonAddress` | Raw `0:hex64`, friendly 48-char, empty, too-short |
| `TonConnect.Address.ParseRaw` | `0:3333…` → WC=0, Hash[0]=0x33 |
| `TonConnect.Address.ParseBadHex` | Bad hex rejected, empty rejected |
| `TonConnect.Address.RoundTrip` | All 4 tag variants encode+decode to same WC/hash/flags |
| `TonConnect.Address.Masterchain` | WC=-1 survives round-trip |
| `TonConnect.Cell.WriteUint` | `0xDEADBEEF` in 32 bits = `[0xDE,0xAD,0xBE,0xEF]` |
| `TonConnect.Cell.WriteVarUint` | Zero=4 bits; 1=12 bits; 256=20 bits |
| `TonConnect.Cell.TextComment` | op=`0x00000000` + `"hi"` = 48 bits, bytes `00 00 00 00 68 69` |
| `TonConnect.Cell.BocBase64NonEmpty` | BOC output starts with `te6c` (magic `0xB5EE9C72`) |
| `TonConnect.Cell.WriteAddress` | 267-bit layout, first byte `0x80` (type=`10`, anycast=0) |
| `TonConnect.Cell.JettonTransfer` | Op bytes `0F 8A 7E A5`, no child refs |
| `TonConnect.TlbParser.ParseTransfer` | TEP-74 `transfer#0f8a7ea5`: name, opcode, 3 fields typed correctly |
| `TonConnect.TlbParser.ParseNoTag` | Constructor without `#tag` gets opcode=0 |
| `TonConnect.TlbParser.ParseInvalid` | Empty line and missing `=` return false |
| `TonConnect.TlbParser.ParseFile` | Multi-line .tlb → 2 messages with correct names/opcodes |
| `TonConnect.TlbParser.TypeMapping` | uint8, int32, Bool, addr_std, Coins all map correctly |

**Expected result:** all 20 tests green, 0 errors.

### Protocol regression tests (3 additional)

These cover the 3 protocol fixes applied after the initial handshake review.  
Run with the same filter step above, or `TonConnect.Protocol`:

| Test filter | What it covers |
|---|---|
| `TonConnect.Protocol.MockBridgeUrl` | Both wallets in mock list have `BridgeUrl` populated; Tonkeeper URL = `bridge.tonapi.io/bridge` |
| `TonConnect.Protocol.RpcEnvelopeSchema` | sendTransaction RPC JSON has `jsonrpc:"2.0"`, `method`, `id`, `params[0]` with `messages` + `valid_until` |
| `TonConnect.Protocol.TtlExpiry` | (PIE only) Injecting a fake pending result then calling `Test_ExpirePending` removes it from `PendingResults` |

> `TtlExpiry` soft-skips outside PIE — run it together with Section 11 tests.

**Combined expected result:** all 23 tests green.

---

## 11. Latent Mock Flow Tests (PIE / -game)

These tests require a live game world. Run in **Play-in-Editor** or `-game` mode.

### How to run (PIE)

1. Enable mock: Project Settings → TonConnect → `bUseMock = true`
2. Hit **Play**
3. Session Frontend → Automation → filter `TonConnect.Mock.` → Start Tests

### How to run (-game)

```
<Editor>.exe <YourProject>.uproject <Map> -game -ton.mock -ExecCmds="Automation RunTests TonConnect.Mock"
```

| Test | What it checks | Notes |
|---|---|---|
| `TonConnect.Mock.ConnectFlow` | `Connect()` → state becomes `Connecting` → waits ≤5 s for `Connected` | Soft-skip if no subsystem (outside PIE) |
| `TonConnect.Mock.SendTon` | `SendTon()` dispatches without crash; waits 1.5 s | Skipped if not connected; result capture tested in Gauntlet |

---

## 12. Gauntlet (Full Session End-to-End)

Full packaged game session — verifies the complete connect→send→disconnect flow.

### Requirements

- Packaged build (Development or Test configuration)
- `UTonConnectGauntletController` registered in the game (add to a GameMode or via `-TonGauntlet` flag)

### Launch command

```
<Game>.exe <Map> -gauntlet -TonGauntlet -ton.mock -ton.mock.approve_delay=0.5 -nullrhi -log
```

### Steps executed automatically

| Step | Asserts |
|---|---|
| 1. WaitForWorld | Subsystem found within first ticks |
| 2. Connect | State transitions to `Connecting` within 2 s |
| 3. WaitConnect | `OnConnected` fires within 10 s; `Address` non-empty |
| 4. SendTon | `OnSendResult` fires within 10 s |
| 5. Assert send | `ETonSendResult::Approved` |
| 6. Disconnect | `Disconnect()` called; `EndTest(0)` |

**Pass:** process exits 0, log shows `[TonGauntlet] Send approved — all steps PASSED`  
**Fail:** process exits 1, log shows `[TonGauntlet] FAIL: <reason>`

---

## Pass Criteria

- **Section 10** (Automation Tests): all 23 pass (20 + 3 protocol) → unit logic is correct
- **Section 11** (Latent Mock): both pass in PIE → async flow works in-engine
- **Section 12** (Gauntlet): exits 0 → full end-to-end session works
- **Sections 1–9** (manual): all pass before real-wallet testing and shipping

Any `Fatal`, `Error` in Output Log during a test = **fail** (warnings are OK for deliberate error paths).
