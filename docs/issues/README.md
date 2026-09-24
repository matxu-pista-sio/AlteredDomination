# Issue backlog

GitHub Issues are **disabled** on `matxu-pista-sio/AlteredDomination`
(the API answers `410 Issues has been disabled in this repository`), and
only the repository owner can enable them (Settings → General → Features →
Issues). Until then the plan's tickets live here, one file per issue, in
the exact house format they will be filed with (`.claude/skills/create-ticket`).

Numbering is the plan's (docs/MODERNIZATION_PLAN.md §4); GitHub assigns its
own numbers on filing, after which this directory becomes the archive of
the done-records.

| # | Title | Owner action? | Status |
|---|---|---|---|
| 01 | [Meta] Project skeleton: CMake presets, core/client split, C++23, Qt 6.8+, CI, docs, legacy tree removed | | done |
| 02 | [Data] World generator from Natural Earth + World Bank: countries, ~1000 cities, Miller projection, symmetric link graph | | done |
| 03 | [Data] Per-city territories (Voronoi ∩ country), country outlines and banner colours as vector path data | | done |
| 04 | [Data] SVG flags (flag-icons), generated SVG unit icons, cleaned units.json with a validator | | done |
| 05 | [Core] World and unit catalog: immutable data model loaded from the generated JSON | | done |
| 06 | [Core] Campaign rules engine: economy, rounds, recruit/move/attack, capture, elimination, victory, hotseat | | done |
| 07 | [Core] Save/load: versioned JSON schema with world hash and RNG state | | done |
| 08 | [Core] Board battle engine: phases, pattern moves and strikes, generals, generalmate, draw and surrender | | done |
| 09 | [Core] Strategic AI: analysis, counter recruitment, scored attacks, reinforcement, personalities, AiRound | | done |
| 10 | [Core] Tactical AI: formation, promotion, alpha-beta search with node budgets | | done |
| 11 | [Core] AI-vs-AI auto-resolve through the real board engine + performance budget | | done |
| 12 | [Client] Theme singleton, ADDesktop control style, OFL fonts, settings persistence | | done |
| 13 | [Client] App shell: StackView pages (Intro, Home, New game, Load game, Settings, Codex) over the bridge | | done |
| 14 | [Client] World map view: Shapes curve renderer territories, ocean shader, links, LOD markers, camera | | done |
| 15 | [Client] Campaign HUD: top bar, city sheet, ranking, end turn with AI progress, toasts, game over | | done |
| 16 | [Client] Battle screen: board, chips, highlights, animations, particles, unit card, result, AI worker | | done |
| 17 | [Client] Audio: SoundEffect pool and music with saved volumes; prune legacy duplicates | | done |
| 18 | [Client] Saves UI: slots with metadata, autosave, in-game menu | | done |
| 19 | [Client] Desktop polish: shortcuts, tooltips, high-DPI, headless AD_DRIVE hook, skills | | done |
| 20 | [Owner] Choose a license for the code and the generated data | **yes** | open |
| 21 | [Owner] Asset provenance: confirm or replace the legacy sounds, fonts and logo | **yes** | open |
| 22 | [Epic] Online multiplayer: ranked campaigns over a matchmaking + relay server | | done |
| 23 | [Core] Wire codec: JSON round trips for the campaign and board commands | | done |
| 24 | [Server] ad-server: accounts, ELO, the ranked queue, the setup negotiation, the lockstep relay | | done |
| 25 | [Client] The lobby: LobbyClient, the Online page, the shared country picker | | done |
| 26 | [Client] The lockstep session: GameController and BattleController online modes | | done |
| 27 | [Client] Watching a battle, reconnecting mid-match, the verdicts | | done |
| 28 | [Owner] Host ad-server: a machine, TLS, and the client's default address | **yes** | open |

Every done ticket ends with a **Done** section: the commits that closed
it and the screenshots that prove it (taken headless with
`scripts/ad.py`, see `.claude/skills/share-screenshot`); a reduced set of
those screenshots is kept in `docs/screenshots/`.

Labels on filing: `enhancement` for features, `bug` where a defect is fixed
(07), `documentation` for the owner items; issue type `Feature`/`Task`;
owner items assigned to `matxu-pista-sio`.
