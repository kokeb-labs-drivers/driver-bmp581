# Repository setup

The one-time steps an org owner runs after creating a repository from the template, and before the first PR.

The scripts are in [`kokeb-labs/.github/org/`](https://github.com/kokeb-labs/.github/tree/main/org). Each one is a dry run
unless given `--apply`, and `--repo` limits it to one repository. The rules behind them are in
[repo-governance.md](https://github.com/kokeb-labs/.github/blob/main/standards/repo-governance.md).

## 1. Create and scaffold

```bash
gh repo create kokeb-labs-drivers/driver-<chip> --public --template kokeb-labs/driver-template --clone
cd driver-<chip>
git switch -c feat/initial-driver
python3 tools/setup.py
```

`tools/setup.py` asks for the bus and a one-sentence description, scaffolds, builds and runs the host tests;
`tools/scaffold.py --help` lists every option (`--platforms host,esp32` is a driver's default; a library gets
`host` unless `esp32` is asked for); it ends with a checklist of what is left to fill in.

## 2. Apply the org settings

From a checkout of `kokeb-labs/.github`, after adding the repository to `org/repos.json` by PR. Its entry sets
the About: the description is the README's first sentence (`C++20 driver for the <vendor> <parts> <what it is>
(<bus>)`), and the topics are the shared `kokeb-labs driver embedded cpp20 misra-cpp header-only` plus the bus, the
device category, the vendor and the part (`spi eeprom microchip at25040b`):

```bash
python3 org/kokeb_org.py --repo driver-<chip>           # the plan: every setting that would change
python3 org/kokeb_org.py --repo driver-<chip> --apply   # settings, security, Actions, rulesets, labels
```

A private repository on the free plan gets the settings, Actions policy and labels; security features and
rulesets need it public (the tool says which sections it skips).

Run it without `--apply` first to see what changes.

## 3. Create the labels

```bash
python3 - <<'PY'
import subprocess, yaml
for label in yaml.safe_load(open(".github/labels.yml")):
    subprocess.run(["gh", "label", "create", label["name"], "--color", label["color"],
                    "--description", label["description"], "--force"], check=True)
PY
```

## 4. Require the gate

After the first CI run has produced the check `CI gate / gate`, add it to the repository's `main-review` ruleset as
described in [The required check](https://github.com/kokeb-labs/.github/blob/main/standards/repo-governance.md#the-required-check).

## 5. Wire it in

Add the repository as a submodule or CMake dependency where it is consumed, in its own pin PR, then
follow [checklists/new-driver.md](https://github.com/kokeb-labs/.github/blob/main/standards/checklists/new-driver.md).
