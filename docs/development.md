# Development notes

How this repository is worked on. The upstream Telegram Desktop contribution
notes still apply; this file only covers what is different here.

## Branches

* `main` is never pushed to directly. All work lands on `dev`.
* `dev` is the branch every pull request targets.

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
