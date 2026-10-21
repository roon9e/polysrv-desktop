# PolySRV Desktop

PolySRV is a [Telegram Desktop][telegram_desktop] fork that lets you sign in to
**more than one server** from a single application, alongside the official
Telegram network.

It keeps the upstream Telegram Desktop feature set, the same
[MTProto][telegram_proto] transport and the same update model, and adds:

* **A server picker at sign-in.** Choose the official Telegram network, or
  point the client at a self-hosted MTProto backend, when you add an account.
* **One client, several accounts.** Every account keeps its own server
  binding, its own session identity and its own local state, so the same
  numeric user id on two servers does not collide.
* **A single remote domain.** The server list and the update manifest are
  served from one place, so the client only has one address to trust.
* **Its own versioning.** The official Telegram version, plus a PolySRV build
  number that counts the releases within it.

The multi-server design is inspired by the
[owpengram](https://github.com/owpengram/owpengram-desktop-client) desktop
client, with a number of its rough edges fixed. PolySRV is not affiliated with
Telegram FZ-LLC or with owpengram; Telegram itself remains a separate project
with its own clients and its own releases.

## Building

The build instructions are the upstream ones, in the `docs` folder:

* [Windows](docs/building-win.md)
* [macOS](docs/building-mac.md)
* [Linux](docs/building-linux.md)

The product name, the bundle identifiers and the file names are all driven by
one file, `Telegram/build/branding`, and one script,
`Telegram/build/apply_branding.py`. Renaming the product means editing that
file and running the script.

## Releases

Builds are published as GitHub releases, and the update manifest that the
client checks lives at
`https://roon9e.github.io/polysrv-desktop/update.json`. The server list is at
`https://roon9e.github.io/polysrv-desktop/servers.json`.

## License

The source code is published under GPLv3 with the OpenSSL exception, the same
license as upstream Telegram Desktop, available [here][license].

[telegram_desktop]: https://github.com/telegramdesktop/tdesktop
[telegram]: https://telegram.org
[telegram_api]: https://core.telegram.org/api
[telegram_proto]: https://core.telegram.org/mtproto
[license]: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
