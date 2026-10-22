# Development notes

How this repository is worked on. The upstream Telegram Desktop contribution
notes still apply; this file only covers what is different here.

## Branches

* `main` is never pushed to directly. All work lands on `dev`.
* `dev` is the branch every pull request targets.

## The upstream merge

`main` mirrors the upstream `dev` branch of
[telegramdesktop/tdesktop](https://github.com/telegramdesktop/tdesktop), and
`dev` carries the PolySRV changes on top of it. The upstream remote is:

```bash
git remote add upstream https://github.com/telegramdesktop/tdesktop
git fetch upstream
```

The merge is a plain merge of `upstream/dev` into `dev`. The only conflicts
are the files the branding touches: `Telegram/build/version`,
`Telegram/SourceFiles/core/version.h`, the Windows resources and the AppX
manifest. The official version moves to the new upstream version and the
fourth component of the Windows and AppX versions stays the PolySRV build
number, so the release reads 7.3.3.1 for the first PolySRV release of
Telegram 7.3.3.

After the merge the branding is restored with one command:

```bash
python3 Telegram/build/apply_branding.py
```

The `Branding` CI job fails when a merge left the branding unapplied.

## CI discipline

The GitHub Actions runners are a shared, metered resource, so the pipeline is
kept deliberately narrow.

* **One milestone per CI run.** A milestone is *landed* only after the run for
  the commit that carries it is green. Pushing the next milestone while a run
  is still in flight starts a second set of four jobs and slows both down.
* **Never push while a run is in flight** unless the run is superseded. If a
  commit has to be pushed anyway, cancel the runs of the commits it replaces
  first, so the queue does not fill up.
* **Cancel a superseded run** rather than letting it finish. A run of a commit
  that a later commit already contains proves nothing new, and on a free plan
  the concurrent job limit is the real constraint, not the minutes.
* The build matrix is one job per platform. Adding a matrix dimension means
  adding several concurrent jobs, so it has to be worth it.

### The prune that keeps the wallet engine

`mac.yml` and `win.yml` prune the third party libraries to free disk space
before the build. The prune is a negated keep list, so **it deletes every
regular file that is not on the list** — including the Rust generated
`wallet_engine.cpp`, which the CMake configure then cannot find.

The keep list must therefore protect:

* `*/src/wallet_engine/*` — the generated source,
* `*/tlottie/*` and `*/wallet-engine/*` — the Rust checkouts, minus their
  `target` directories,
* the empty directories under those two checkouts.

These are exactly the rules upstream's canary workflow already carries, and
`mac.yml` and `win.yml` now use the same expression verbatim. Changing the
prune without them breaks the Windows and macOS builds with
`Cannot find source file: .../tdesktop_rust/out/src/wallet_engine/wallet_engine.cpp`.

`mac_packaged.yml` has no prune at all, which is why it was never affected.

## The branding

The product name, the bundle identifiers, the file names and the URLs all come
from one file:

```
Telegram/build/branding
```

and one script applies them:

```
python3 Telegram/build/apply_branding.py            # apply
python3 Telegram/build/apply_branding.py --check    # report, change nothing
```

The script is the only thing that writes the branding into the sources, so
after an upstream merge the branding is restored with one command, and
`--check` fails loudly if an upstream change moved a pattern the script
patches. The `Branding` CI job runs that check on every push.

Renaming the product means editing `Telegram/build/branding`, running the
script and committing the result. Nothing else needs to change.

## The icons

`Telegram/Resources/art/generate_icons.py` produces the whole icon set from a
single square artwork named `Telegram/Resources/art/icon_source.png`. Without
that file it falls back to a placeholder mark, so the set can always be
regenerated.

## The remote files

The server list and the update manifest are published from the `gh-pages`
branch and served from `https://roon9e.github.io/polysrv-desktop/`. The
`gh-pages` branch is rewritten by hand, with `gh api`, and holds only the
static files.
