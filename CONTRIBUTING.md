# Contributing

Bug reports, ideas and pull requests go through this repository's issues and pull
requests. Add-ons that leave the official mods unchanged fit better as Community Mods: the
[AZ-MODS SDK](https://github.com/Kyle-Hosman/az-mods-sdk) has the template, the guide and
the publishing rules.

## Bug reports

| Include | Why |
|---|---|
| AZ-MODS mods version (Mod Menu > Updates) and deck firmware | the code paths and address tables differ per build ([docs/FIRMWARE-SUPPORT.md](docs/FIRMWARE-SUPPORT.md)) |
| What you did, what you expected, what happened | the steps to reproduce |
| Which deck (1 to 4) and which view (2 DECK or 4 DECK) | per-deck state is resolved per deck; a fault on deck 2 and up can be absent on deck 1 |
| A photo or video of the deck screen | most mods are visual |
| The deck's logs, if you have SSH: one command collects them ([docs/LOGS.md](docs/LOGS.md#collecting-them-over-ssh)) | crash reports and the loader's decisions; `/tmp` is cleared at power-off, so collect them before power-cycling the deck |

## Pull requests

| Step | Where |
|---|---|
| Read how the tree is organized and its conventions | [ARCHITECTURE.md](ARCHITECTURE.md), section 9 |
| Follow the stability practices every mod follows | the SDK's [Guidelines](https://github.com/Kyle-Hosman/az-mods-sdk/blob/main/docs/GUIDELINES.md) |
| Run the unit tests, all three passes | `tests/run-linux.sh` ([docs/TESTING.md](docs/TESTING.md)) |
| Run the deck script suites, for a change under `deck/` | `deck/test/run.sh` ([deck/README.md](deck/README.md#tests)) |
| Test on a deck: the change and its control, on every deck it applies to | [docs/TESTING.md](docs/TESTING.md#5-on-a-real-deck) |
| Install the pre-push hook | `tools/install-hooks.sh` runs `tools/public-hygiene` over the tree and the pushed commit messages (rules in `.public-hygiene.json`) |

The pull request says what was tried, on which firmware and decks, and what the control
showed.

No images, code or data taken from the deck's firmware: the Guidelines have the rule.
Addresses and descriptions of how the deck behaves are fine.

## License

GPL-3.0-only (see [LICENSE](LICENSE), and [NOTICE](NOTICE) for the third-party parts).
Contributions are accepted under the same license.
