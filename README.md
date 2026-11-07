# PolySRV Desktop

**One client. Every server you trust.**

PolySRV Desktop is a multi-server messenger built on the familiar Telegram
Desktop experience. Sign in to the official Telegram network, to a server your
community publishes, or to a machine you run yourself — each account bound to
its own server, all in one app. Private by design, comfortable to use, and free
from lock-in.

> **Available for Windows, Linux and macOS (x64 and Apple Silicon).** Grab a build from
> the [Releases](https://github.com/roon9e/polysrv-desktop/releases) tab.

> Built on **MTProto API layer 232**, on top of Telegram Desktop **7.3.3**.

---

## Download

Builds live in the
[Releases](https://github.com/roon9e/polysrv-desktop/releases) tab.

| Platform | File | What it is |
|---|---|---|
| Windows x64 | `PolySRV-windows-x64.exe` | Run the client directly — no installer, no archive. |
| Windows arm64 | `PolySRV-windows-arm64.exe` | The same client for Windows on ARM. |
| Linux x64 | `PolySRV-linux-x64` | **Not an installer — the client binary itself.** `chmod +x PolySRV-linux-x64 && ./PolySRV-linux-x64` and you are in. |
| macOS arm64 | `PolySRV-macos-arm64.dmg` | The app bundle for Apple Silicon. |
| macOS x64 | `PolySRV-macos-x64.dmg` | The app bundle for Intel Macs. |

Every release also publishes `SHA256SUMS`. The builds are **not code-signed** —
Windows SmartScreen and macOS Gatekeeper will warn on first launch; the macOS
build is ad-hoc signed only — so check a download against its checksum before you
keep it.

`srv://` links work in every build: opening one hands the address to the client.

## Try it without setting anything up

PolySRV publishes a **public test server** in its server list, so it is already
there when you sign in. On the server-selection screen, pick **Test server**
instead of Telegram and sign in as usual.

That account sits alongside your Telegram one, so you can try the project
without leaving anything behind. When you want your own server later, you add it
in the same app and keep both.

## Why you will like it

- **Multi-server** — add accounts on different servers and switch between them freely.
- **Bring your own server** — connect to a machine you host and fully control.
- **Adding one takes an address** — type `host:port`, and the client fetches the server's key, data centre and identity itself.
- **Familiar and comfortable** — the experience you already know, no learning curve.
- **Private** — talk on infrastructure you trust, away from the cloud.
- **Censorship-resistant** — your own server stays reachable when others are blocked.
- **Open source** — read it, audit it, build it yourself.

## How multi-server works

Every account is tied to a server, and you choose that server when you sign in.
PolySRV comes with ready-to-use options:

- **Telegram** — the official network, with your normal Telegram account;
- **Published servers** — the ones the PolySRV list names, fetched from a single address;
- **Custom** — any server you or your community runs.

Several accounts on different servers stay cleanly separated — different
identities, different data, one app. The separation is real, not cosmetic: each
account's cached peers, files and ids are scoped to the server they came from, so
two servers that happen to hand out the same numeric id never bleed into each
other.

The account list groups your accounts by the server they belong to and names each
group after it, so it is always clear which account lives where.

Official Telegram accounts still count against Telegram's own limit (3, or 6 with
Premium); self-hosted ones are not subject to that. The app's own ceiling — 20
accounts, or 50 with Telegram Premium — applies across every server combined.

Remove a server and every account on it goes with it — no orphaned logins left
behind pointing at a host that no longer exists.

## Connect your own server

On the **server selection screen** (shown when you sign in or add an account),
click **Add server** and type the address — `chat.example.com:2398`, or
`203.0.113.10:2398`. That is the only field you have to fill in.

The client then asks the server who it is and fills in the rest by itself: the
RSA public key, the home data centre, and the name, description and logo the
server's operator set. Give it your own name or picture if you like — the logo in
the header of the box is a button that takes an image from your disk. Save, pick
the server, sign in as usual.

Everything the client fetched stays editable under **Advanced → Servers**, for a
server that answers nothing or when you want to pin the key yourself.

> Only `host:port` is ever taken from a link or typed in. The identity a server
> claims is fetched from that address directly, so nobody can hand you a link
> that misrepresents whose server you are about to trust.

Operators can hand out a ready-made **`srv://addserver?host=...&port=...`** link:
opening it pops the Add server box pre-filled with the address, and nothing else.

## Versioning

PolySRV Desktop tracks the upstream Telegram Desktop version it is built from.
The About box and the left menu read that version and nothing more — there is no
separate PolySRV counter to keep in mind.

## Releases

Builds are published as GitHub releases, and the update manifest the client
checks lives at `https://roon9e.github.io/polysrv-desktop/update.json`. The
server list is at `https://roon9e.github.io/polysrv-desktop/servers.json` — one
address the client has to trust for both.

The download page is `https://roon9e.github.io/polysrv-desktop/`, and it links to
the latest release.

## Relation to OwpenGram

The multi-server design follows the
[OwpenGram](https://github.com/owpengram/owpengram-desktop-client) desktop
client, which PolySRV is grateful to: it showed that one client can hold accounts
on several servers without confusing them. PolySRV keeps that shape and fixes a
number of rough edges — among them the separation of each account's data, a
server list served from one address, and adding a server from its address alone.

PolySRV is not affiliated with Telegram FZ-LLC or with OwpenGram. Telegram
remains a separate project with its own clients and its own releases.

## Building

The build instructions are the upstream ones, in the `docs` folder:

* [Windows](docs/building-win.md)
* [macOS](docs/building-mac.md)
* [Linux](docs/building-linux.md)

The product name, the bundle identifiers and the file names are all driven by one
file, `Telegram/build/branding`, and one script,
`Telegram/build/apply_branding.py`. Renaming the product means editing that file
and running the script.

## License

The source code is published under GPLv3 with the OpenSSL exception, the same
license as upstream Telegram Desktop, available [here][license].

[telegram_desktop]: https://github.com/telegramdesktop/tdesktop
[telegram]: https://telegram.org
[telegram_proto]: https://core.telegram.org/mtproto
[license]: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
