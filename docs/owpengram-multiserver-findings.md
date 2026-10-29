# OwpenGram multi-server feature — findings and porting reference

Source of truth: [`owpengram/owpengram-desktop-client`](https://github.com/owpengram/owpengram-desktop-client),
a fork of Telegram Desktop. Fork tip analysed: `d5a262f7baa1a80f747b385fd06cc1f5d57dfe63`.
Upstream baseline: `e6bf3e41ac^2` (upstream `v7.2.2`, MTProto API layer 229).
Total diff against upstream `v7.2.2`: **175 files, +8079 / -536**.

This document is a research artifact for porting the feature into
[`roon9e/polysrv-desktop`](https://github.com/roon9e/polysrv-desktop) (PolySRV).
It describes **what the feature is and how it works** — not what PolySRV should do.

No OwpenGram branding is carried over by this port. The `owpg://` scheme becomes
`srv://`; the built-in "OwpenGram" server entry becomes a PolySRV-configured one.

---

## 1. Executive summary

OwpenGram's multi-server feature is a **small, surgical change to the MTProto
connection layer plus a server-selection UI**. It does not fork the protocol or
the API schema.

The core idea:

* A server is a `(id, name, host, port, rsaPublicKey, multiDc, mainDcId)` record.
* Two servers are **built in**: `telegram` (official Telegram DCs) and `official`
  (the fork's own server, hardcoded `152.89.254.50:2398` with a compiled-in RSA key).
* Users can add **custom servers**, stored in a local JSON file.
* Each `Main::Account` remembers which server it belongs to.
* Connecting to a non-Telegram server = replace the MTP DC options list with a
  single `host:port` mapped across every `dc_id`, and replace the set of trusted
  RSA public keys with the server's own PEM key.

The genuinely hard parts, and where the fork is fragile:

1. The DC-options list must be **locked**, or `help.getConfig` overwrites it.
2. Account identity is `(userId, server)` — the same numeric user id on two
   different servers must not collide.
3. Local state that is keyed by server (recent custom emoji, cloud lang packs,
   upload size limits) must be scoped per server, or it leaks across servers.
4. `tg://` links belong to Telegram only; `owpg://` links are routed by host.

---

## 2. Server list — source and persistence

### 2.1 Model

`Telegram/SourceFiles/owpengram/owpengram_servers.h`, namespace `Owpengram`:

```cpp
struct Server {
    QString id, name, host;
    int port = 0;
    QString description, rsaPublicKey, logoPath;
    bool isOfficial = false;
    bool isTelegram = false;
    // Multi-DC servers (Telegram) trust help.getConfig / the special loader
    // to discover real alternate data-center addresses. Single-server
    // backends are one physical machine, so every dc_id is mapped onto the
    // single host:port instead.
    bool multiDc = false;
    // Home data-center id. 0 = auto (multiDc -> 2, single-server -> 1).
    int mainDcId = 0;

    [[nodiscard]] bool valid() const {
        return !id.isEmpty() && !host.isEmpty() && port > 0;
    }
};
```

Reserved ids: `kOfficialServerId = "official"`, `kTelegramServerId = "telegram"`.

### 2.2 List composition

`ListServers()` (`owpengram_servers.cpp:608`) returns, in order:

1. `TelegramServer()` (`:559`) — id `telegram`, host `149.154.167.51`, port `443`,
   `multiDc=true`, `mainDcId=2`, `isTelegram=true`, logo `:/gui/art/telegram_logo_256.png`.
2. `OfficialServer()` (`:574`) — id `official`, host `kOfficialDefaultHost`
   (`u"152.89.254.50"`, `:50`), port `kOfficialDefaultPort` (`2398`, `:51`),
   `multiDc=false`, `mainDcId=1`, `rsaPublicKey = kOfficialRsaPublicKey`,
   logo `:/gui/art/logo_256.png`.
3. Custom servers from `ReadCustomServers()` (`:343`) — `tdata/owpengram_servers.json`.

**There is no remote server-list endpoint.** The list is entirely local: two
compile-time built-ins plus a user-managed JSON file. The fork never fetches a
list of servers; it fetches *metadata about one server at a time* (see 2.5).

### 2.3 Built-in default RSA key

`owpengram_servers.cpp:54-62` — a compile-time constant:

```
-----BEGIN RSA PUBLIC KEY-----
MIIBCgKCAQEAxUi0fsjiop7kTI+c+ATHwIs+XPcircj9WL/tNASH45/phiIvxhPU
z4T6OgUfDVpQEC8SWCuq77aZAhnpZ0rfQ+h6vEv7X970wIAPT/hbyzekyWEFmH5Q
RxSPrOMKF/V0wuTOVJcoHZW5r5cK7xsVe/otdYdOgt67kITS7pqoO1BlstRuOEHL
jhaJ/40dXocrWpDQlJP2TZFwk5JF1Pbx/2mLr/asQapc/qbQP82b+iDLW8WIBT0f
+xi4Js4k7Qo9kZSuUHUCDzmJt6Z0USBxxp/tSZRVjWRaT9ORDrdyfb/mKFSt9BtC
B5VRJ1e0q7P/9/w21T0p9uV3eNXhnPnLFQIDAQAB
-----END RSA PUBLIC KEY-----
```

### 2.4 Persistence

| Path | Content | Writer |
|---|---|---|
| `tdata/owpengram_servers.json` | JSON **array** of custom servers | `WriteCustomServersJson()` `:90` |
| `tdata/owpengram_builtin_identity.json` | JSON **object** keyed by built-in server id -> `{name, description, logoPath}` | `WriteBuiltinIdentity()` `:142` |
| `tdata/owpengram_server_logos/` | downloaded / user-picked logo files | `SaveCustomServerLogo`, `SaveFetchedServerLogo` |
| `tdata/owpengram_recent_emoji.json` | per-server custom-emoji cache | `core_settings.cpp` |
| `<account>/owpengram_server` | encrypted per-account selection | `Storage::Account` (section 4) |
| `<account>/owpengram_server.json` | plaintext fallback for the same | `Storage::Account` |

Custom-server JSON keys (`ServerFromJson` `:183`, `ServerToJson` `:198`):
`id, name, host, port, description, rsaPublicKey, logoPath, multiDc, mainDcId`.
`id` is a `QUuid::createUuid().toString(QUuid::WithoutBraces)` minted by
`AddCustomServer()` `:640`.

### 2.5 Per-server discovery endpoints (same host:port, plain HTTP)

`RawHttpGetBody(host, port, path)` (`:978`) — raw `QTcpSocket`, `GET path HTTP/1.1`,
3 s timeout, requires a literal `" 200 "` in the status line, returns the body
after `\r\n\r\n`.

| Endpoint | Response | Consumer |
|---|---|---|
| `GET /owpengram/server-info` | JSON `{ "rsa_public_key_pem": "...", "dc_id": N, "name": "...", "description": "...", "has_icon": bool }` | `FetchServerInfo()` `:1014` -> `ServerInfoFetchResult` |
| `GET /owpengram/server-icon` | raw image bytes | `FetchServerIcon()` `:1050` |

Both run inside `crl::async(...)` and deliver results via `crl::on_main(...)`.

`RefreshServersInfo()` `:1064` — fire-and-forget round over every non-Telegram
server. It **deliberately ignores** `rsaPublicKeyPem` and `dcId`; only
`name` / `description` / `logoPath` are applied (`ApplyFetchedIdentity()` `:422`).
Rationale in source: the endpoint is plain HTTP, so anything able to MITM it
must not be able to redirect the connection or swap the handshake key. A wrong
name or icon is merely wrong, not dangerous.

Called from `ServerSelectWidget::activate()` (`intro_server_select.cpp:405`).

`CheckServerOnline(server, done)` `:945` — a `QTcpSocket` reachability probe on a
background thread; reports latency in ms or `-1`. Distinct from the MTP-level check.

### 2.6 Mutation API

| Function | Location | Notes |
|---|---|---|
| `AddCustomServer(...)` | `:627` | Mints a fresh UUID. |
| `UpdateCustomServer(id, ...)` | `:661` | **Keeps the id**, so accounts stay bound. A delete+add would mint a new UUID and orphan accounts. Returns `std::nullopt` for an unknown id. |
| `RemoveCustomServer(id)` | `:720` | |
| `IsRemovableServer(server)` | `:714` | `false` for built-ins. |
| `CustomServersChanges()` | `:745` | `rpl::producer<>` over a static `rpl::event_stream<>`. Fired **only** from `WriteCustomServersJson()` `:108` — a single choke point. The file is explicitly `close()`d before firing so subscribers that synchronously re-read it see fresh content (Windows buffering bug, comment `:99-106`). |

---

## 3. MTP / protocol layer — the hard part

### 3.1 What changed upstream

Only **five** MTProto files differ from upstream:

| File | Change |
|---|---|
| `mtproto/mtproto_dc_options.{h,cpp}` | The core of the feature (below). |
| `mtproto/config_loader.cpp` | `refreshSpecialLoader()` early-exits when options are locked. |
| `mtproto/session_private.cpp` | `dcOptionsChanged()` hard-reconnects instead of migrating when locked. |
| `mtproto/details/mtproto_rsa_public_key.cpp` | `CreateRaw()` returns `nullptr` instead of asserting on an unrecognised PEM header. |
| `mtproto/mtp_instance.h` | Whitespace only. |

**Unchanged:** `mtproto_config.{h,cpp}`, `mtproto_auth_key.{h,cpp}`, `api.tl`
(byte-identical, layer 229). There is **no auth-key rewrite and no protocol
change**. The custom-key mechanism is entirely "put a different RSA public key
into the fingerprint-keyed map that `DcOptions::getDcRSAKey()` already reads".

### 3.2 `mtproto_dc_options` — new public API

```cpp
void setOptionsLocked(bool locked);
[[nodiscard]] bool optionsLocked() const;
void setBuiltInPublicKeys(bool telegram);
void setPublicKeysFromPem(const QString &pem);
```

* `setOptionsLocked()` `:420` — writes `_immutable`. Upstream used `_immutable`
  for the ".tdesktop-endpoints" static config; the fork **repurposes** it as
  "locked to a custom single server".
* `optionsLocked()` `:426` — reads `_immutable`.
* `constructAddOne()` `:412` — **early-returns when `_immutable`**, so
  `help.getConfig` can never overwrite a locked endpoint list.
* `setBuiltInPublicKeys(bool telegram)` `:231` — clears `_publicKeys` and
  repopulates from `kTelegramPublicRSAKeys` / `kTelegramTestPublicRSAKeys`
  (Telegram) or `kPublicRSAKeys` / `kTestPublicRSAKeys` (non-Telegram).
* `setPublicKeysFromPem(pem)` `:249` — clears `_publicKeys`, parses one PEM via
  `MTP::details::RSAPublicKey`, inserts it into `_publicKeys` keyed by
  `parsed.fingerprint()`.
* `readBuiltInPublicKeys()` `:212` — now starts with `_publicKeys.clear()`
  (upstream did not clear).

The fingerprint-keyed `_publicKeys` map is what `DcOptions::getDcRSAKey(dcId,
fingerprints)` returns; `DcKeyCreator::pqAnswered` consumes it for
`EncryptPQInnerRSA` and the `Req_DH_params` fingerprint.

### 3.3 `ApplyServerToDcOptions()` — how a server is actually applied

`owpengram_servers.cpp:357`:

```cpp
dcOptions->setOptionsLocked(false);
if (server.multiDc) {
    if (server.isTelegram)  dcOptions->constructFromBuiltIn();   // full 5-DC table
    else                    dcOptions->setFromList({ one MTP_dcOption }); // main DC only
    if (!server.rsaPublicKey.isEmpty()) dcOptions->setPublicKeysFromPem(server.rsaPublicKey);
    else                              dcOptions->setBuiltInPublicKeys(true);
    return;                                      // LEFT UNLOCKED
}
const auto flags = MTPDdcOption::Flag::f_static | MTPDdcOption::Flag::f_tcpo_only;
for (dcId = 1..kSingleServerDcs /*5*/)
    list.push_back(MTP_dcOption(flags, dcId, server.host, server.port, MTPbytes()));
if (!server.rsaPublicKey.isEmpty()) dcOptions->setPublicKeysFromPem(server.rsaPublicKey);
else                               dcOptions->setPublicKeysFromPem(kOfficialRsaPublicKey);
dcOptions->setFromList(list);
dcOptions->setOptionsLocked(true);                // LOCKED
```

Consequences:

* **Single-server backend:** every `dc_id` 1..5 resolves to the same
  `host:port`, so files and migrations that reference DC 2..5 still work.
  `f_tcpo_only` forces plain TCP. Options are **locked**, so `help.getConfig`
  cannot rewrite them.
* **Multi-DC custom server:** only the main DC is seeded; options stay
  **unlocked** so `help.getConfig` / the special loader can discover the real
  alternate DCs.
* **Single-server backends never fall back to Telegram's RSA keys** — they use
  `kOfficialRsaPublicKey`, because a Telegram fingerprint would never match.

`MainDcIdForServer()` `:220` -> `server.mainDcId` if `> 0`, else
`multiDc ? 2 : 1`.

### 3.4 Restore paths

`RestoreServerToConfig(account, config)` `:749` (called from `Main::Account::startMtp`
before the MTP instance exists) and `RestoreServerToAccount(account)` `:803`
(called right after the instance is created):

* No saved selection -> force-unlock `dcOptions()`, so a stale fallback config
  from a previous account cannot leak into a new one.
* Telegram server **and was locked** -> `constructFromBuiltIn()` (full reset).
  A locked config means DC 1/3/4/5 are poisoned with the old single-server
  address. This was the actual cause of "new account can't connect after a
  prior custom-server login".
* Telegram server **and not locked** -> `addFromList({DC2})`, keeping saved
  DC 3/4/5 so media downloads do not stall for 1-3 s waiting for `help.getConfig`.
* Non-Telegram -> full `ApplyServerToDcOptions()`.

`RestoreServerToAccount` then calls `mtp.setMainDcId(dcId)` and
`mtp.reInitConnection(dcId)`.

### 3.5 Other protocol-layer effects

* `mtproto/config_loader.cpp:118-128` — `ConfigLoader::refreshSpecialLoader()`
  resets `_specialLoader` and returns when `dcOptions().optionsLocked()`. The
  Telegram special CDN/DNS endpoint bootstrap is disabled for locked accounts.
* `mtproto/session_private.cpp:356-362` — `SessionPrivate::dcOptionsChanged()`
  does `destroyAllConnections(); connectToServer(false);` when locked (upstream:
  `connectToServer(true)`), i.e. a hard reconnect instead of a graceful migrate.

---

## 4. Account binding and identity

### 4.1 Per-account selection

`Telegram/SourceFiles/storage/storage_account.{h,cpp}`:

```cpp
struct OwpengramServerSelection { QString id; QString host; int port = 0; };   // storage_account.h:50
```

| Member | Location | Detail |
|---|---|---|
| `writeOwpengramServer(id, host, port)` | `storage_account.cpp:1224` | Encrypted file `owpengram_server` via `FileWriteDescriptor(u"owpengram_server"_q, _basePath, true)` + `EncryptedDescriptor`. `QDataStream`: `QString id`, `QString host`, `qint32 port`. |
| `readOwpengramServer()` | `:1274` | Reads the encrypted file; falls back to the plaintext one. |
| `writeOwpengramServerFallback()` | `:1241` | Plaintext JSON `{id, host, port}` at `owpengram_server.json`. |
| `readOwpengramServerFallback()` | `:1259` | |
| `collectGoodNames()` | `:283` | Added `"owpengram_servers"` so the shared file is not flagged as unknown. |

`Owpengram::ApplyServerToAccount()` (`owpengram_servers.cpp:895`):

```cpp
account->local().writeOwpengramServer(server.id, server.host, server.port);
account->resetAuthorizationKeysForServerSwitch();   // wipes auth keys for the old server
if (account->local().peekLegacyLocalKey()) account->local().writeMtpConfig();
```

`Main::Account::resetAuthorizationKeysForServerSwitch()` (`main_account.cpp:665`)
-> `resetAuthorizationKeys()`.

### 4.2 Identity scoping — same numeric user id on two servers

Three places compute a **server key** so `userId` alone is not an identity:

| Location | Form |
|---|---|
| `main_account.cpp:151` `Account::willHaveSessionUniqueId` | `Fnv1aHash(scopeKey) & 0xFFFF` XOR'd into bits **40-55** |
| `main_session.cpp:407` `Session::uniqueId` | same FNV-1a hash, same bits |
| `storage_domain.cpp:190` | `SessionKey = std::pair<uint64, QString>`, QString = `id + "\|" + host.toLower() + ":" + port` |
| `intro_step.cpp:200` | same `id\|host:port` string |

`Owpengram::ServerScopeKeyForAccount()` (`owpengram_servers.cpp:864`) returns
`server.host.toLower()` — **the host, not the id**: the same physical backend
added twice under different local ids must still share one scope. Empty for
Telegram accounts, so real Telegram `uniqueId()` is bit-for-bit unchanged.

The FNV-1a helper is duplicated verbatim in `main_account.cpp:41` and
`main_session.cpp:103` (offset basis `2166136261u`, prime `16777619u`). The two
copies **must** stay bit-identical or account identity breaks.

### 4.3 Account limits

`Telegram/SourceFiles/main/main_domain.{h,cpp}`:

```cpp
static constexpr auto kMaxAccounts = 3;            // Telegram free
static constexpr auto kPremiumMaxAccounts = 6;     // Telegram premium
static constexpr auto kMaxTotalAccounts = 50;      // fork-added, ALL servers combined
static bool AccountIsTelegram(not_null<Account*>); // -> CurrentServerForAccount(acc).isTelegram
[[nodiscard]] int telegramAccountsCount() const;   // only sessionExists() && AccountIsTelegram
```

* `maxAccounts()` `:519` counts only Telegram accounts for the premium bump.
* `add()` `:274` — **no longer clones the active account's MTP config**; always
  `std::make_unique<MTP::Config>(environment)`. Reason (comment `:275-278`):
  `fallbackProductionConfig()` may have been captured from a custom-server
  account and carries locked DC options plus custom RSA keys.
* `addActivated()` `:312` — gated by `kMaxTotalAccounts`, not `maxAccounts()`.
* `checkForLastProductionConfig()` `:442` — skips when
  `mtp->dcOptions().optionsLocked()`, so a self-hosted account cannot
  contaminate the production fallback.
* `closeAccountWindows()` `:375` — only steals the active slot if the account
  that logged out was the one showing.
* `maybeActivate()` `:458` — deliberately **no connection guard** (switching must
  be instant even if the server is offline).
* `Application::run()` (`core/application.cpp:406-436`) — the account-limit nag
  fires on `_domain->telegramAccountsCount() > kMaxAccounts` and ranks only
  Telegram accounts.

`Storage::Domain::startModern()` (`storage_domain.cpp:162-215`) — count limit
`kPremiumMaxAccounts` -> `kMaxTotalAccounts`; dedup set type
`flat_set<uint64>` -> `flat_set<std::pair<uint64, QString>>` keyed by
`(sessionId, "id|host:port")`.

### 4.4 Logout robustness

`Account::logOut()` (`main_account.cpp:567-580`) schedules
`base::call_delayed(kLogoutFallbackTimeout /*crl::time(2000)*/, this, ...)` ->
`loggedOut()` if `auth.logOut` never completes. Without it, an unreachable
server leaves the account stuck "logging out" forever.

---

## 5. URL scheme — `owpg://` (to become `srv://`)

### 5.1 Registration

`Telegram/SourceFiles/core/application.cpp:2336-2350`, inside
`Application::RegisterUrlScheme()`, a **second** `base::Platform::RegisterUrlScheme`
call:

```cpp
base::Platform::RegisterUrlScheme(base::Platform::UrlSchemeDescriptor{
    .executable = Platform::ExecutablePathForShortcuts(),
    .arguments = arguments,
    .protocol = u"owpg"_q,
    .protocolName = u"OwpenGram Link"_q,
    .shortAppName = u"tdesktop"_q,
    .longAppName = QCoreApplication::applicationName(),
    .displayAppName = AppName.utf16(),
    .displayAppDescription = AppName.utf16(),
});
```

`Telegram/lib_base/base/platform/**` is **unchanged** from upstream — the fork
reuses upstream's generic registration:

* **Windows** (`lib_base/base/platform/win/base_url_scheme_win.cpp`):
  `RegisterLegacy` -> HKCU `Software\Classes\owpg` with `URL Protocol`,
  `URL:OwpenGram Link`, `DefaultIcon`, `shell\open\command` =
  `"<exe>" <args> -- "%1"`. `RegisterDefaultProgram` ->
  `Software\Classes\tdesktop.owpg` + `Software\<longAppName>\Capabilities`
  (`RegisteredApplications`).
* **Linux** (`lib_base/base/platform/linux/base_url_scheme_linux.cpp`): GIO
  `x-scheme-handler/owpg` via `Gio::AppInfo::set_as_default_for_type(...)`
  against `QGuiApplication::desktopFileName()`.
* **macOS**: **not wired up** — `Telegram/Telegram.plist` has no
  `CFBundleURLTypes` entry for `owpg`.

### 5.2 Dispatch — `Application::openLocalUrl()` (`core/application.cpp:1265+`)

Three distinct `owpg://` shapes, all checked **before** the `tg://` block.

**(a) Command links** — regex `^owpg://(oauth|resolve)/?(\?.*)?$` (case-insensitive).
Converts to `tg://<cmd><query>` and dispatches on the current session via
`TryRouterForLocalUrl(controller, command)`, falling back to
`openCustomUrl("tg://", LocalUrlHandlers(), tgUrl, tgContext)`. The trailing
`/?` is optional because browsers normalise `scheme://host?query` ->
`scheme://host/?query` (comment `:1275-1281`). Bypasses the Telegram-only guard
— works on custom-server accounts.

* `owpg://oauth?token=...`
* `owpg://resolve?domain=...&startapp=...`

**(b) Add-server link** — regex `^owpg://addserver/?(\?.*)?$`. Reads `host` and
`port` from the query **only**. `rawQuery` is taken with `QUrl::FullyEncoded`
and `'+'` is rewritten to `"%20"` before `QUrlQuery` parsing (comment
`:1338-1346`: Go's `url.Values.Encode()` writes a literal space as `+`, which
`QUrlQuery` does not decode). Invalid -> `Ui::MakeInformBox(tr::lng_owpengram_server_link_invalid)`.
Valid -> `Box<AddServerBox>(done, server)` with only host+port pre-filled.

* `owpg://addserver?host=1.2.3.4&port=2398`

**Security rationale, quoted from the source (`:1310-1317`):** the link
deliberately carries *no* name/description/key/DC, because an attacker-supplied
key would be a MITM vector — the key that answers for a host:port at connect
time is the only thing ever trusted.

**(c) Host-routed links** — `owpg://<server-host>/<rest>` ->
`Application::openOwpengramUrl(url, context)` (`core/application.cpp:1400+`).

* Parses `body = url.mid(6)`, `slash = body.indexOf('/')`; `slash <= 0` -> false.
* `hostOf(account)` = `account->mtp().configValues().internalLinksDomain` with
  the `^https?://` prefix stripped and the trailing `/` chopped.
* Finds the **active** account whose host matches; otherwise the first matching
  account in `domain().orderedAccounts()`.
* No match -> `Ui::MakeInformBox(tr::lng_owpengram_owpg_other_server + "\n" + host)`.
* Match on a **different** account -> the account switch is deferred via
  `InvokeQueued(this, ...)` (comment `:1494-1498`: switching inside the click
  handler can use-after-free the originating window), then
  `_domain->maybeActivate(account)` and dispatch.
* Dispatch = `Core::TryConvertUrlToLocal(u"https://t.me/"_q + rest)` -> must
  start with `tg://` -> `TryRouterForLocalUrl(controller, command)` else
  `openCustomUrl("tg://", ...)`.

### 5.3 Telegram-only guard on `tg://`

`core/application.cpp:1352-1398`. The `tg://` branch now requires a Telegram account:

```cpp
isTgAccount(acc) = Owpengram::CurrentServerForAccount(acc).isTelegram;
```

If neither the context window nor the active window is a Telegram account, the
app scans `domain().orderedAccounts()` for a Telegram account and shows
`lng_owpengram_tg_link_switch_account` ("This link is for Telegram's official
network. Please switch to your Telegram account to open it.") or
`lng_owpengram_tg_link_no_account`.

### 5.4 Web -> `owpg://` conversion

`Telegram/SourceFiles/core/local_url_handlers.cpp`:

* `OwpengramAccountHost(account)` `:1932` — the bare link host (`me_url_prefix`
  without scheme/trailing slash) of a logged-in account; empty for
  `t.me` / `telegram.me` / `telegram.dog`. Comment `:1934-1941`: any
  non-official self-hosted server is trusted, not only ones advertising an
  `owpengram` app-config flag.
* `OwpengramHostForUrl(url)` `:1959` — returns that host if `url` matches
  `^(https?://)?(www\.)?<host>/.+$` for any logged-in account.
* `TryConvertUrlToLocal(url)` `:2011-2022` — a URL on a self-hosted host becomes
  `owpg://<host>/<rest>` **before** the `telegram.me|t.me` branch.

### 5.5 Other `owpg://` touch points

* `core/click_handler_types.cpp:279` — `HiddenUrlClickHandler::Open` also accepts `owpg://`.
* `core/click_handler_types.cpp:406` — `BotGameUrlClickHandler::onClick` treats
  `owpg://` like `tg://` (does not hijack the game's own URL).
* `core/ui_integration.cpp` — small fork-specific change.
* `settings/sections/settings_privacy_security.cpp:731-745` — the "login email"
  button no longer fires a fake `tg://settings/login_email` URL (which would hit
  the Telegram guard); it now navigates directly via
  `controller->showSettings(CloudLoginEmailId())`, because
  `account.sendVerifyEmailCode` / `verifyEmail` are real RPCs on any server.

---

## 6. Settings and storage

### 6.1 `Main::SessionSettings`

**Unchanged** — the diff for `main_session_settings.{h,cpp}` is empty.

### 6.2 `Core::Settings`

New public API (`core_settings.h:789-795`):

```cpp
[[nodiscard]] std::vector<RecentEmoji> recentEmojiForScope(const QString &scope) const;
void incrementRecentEmojiForScope(RecentEmojiId id, const QString &scope);
```

New private members (`core_settings.h:1158-1163`):

```cpp
mutable base::flat_map<QString, std::vector<RecentEmoji>> _recentEmojiDocumentsByScope;
mutable base::flat_set<QString> _recentEmojiDocumentsByScopeLoaded;
```

New file-backed store (`core_settings.cpp:119-190`):
`RecentEmojiScopeFilePath()` -> `cWorkingDir() + u"tdata/owpengram_recent_emoji.json"_q`;
`ReadRecentEmojiScopeFile(scope)` / `WriteRecentEmojiScopeFile(scope, list)` —
JSON object `{ "<scope>": [ {id, rating}, ... ] }`. **Deliberately outside
`serialize()` / `addFromSerialized()`** so it never breaks the versioned blob.

Scope key = `Owpengram::ServerScopeKeyForAccount(account)` = the server host,
lowercased (empty for Telegram -> the old unscoped path).

Consumers: `chat_helpers/emoji_list_widget.cpp:1912` (`recentEmojiForScope`) and
`:3087` (`incrementRecentEmojiForScope`).

Unrelated change bundled in: `core_settings.h:1178` —
`rpl::variable<bool> _systemDarkModeEnabled` default flipped `true` -> `false`.

---

## 7. UI surfaces

### 7.1 New files

```
Telegram\SourceFiles\owpengram\owpengram_servers.h              (203 lines)
Telegram\SourceFiles\owpengram\owpengram_servers.cpp            (1144 lines)
Telegram\SourceFiles\boxes\owpengram_add_server_box.h           (70)  / .cpp (581)
Telegram\SourceFiles\boxes\owpengram_server_details_box.h      (40)  / .cpp (466)
Telegram\SourceFiles\boxes\owpengram_connecting_box.h           (47)  / .cpp (96)
Telegram\SourceFiles\intro\intro_server_select.h                (64)  / .cpp (635)
Telegram\SourceFiles\core\branding.h                            (50)
```

### 7.2 `Intro\details\ServerSelectWidget` — `intro/intro_server_select.{h,cpp}` (635 lines)

* `class ServerRow final : public Ui::RippleButton` (`:44`) — logo, name, official
  badge, endpoint line `tr::lng_owpengram_server_endpoint_short`
  (`{host} · Port {port}`), status dot, latency (`{latency} ms`), `Join` button.
* `ServerSelectWidget` (`:310`) — panel + `Ui::ScrollArea` + rows container;
  title `lng_owpengram_server_selection_title` ("Server Selection"), subtitle
  `lng_owpengram_server_selection_subtitle` ("Choose a server or add your own"),
  `+ Add Server` button.
* `rebuildList()` `:445` — iterates `Owpengram::ListServers()`; per row a context
  menu with **Edit** (only if `IsRemovableServer`) and **Info**
  (`Box<ServerDetailsBox>`); subscribes to `Owpengram::CustomServersChanges()`
  `:372` to rebuild live.
* `activate()` `:383` — starts the status timer and calls `Owpengram::RefreshServersInfo()`.
* `joinServer()` `:503` — validates, enforces the Telegram limit (only for a
  Telegram server), `CheckServerOnline()`, and on offline shows
  `lng_owpengram_server_offline_confirm` ("This server appears to be offline. Join anyway?").
* `proceedJoin()` `:543` — `Owpengram::ApplyServerToAccount(&account(), server)`,
  shows `Box<ConnectingBox>`, and races it against `Owpengram::WaitForServerConnection(...)`.

### 7.3 Boxes

* **`owpengram_add_server_box.{h,cpp}`** (581 lines) —
  `class AddServerBox : public Ui::BoxContent`, ctor
  `(QWidget*, Fn<void(Owpengram::Server)> done, Owpengram::Server existing = {})`;
  fields: name, description, address (`host:port`), RSA PEM, main-DC;
  `RadioTypeRow` (`:167`) with literal radio labels `u"Single server"_q` (index 0)
  and `u"Multi-DC (Telegram)"_q` (index 2); `ParseAddress()` `:204`;
  `fetchPublicKeyForAddress()` `:456` (debounced via `_fetchDebounce`) calls
  `FetchServerInfo` + `FetchServerIcon` and auto-fills name/description/DC/PEM/icon;
  `save()` uses `AddCustomServer` / `UpdateCustomServer`; RSA validated with
  `IsValidRsaPublicKeyPem` (`:546`).
* **`owpengram_server_details_box.{h,cpp}`** (466 lines) —
  `class ServerDetailsBox : public Ui::BoxContent`, ctor
  `(QWidget*, Owpengram::Server, Fn<void(const Owpengram::Server&)> connect,
  Fn<void()> removed = nullptr, Fn<void()> edit = nullptr)`; shows IP/port/description/
  connection status; **Delete** -> `Owpengram::AccountsUsingServer(_server.id)` (`:407`)
  -> warns with `lng_owpengram_server_delete_confirm_with_accounts` ->
  logs out those accounts (`account->logOut()`, `:428`) ->
  `Owpengram::RemoveCustomServer(_server.id)` (`:430`).
* **`owpengram_connecting_box.{h,cpp}`** — `class ConnectingBox : public Ui::BoxContent`,
  ctor `(QWidget*, Fn<void()> onCancel)`; Escape/outside-click disabled;
  `showFailed()` relabels the button to "Close"; animated dots timer.

### 7.4 Intro flow

* `intro_widget.h:38-44` — `enum class EnterPoint : uchar { Start, Phone, Qr, ServerSelect };`
  moved into `namespace Intro`.
* `intro_widget.h:84-89` — `Data::enterPoint` + `Data::requestNearestDc` (`Fn<void()>`).
* `intro_widget.cpp:104-105` — `getData()->enterPoint = point; getData()->requestNearestDc = [=] { getNearestDC(); };`
* `intro_widget.cpp:116-127` — `EnterPoint::Start` no longer calls `getNearestDC()`;
  `EnterPoint::Qr` now shows `StartWidget` (not `QrWidget`);
  `EnterPoint::ServerSelect` -> `ServerSelectWidget`.
* `intro_start.cpp:38` — `submit()` -> `goNext<ServerSelectWidget>()` (was `goNext<QrWidget>()`).
* `window_controller.cpp:409` — `setupIntro` picks `EnterPoint::ServerSelect`
  instead of `EnterPoint::Qr` when an authed account exists.
* `intro_step.cpp:198-215` — account dedup also compares the server key
  (`id|host:port`); the same userId on a different server is **not** a duplicate.
* `intro_step.cpp:492-508` — `paintCover` paints `QImage(Branding::LogoPath.utf16())`
  scaled to 150 px instead of the animated plane (`st::introCoverIcon`).

### 7.5 Account switcher — `settings/sections/settings_information.cpp`

`AccountsList::rebuild()` `:1118`:

* Builds `std::vector<std::pair<not_null<Main::Account*>, Owpengram::Server>>`
  from `domain().orderedAccounts()` where `sessionExists()`.
* `std::stable_sort` — **Telegram group first**
  (`a.second.isTelegram != b.second.isTelegram`), then by `server.name`.
* Emits `Ui::AddSubsectionTitle(inner, rpl::single(server.name))` whenever
  `server.id` changes -> one header per server.
* **Drag-reorder removed**: `Ui::VerticalLayoutReorder` deleted (include +
  `_reorder` member + `_reordering` counter + `setAccountsOrder` write-back).
  Reason (comment `:1122-1123`): manual order conflicts with server grouping.
* `_watched` changed from `flat_map<Account*, unique_qptr<SettingsButton>>` ->
  `flat_set<Account*>`; buttons are owned solely by the inner layout.
* The grouped list is shown only when `withServers.size() > 1`.
* `setupAdd()` `:1067` — the add button is blocked only at `kMaxTotalAccounts`
  (the server is not known until the intro picks it).

### 7.6 Settings -> Help section — `settings/sections/settings_main.cpp:576-634`

* Telegram FAQ / Features / Ask-a-Question buttons render **only** when
  `Main::Domain::AccountIsTelegram(&session->account())`.
* New separate block: `id = u"main/owpengram-github"_q`, title `u"OwpenGram"_q`,
  icon `st::menuIconGithub` (new `Telegram/Resources/icons/menu/github.svg`, new
  style entry `menuIconGithub` in `Telegram/SourceFiles/ui/menu_icons.style:130`),
  onclick -> `UrlClickHandler::Open(u"https://github.com/owpengram/owpengram-server"_q)`.
* `fillTopBarMenu` `:679` gate -> `kMaxTotalAccounts`.
* Username placeholder uses `session->createInternalLink(u"username"_q)` instead
  of the hardcoded `"t.me/username"` (`:640-648`).

### 7.7 Main menu server footer — `window/window_main_menu.{h,cpp}`

* New `not_null<Ui::FlatLabel*> _server;` + `setupServerFooter()` / `updateServerFooter()`.
* `updateServerFooter()` `:814` — text = `server.name + "\n" + host + ":" + port`,
  marked as link index `1`.
* `setupServerFooter()` `:803` — link 1 opens `Box<ServerDetailsBox>(server, nullptr, nullptr)`.
* Layout rewritten to a `layoutFooter` lambda driven by `_footer->sizeValue()`;
  `st::mainMenuServerSkip: 2px`, `st::mainMenuServerLabel` (new),
  `mainMenuFooterHeightMin: 80px -> 114px`.

---

## 8. Branding, build and packaging integration

### 8.1 `core/branding.h` (new)

```cpp
namespace Branding {
constexpr auto AppName          = "OwpenGram Desktop"_cs;
constexpr auto ShortAppName     = "OwpenGram"_cs;
constexpr auto ApplicationName  = "OwpengramDesktop"_cs;
constexpr auto CompanyName      = "OwpenGram"_cs;
constexpr auto WindowsIconPath  = "Resources/OwpenGram/art/icon256[_debug].ico"_cs;
constexpr auto MacOSIconName    = "Icon.icns"_cs;
constexpr auto LinuxIconBase    = "Resources/OwpenGram/art/icon"_cs;
constexpr auto LogoPath         = ":/gui/art/logo_256[_debug].png"_cs;
constexpr auto LogoNoMarginPath = ":/gui/art/logo_256[_debug].png"_cs;
constexpr auto PlaneWhitePath   = ":/gui/plane_white.svg"_cs;
constexpr auto BusinessLogoPath = ":/gui/art/business_logo.png"_cs;
constexpr auto AffiliateLogoPath= ":/gui/art/affiliate_logo.png"_cs;
constexpr auto TrayMonochromePath / TrayMonochromeAttentionPath / TrayMonochromeMutePath
constexpr auto BundleIdentifier  = "org.owpengram.desktop"_cs;
constexpr auto LinuxAppId       = "org.owpengram.desktop[.Debug]"_cs;
}
```

Consumers: `core/version.h:24` (`AppName = Branding::AppName`), `main_window.cpp`
(`Logo()`, `LogoNoMargin()`, `updateTitle()`), `about_box.cpp`,
`notifications_manager_default.cpp`, `settings_notifications.cpp`,
`api_authorizations.cpp` (app name sent to the server),
`data_message_reactions.cpp` (`Branding::ShortAppName + " Star"`),
`data_session.cpp` (service-notification avatar name), `intro_step.cpp` (cover logo),
`intro_start.cpp` (title).

### 8.2 `Telegram/CMakeLists.txt`

* New source entries: the three `boxes/owpengram_*`, `intro/intro_server_select`,
  `owpengram/owpengram_servers`, `core/email_signup_phone`, `intro/intro_email_signup`.
* Bundle identifiers: `org.telegram.desktop` -> `org.owpengram.desktop`;
  macOS `com.tdesktop.Telegram` -> `com.owpengram.OwpenGram` (+ `.Debug` for Xcode);
  mac-store output `"OwpenGram Lite"`.
* Output name: `Telegram` -> `OwpenGram`; `G_LOG_DOMAIN` `"Telegram"` -> `"OwpenGram"`.
* Linux install block rewritten: `linux_app_id` = `org.owpengram.desktop` /
  `org.owpengram.desktop.Debug` (Debug), `linux_icon_suffix` = `_debug`; icons from
  `Resources/OwpenGram/art/iconNN{suffix}.png` renamed to `${linux_app_id}.png`;
  the `.desktop` file is generated then string-replaced; `org.telegram.desktop.*`
  -> `org.owpengram.desktop.*`.
* `set(branding_app_name ...)` etc. (`:27-31`) — **declared but never consumed**;
  `core/branding.h` hard-codes the same strings.

### 8.3 Other build files

* `Telegram/cmake/td_export.cmake:47` — added `tdesktop::td_mtproto` to
  `target_link_libraries(td_export PUBLIC ...)`.
* `Telegram/cmake/td_mtproto.cmake` — +2 lines (same commit).
* Top-level `CMakeLists.txt:9-21` — `cmake_policy(SET CMP0156/CMP0179/CMP0218 OLD)`.
* `Telegram/build/prepare/prepare.py` — libwebp `nmake /f Makefile.vc` for
  debug+release; libvpx/ffmpeg on Windows get `VCToolsInstallDir\bin\Hostx64\x64`
  on `PATH`; libvpx/ffmpeg use `bash ../patches/build_libvpx_win.sh`.
* `Telegram/build/docker/centos_env/Dockerfile` — `gcc-toolset-15` -> `gcc-toolset-14`;
  linker-flag injection moved from `CMAKE_${lang}_LINKER_FLAGS_DEBUG_INIT` to
  `CMAKE_${lang}_LINKER_FLAGS_INIT`.
* `Telegram/cmake/telegram_options.cmake` (on the fork's `dev` branch) — new
  `OWPENGRAM_BUILD` cache variable, plumbed as
  `target_compile_definitions(Telegram PRIVATE OWPENGRAM_BUILD=${OWPENGRAM_BUILD})`.

### 8.4 Resources

* `Telegram/Resources/OwpenGram/` — new art tree (34 art files: `icon{16..512}[@2x][_debug].png`,
  `icon256[_debug].ico`, `logo_256.png`, `logo_256_no_margin.png`,
  `telegram_logo_256.png`, `business_logo.png`, `affiliate_logo.png`) plus `icons/`
  (logos 500-1024, `plane_white.svg`, `tray_monochrome*.svg`) and `generate_icons.py`.
* `Telegram/Resources/qrc/telegram/telegram.qrc` — art/icons re-pointed at
  `OwpenGram/`; new aliases `art/logo_256_debug.png`, `art/telegram_logo_256.png`,
  `art/logo_qr_big.png`; `misc/org.telegram.desktop.desktop` -> `misc/org.owpengram.desktop.desktop`.
* `Telegram/Resources/qrc/telegram/sounds.qrc` — new `call_incoming_owpengram.mp3`,
  `call_incoming_owpengram_old_1.mp3`, `msg_incoming_owpengram.mp3`.
* `Telegram/Resources/winrc/Telegram.rc` — `IDI_ICON1` -> `..\OwpenGram\art\icon256[_debug].ico`;
  `CompanyName` = `OwpenGram`, `FileDescription`/`ProductName` = `OwpenGram Desktop`.
* `Telegram/Telegram.plist` — `CFBundleGetInfoString` = `OwpenGram Desktop messaging app`.
* `Telegram/Resources/icons/menu/github.svg` — new.

### 8.5 Linux desktop integration

* `lib/xdg/org.telegram.desktop.desktop` -> `org.owpengram.desktop.desktop` (rename).
  Content: `Name=OwpenGram`, `Comment=New era of messaging`, `TryExec=OwpenGram`,
  `Exec=OwpenGram -- %U`, `Icon=org.owpengram.desktop`,
  `StartupWMClass=org.owpengram.desktop`, `DBusActivatable=true`,
  `SingleMainWindow=true`,
  `MimeType=x-scheme-handler/tg;x-scheme-handler/tonsite;`, `Keywords=...;owpengram;`,
  `[Desktop Action quit] Exec=OwpenGram -quit`.
  **`x-scheme-handler/owpg` is NOT in `MimeType`** — see section 10.
* `lib/xdg/org.telegram.desktop.service` -> `org.owpengram.desktop.service`:
  `[D-BUS Service] Name=org.owpengram.desktop / Exec=@CMAKE_INSTALL_FULL_BINDIR@/OwpenGram`.
* `lib/xdg/org.telegram.desktop.metainfo.xml` -> `org.owpengram.desktop.metainfo.xml`
  (renamed, 12 lines changed — the AppStream metadata still points at
  desktop.telegram.org and tdesktop.com).

### 8.6 `.gitmodules`

**Unchanged** from upstream `v7.2.2`. A `Telegram/ThirdParty/TooManyCooks` entry
was added by `4c02cd94f1` and reverted by `1fcb5fa194`.

---

## 9. Localization

All new keys are prefixed `lng_owpengram_` — **47 keys added**, 76 existing keys
edited (Telegram -> OwpenGram).

### 9.1 Added keys

```
lng_owpengram_server_selection_title              = "Server Selection"
lng_owpengram_server_selection_subtitle           = "Choose a server or add your own"
lng_owpengram_server_add                          = "+ Add Server"
lng_owpengram_server_endpoint_short               = "{host} · Port {port}"
lng_owpengram_server_official_badge               = "official"
lng_owpengram_server_latency_short                = "{latency} ms"
lng_owpengram_server_join                         = "Join"
lng_owpengram_server_online                       = "Online"
lng_owpengram_server_offline                      = "Offline"
lng_owpengram_server_ip_line                      = "IP: {host}"
lng_owpengram_server_port_line                    = "Port: {port}"
lng_owpengram_server_host_hint                    = "Address:port (e.g. 1.2.3.4:2398)"
lng_owpengram_server_telegram_name                = "Telegram"
lng_owpengram_server_telegram_description         = "Official Telegram cloud. Sign in with your Telegram account."
lng_owpengram_server_official_name                = "OwpenGram"
lng_owpengram_server_official_description         = "This is a test OwpenGram server."
lng_owpengram_server_add_title                    = "Add Server"
lng_owpengram_server_edit_title                   = "Edit Server"
lng_owpengram_server_logo                         = "Server icon (optional)"
lng_owpengram_server_logo_choose                  = "Choose image"
lng_owpengram_server_logo_invalid                 = "Could not load the selected image."
lng_owpengram_server_name                         = "Server name"
lng_owpengram_server_host                         = "IP address"
lng_owpengram_server_port                         = "Port"
lng_owpengram_server_rsa_key                      = "RSA public key (optional)"
lng_owpengram_server_rsa_key_hint                 = "PEM format. Leave empty to use the default OwpenGram key."
lng_owpengram_server_rsa_key_invalid              = "Invalid RSA public key. Use PEM format (BEGIN PUBLIC KEY or BEGIN RSA PUBLIC KEY)."
lng_owpengram_server_description                  = "Description"
lng_owpengram_server_endpoint                     = "IP: {host}  Port: {port}"
lng_owpengram_server_offline_confirm              = "This server appears to be offline. Join anyway?"
lng_owpengram_server_invalid                      = "Please fill in a valid name, IP address and port."
lng_owpengram_server_link_invalid                 = "This server link is missing or has an invalid name, address, port, or public key."
lng_owpengram_server_added                        = "Server added. You can now log in to it from Add Account."
lng_owpengram_server_latency                      = "latency: {latency}ms"
lng_owpengram_server_details_host                 = "IP address"
lng_owpengram_server_details_port                 = "Port"
lng_owpengram_server_details_title                = "Server Info"
lng_owpengram_server_details_description          = "Description"
lng_owpengram_server_details_status                = "Connection status"
lng_owpengram_server_delete                       = "Delete"
lng_owpengram_server_delete_confirm               = "Remove server \"{name}\" from the list?"
lng_owpengram_server_delete_confirm_with_accounts#one   = "Remove server \"{name}\" from the list? This will log out {count} account signed in to it."
lng_owpengram_server_delete_confirm_with_accounts#other = "Remove server \"{name}\" from the list? This will log out {count} accounts signed in to it."
lng_owpengram_server_checking                     = "Checking…"
lng_owpengram_server_page_prev                    = "◀"
lng_owpengram_server_page_next                    = "▶"
lng_owpengram_server_page                         = "{current} / {total}"
lng_owpengram_tg_link_no_account                  = "This link is for Telegram's official network. Please log in to a Telegram account in this app to open it."
lng_owpengram_tg_link_switch_account              = "This link is for Telegram's official network. Please switch to your Telegram account to open it."
lng_owpengram_owpg_other_server                   = "This link belongs to a server you're not connected to:"
```

Note: `lng_owpengram_server_delete_confirm_with_accounts#one` says
"**{count} account**" without a distinct singular form — the count placeholder
is present in both forms.

### 9.2 Existing keys edited (Telegram -> OwpenGram) — 76 total

`lng_open_from_tray`, `lng_quit_from_tray`, `lng_tray_icon_text`,
`lng_error_start_minimized_passcoded`, `lng_proxy_unsupported`, `lng_intro_about`,
`lng_intro_qr_title`, `lng_intro_qr_step1`, `lng_code_from_telegram`,
`lng_code_call`, `lng_code_calling`, `lng_code_called`, `lng_bad_photo`,
`lng_bad_video`, `lng_signin_sure_reset`, `lng_no_conversations_subtitle`,
`lng_update_telegram`, `lng_username_description1`, `lng_settings_events_joined`,
`lng_sure_save_language`, `lng_settings_auto_start`,
`lng_settings_auto_start_disabled_uwp`, `lng_settings_add_sendto`,
`lng_settings_use_winhello_about`, `lng_settings_use_touchid_about`,
`lng_settings_use_applewatch_about`, `lng_settings_use_systempwd_about`,
`lng_settings_username_about`, `lng_settings_peer_to_peer_about`,
`lng_settings_logged_in_title`, `lng_settings_logged_in_description`,
`lng_settings_disconnect_all_sure`, `lng_settings_suggestion_phone_number_about`,
`lng_settings_suggestion_phone_number_change`, `lng_settings_cloud_password_start_about`,
`lng_settings_cloud_login_email_box_about`, `lng_settings_cloud_login_email_about`,
`lng_settings_color_link_name`, `lng_theme_no_desktop`,
`lng_download_path_default_radio`, `lng_passcode_about`, `lng_passcode_about2`,
`lng_passcode_about3`, `lng_passcode_winhello_unlock`,
`lng_cloud_password_create`, `lng_cloud_password_about_recover`,
`lng_cloud_password_passport_losing`, `lng_proxy_about`, `lng_proxy_sponsor_about`,
`lng_proxy_sponsor_warning`, `lng_proxy_web_warning`, `lng_settings_export_data`,
`lng_settings_ask_sure`, `lng_settings_passkeys_unsigned_error`,
`lng_sessions_other_desc`, `lng_sessions_about_apps`, `lng_blocked_list_confirm_text`,
`lng_edit_privacy_phone_number_warning`, `lng_edit_privacy_phone_number_contacts`,
`lng_change_phone_error`, `lng_profile_suggest_sure`, `lng_profile_accept_photo_sure`,
`lng_profile_accept_video_sure`, `lng_manage_peer_antispam_about`,
`lng_star_ref_launch_about`, `lng_star_ref_created_text`,
`lng_action_gift_premium_about`, `lng_bot_share_location_unavailable`,
`lng_about_text3_faq`, `lng_new_version_wrap`, `lng_group_call_mac_access`,
`lng_group_call_mac_screencast_access`, `lng_terms_delete_warning`,
`lng_language_not_ready_about`, `lng_outdated_soon`, `lng_outdated_now`.

---

## 10. Known defects and technical debt in the source fork

These are properties of the **owpengram** implementation. A port should decide
for each whether to reproduce, fix, or drop it.

| # | Issue | Location |
|---|---|---|
| 1 | **Dead commented-out block** `// patch by onysd` — an entire alternative DC table + RSA key set inside the anonymous namespace, ~50 lines | `mtproto/mtproto_dc_options.cpp:31-81` |
| 2 | **Duplicated RSA key arrays** — `kTelegramPublicRSAKeys` / `kTelegramTestPublicRSAKeys` are byte-identical copies of `kPublicRSAKeys` / `kTestPublicRSAKeys` | `mtproto/mtproto_dc_options.cpp:128-149` |
| 3 | **`_immutable` flag repurposed** — upstream used it for the ".tdesktop-endpoints" static-config semantics; the fork reuses it as "locked to a custom server". Upstream semantics for `_immutable` are lost. | `mtproto/mtproto_dc_options.cpp:420` |
| 4 | **Plain-HTTP (no TLS) discovery endpoints.** `/owpengram/server-info` and `/owpengram/server-icon` are fetched over raw TCP with no authentication. `RefreshServersInfo` mitigates this, but `FetchServerInfo` (used by *Add Server*) **does** trust the returned `rsa_public_key_pem` and `dc_id` — the MITM rationale in `application.cpp` protects only the `owpg://addserver` link, not the manual Add-Server path. | `owpengram_servers.cpp:972-1062`; `boxes/owpengram_add_server_box.cpp:456` |
| 5 | **Hardcoded official endpoint + key** — `152.89.254.50:2398` and the RSA PEM are compile-time constants; changing them requires a rebuild. | `owpengram_servers.cpp:50-62` |
| 6 | **`RefreshServersInfo` uses non-atomic statics** — `static auto running` / `static auto remaining` mutated from main-thread callbacks. Safe only because all callbacks land on the main thread (documented at `:1065-1069`). | `owpengram_servers.cpp:1070-1090` |
| 7 | **Custom-server list stored as plaintext JSON** with the RSA public key in the clear (`tdata/owpengram_servers.json`). Fine for a public key, but the file is user-writable and thus a local trust anchor. | `owpengram_servers.cpp:64-66` |
| 8 | **Per-account server selection has a plaintext fallback** (`owpengram_server.json`) written unconditionally, even when the encrypted file succeeds — the selection is always recoverable in the clear. | `storage/storage_account.cpp:1224-1290` |
| 9 | **`_systemDarkModeEnabled` default flipped `true` -> `false`** — an unrelated behaviour change bundled into the fork. | `core/core_settings.h:1178` |
| 10 | **Account drag-reorder removed** from the account switcher (a feature regression vs upstream) to make server grouping work. | `settings/sections/settings_information.cpp:1118+` |
| 11 | **`Domain::add()` no longer reuses the active account's MTP config** — every new account starts from a bare `MTP::Config`. | `main/main_domain.cpp:274-290` |
| 12 | **`branding_*` CMake variables are declared but never used** — `core/branding.h` hard-codes the same strings. | `Telegram/CMakeLists.txt:27-31` |
| 13 | **`snap/snapcraft.yaml` was never updated** — still references `telegram-desktop` / `org.telegram.desktop` / `Telegram/Resources/art/icon512@2x.png` / `usr/share/metainfo/org.telegram.desktop.metainfo.xml`, none of which exist after the rename. The snap is broken. | `snap/snapcraft.yaml` |
| 14 | **`x-scheme-handler/owpg` missing from the installed `.desktop` file's `MimeType`** — only `tg` and `tonsite` are declared, so `owpg://` links do not resolve through the installed desktop entry on Linux. | `lib/xdg/org.owpengram.desktop.desktop` |
| 15 | **macOS URL-scheme registration missing** — `Telegram.plist` has no `CFBundleURLTypes` entry for `owpg`. | `Telegram/Telegram.plist` |
| 16 | **`RefreshServersInfo` can never move a server or change its key**, by design — an operator who rotates the key server-side cannot update a *custom* server entry from the UI without editing it manually. | `owpengram_servers.cpp:1105-1107` |
| 17 | **`owpg://addserver` link carries only host+port** — a deliberate, documented MITM-rationale choice, but it means every recipient must fetch the key from the server itself. | `core/application.cpp:1310-1345` |
| 18 | **`ShouldUseCloudLangPack()` always returns `true`** — the gate added in `api_updates.cpp` and `lang_cloud_manager.cpp` is a no-op stub. | `owpengram_servers.cpp:872` |
| 19 | **Duplicated FNV-1a helper** in `main_account.cpp` and `main_session.cpp` — the two copies must stay bit-identical or account identity breaks. | `main/main_account.cpp:41`, `main/main_session.cpp:103` |
| 20 | **`api.tl` / `mtp_user_normalize` were patched then reverted** — `004538c665` added `mtp_user_normalize.{h,cpp}` and an `api.tl` line; the final tree has neither and `api.tl` is identical to upstream. | history only |

---

## 11. Adjacent fork features (shipped in the same tree, not multi-server)

* **Email signup** — new `core/email_signup_phone.{h,cpp}` +
  `intro/intro_email_signup.{h,cpp}`, plus changes in `intro_code`, `intro_phone`,
  `intro_qr`, `intro_signup` and `Intro::Data::emailSignupPhoneFallbackChosen`.
* **Per-server upload size limits** — `storage/localimageloader.h` gains
  `[[nodiscard]] int64 FileSizeLimit();` / `FileSizePremiumLimit()` computing
  `Data::PremiumLimits::uploadMaxDefault() / uploadMaxPremium() * 512 * 1024`
  from the *active account's* app config (a self-hosted server can cap uploads
  below 2/4 GB). `storage_media_prepare.cpp` and `send_files_box.cpp` use them.
  `boxes/premium_limits_box.cpp:1018` adds an `exactGb` guard because the old
  floor-division rounded a non-round limit to 0 and produced a NaN geometry
  crash in `AddBubbleRow`.
* **Per-server cloud lang packs** — `lang/lang_cloud_manager.{h,cpp}` gains
  `_lastServerScope` (`std::optional<QString>`) and
  `requestLangPackDifference(Pack, bool force = false)`; when the active
  account's server scope changes, a **full** `langpack.getLangPack` pull
  (version forced to 0) is issued for both `Pack::Current` and `Pack::Base`
  instead of `langpack.getDifference`.
* **`api/api_updates.cpp:2818-2835`** gates `mtpc_updateLangPack` and
  `mtpc_updateLangPackTooLong` on `Owpengram::ShouldUseCloudLangPack()`.
* **`api/api_updates.cpp:1766-1786`** — `mtpc_updateMessageReactions` now looks
  the item up **first** via `session().data().message(peer, msg_id)` and only
  falls back to the `historyLoaded()` unread-counter path.
* **`data/data_peer.cpp:439`** — removed the `isNotificationsUser()` fallback
  that returned `Window::LogoNoMargin()`.

---

## 12. Follow-up fix commit index (fork-only commits)

Useful for understanding *which* parts of the design were found fragile after
the fact.

### Architecture / core

| Commit | Title | What it fixed |
|---|---|---|
| `c5eb1312ea` | changed multi-server architecture | Reworked `ApplyServerToDcOptions` + `mtproto_dc_options` (adds `setOptionsLocked`, `setBuiltInPublicKeys`, `setPublicKeysFromPem`); reshaped `AddServerBox`; touched `mtp_instance.h`. |
| `447a64fe7a` | fix for connections betweet three default servers | Fixed DC-option handling across the built-in servers; added `Account::resetAuthorizationKeysForServerSwitch`. |
| `004538c665` | working on compability with MyTelegram and forks | Added `mtp_user_normalize.{h,cpp}`, `api.tl` tweak, `td_export.cmake` -> `td_mtproto`. **Later reverted.** |
| `690414c855` | fix server load per account at startup | Made the saved server selection apply per account at startup (`RestoreServerToConfig` / `RestoreServerToAccount`). |
| `24abff75e8` | fix for server per account init | Added the per-server footer in the main menu + `Account`/`Storage` binding; `window.style` tweaks. |
| `50921ef12a` | finished server pick mvp | Completed the server-select screen; simplified `ServerDetailsBox`. |
| `01f39157e9` / `420ad3e38c` / `ab18da918f` | working server list / fixes for server info modal | Iterations on the server list and the info modal. |
| `b6e337b4de` | first step in server selection | First cut of the server picker. |
| `513e3112ae` | changing server selection logic | Reworked the selection flow. |
| `4be579281e` | fix for fetching server info and server edit | Fixed the `/owpengram/server-info` fetch + the edit path. |
| `6414cd0db2` | added auto fetch for server dc and key | Added `FetchServerInfo` / `FetchServerIcon` and auto-fill in `AddServerBox`. |
| `0b4b5316d5` | added ability to change default owpengram server info from server endpoint | Post-merge: +368 lines — `RefreshServersInfo`, `ApplyFetchedIdentity`, built-in identity store, `ServerInfoFetchResult`. |
| `8e76fc712f` | updated addserver link logic | Reworked `owpg://addserver` handling + `ConnectingBox`. |
| `1552ff6f3a` | removing all server accounts on server deletion | `AccountsUsingServer` + log-out-then-delete in `ServerDetailsBox`. |
| `1aebb475f2` | added edit button for server info modal window | Edit affordance in the details modal. |
| `37bc895d57` | fixed drawning of custom server icon in client | Server logo rendering. |
| `6e1d6424fa` | fix for server with no logo | Missing-logo fallback. |
| `f0df396b2d` | fix for description field | Description field handling. |

### Account identity / dedup

| Commit | Title | What it fixed |
|---|---|---|
| `094f6056a5` | fix for account ids on different servers | Added the FNV-1a server-scope XOR to `Account::willHaveSessionUniqueId` and `Session::uniqueId` (bits 40-55). |
| `b6b0d187cc` | fix for same account id on different servers | `intro_step.cpp`: added the `id\|host:port` server key to the "already authorized" check. |
| `3a6dfc1916` | fix account deduplication | `storage_domain.cpp` + `intro_phone.cpp`: dedup key became `(sessionId, serverKey)`. |
| `5efe23d9c7` | accounts grouped by server now | Grouped the account switcher by server; removed drag-reorder. |
| `bfe13ac877` | fix for non-telegram servers | `settings_main.cpp`: hide Telegram help links on custom servers. |
| `35d6b885fa` | fixes for t.me on other servers accounts | `owpg://` host routing + the Telegram-only guard on `tg://`. |
| `583487bec6` | fixed link gen for channels | Added `OwpengramAccountHost` / `OwpengramHostForUrl` / `TryConvertUrlToLocal` conversion + `click_handler_types.cpp`. |
| `4c02cd94f1` | added fix for owpg protocol | First `owpg://` support in `application.cpp`. |
| `7861204df3` | fix for devices in session list | `api_authorizations.cpp`: app name for the device list. |
| `33f34906f2` | fix for reactions on telegram server | `owpengram_servers.cpp`: reaction-update path for Telegram accounts. |
| `620cf361b8` | fix for messages updates | Update-feed fixes. |

### Per-server local state

| Commit | Title | What it fixed |
|---|---|---|
| `0ab6504982` | Scope recent custom emoji cache per backend server | Added `Core::Settings::recentEmojiForScope` / `incrementRecentEmojiForScope` + `tdata/owpengram_recent_emoji.json` + `ServerScopeKeyForAccount`. |
| `f50f7560b0` | lang pack now per-server | `_lastServerScope` + forced full lang-pack pull on server change. |
| `64a81baa67` | fix for lang pack | Lang-pack follow-up fix. |
| `21e1750df4` | updates for server files size limits | `FileSizeLimit()` / `FileSizePremiumLimit()` from the active account's app config. |

### UX / UI

| Commit | Title | What it fixed |
|---|---|---|
| `b5b1cf1a17` | added "connecting to server" modal to prevent user input on server connection | Added `ConnectingBox`. |
| `0b7fc958b6` | removed connecting modal | **Reverted** the modal — removed 64 lines from `main_domain.cpp`. |
| `a8404f5278` | updated max accounts count | `kMaxTotalAccounts = 50`. |
| `835885098e` | Remove Teamgram from the server selection list | Removed the Teamgram built-in. |
| `a817d2ee3a` / `a4fa41a3c8` / `abed5d5aa4` | added public teamgram server / fix for teamgram public server / changed teamgram logo | The Teamgram built-in, later removed. |
| `8a18c1e90a` / `f2f7202ea5` | added official telegram server in servers list / added logo for official telegram server | The Telegram built-in entry + `telegram_logo_256.png`. |
| `13720d914b` | changed default server description | Built-in server description text. |
| `df4b830741` | changed default OwpenGram server port | Port `2398`. |
| `7ebce8e9c1` / `73205a0f5b` | changed default owpengram server address / changed development server address | Official host `152.89.254.50`. |
| `46893092eb` | changed owpengram server | Server entry update. |

### Build / packaging / branding

| Commit | Title | What it fixed |
|---|---|---|
| `6e82c02f03` -> `89ff0c86ef` -> `71c512ea6b` | first/second branding step, more branding replacement | The `Branding::` namespace. |
| `58a891e107` | init branding | Branding scaffolding. |
| `b73136533c` | changed Owpengram to OwpenGram | Naming. |
| `ffbd69088c` / `fbdb20788f` / `8c4ae4cc46` / `c16ab62f11` / `439243b7ee` / `984fdde119` / `a837534fbc` | new icons / changed icons / fixed icons / changed icon size / changed intro icon size | Icon set. |
| `898f7bb59a` / `87bfd5ee85` | updated sounds / changed sounds | `owpengram` sound files. |
| `d0067dc1b8` | removed plane animation on start | Replaced the animated plane with the static logo in `intro_step.cpp`. |
| `7e2239993f` | disaplay service notifications account avatar instead of app icon | Removed the `isNotificationsUser()` logo fallback in `data_peer.cpp`. |
| `fc64f2b016` | commented modal window with calls rating | Disabled a rating modal. |
| `b7953a7d99` / `269d5cc278` / `b13fdc0953` / `a330eee40a` / `6758b00fdc` | changed local ip to XXX / changed suffix to _O1 / changed suffix / changed name suffix logic / changed client version to O3 | Version string handling (`result += "_O7"` in `about_box.cpp:158`). |
| `5322e84797` | fix for build | `Telegram/build/qt_version.py` + `scripts/build-windows.ps1`. |
| `e38f567108` | fixed linux build | Dockerfile + `scripts/lib/build-linux-steps.sh`. |
| `4c7e2b8c48` | added install-linux.sh for desktop integration | `install-linux.sh`. |
| `2bfbf8b676` | added deb packaging script, fixed dbus activation in user install | `package-deb-linux.sh` + `install-linux.sh` D-Bus fix. |
| `1fcb5fa194` | fix: drop stray Telegram/ThirdParty/TooManyCooks gitlink | Removed the bogus submodule entry added by `4c02cd94f1`. |

---

## 13. Porting notes for PolySRV (observations, not decisions)

Points worth a decision before porting, each traceable to the findings above:

1. **The DC-options lock reuses upstream's `_immutable`.** A port should give it
   a distinct name/flag rather than overload `_immutable`, or upstream's
   ".tdesktop-endpoints" semantics is silently lost (finding 3).
2. **The `_publicKeys` fingerprint map is the entire custom-key mechanism.**
   No new crypto. A port needs only `setPublicKeysFromPem` +
   `setBuiltInPublicKeys` + the lock.
3. **The discovery endpoints are plain HTTP on the server's own port.**
   Porting them as-is reproduces the MITM surface on the Add-Server path
   (finding 4). A remote domain that serves the server list — which PolySRV
   wants anyway — is the natural place to serve trusted server metadata instead.
4. **There is no remote server list in owpengram.** PolySRV wants one, served
   from the single remote-update domain. The `Server` record and the
   `RefreshServersInfo` merge logic are reusable as-is; only the source of
   `ListServers()` changes.
5. **Account identity scoping touches four places** and duplicates an FNV-1a
   helper in two files (finding 19). A port should factor the hash into one
   shared helper.
6. **Per-server local state** (recent emoji, cloud lang packs, upload limits)
   is a second wave of work after the basic server switch — it is not required
   for multi-server to function, only for correctness across servers.
7. **The URL scheme is one string in `application.cpp` plus platform
   registration.** `owpg://` -> `srv://` is a small rename, but the Linux
   `.desktop` `MimeType` and the macOS `CFBundleURLTypes` entries need to be
   added (findings 14 and 15) — owpengram never did.
8. **The `snap` package is broken in owpengram** (finding 13) and must be fixed
   or dropped in PolySRV.

---

## Appendix — how to reproduce the analysis

```bash
cd owpengram-desktop-client

# Fork-only commits relative to the upstream merge point.
git log --oneline e6bf3e41ac^1 --not e6bf3e41ac^2

# The fork's own diff against upstream v7.2.2.
git diff e6bf3e41ac^2..e6bf3e41ac^1 -- Telegram/SourceFiles

# Files that exist only in the fork.
git diff --name-status e6bf3e41ac^2..e6bf3e41ac^1
```

The upstream merge point is `e6bf3e41ac` ("Merge tag 'v7.2.2' of
telegramdesktop/tdesktop into merge-tdesktop-722-stable").
