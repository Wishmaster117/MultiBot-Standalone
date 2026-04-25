<img width="1024" height="559" alt="image" src="https://github.com/user-attachments/assets/3ac43983-8767-4dd6-9a17-4548ede1e9d3" />

# Breaking News
MultiBot has been converted to a bridge-first, mostly chatless architecture.

The addon now uses `mod-multibot-bridge` to request structured data from the server instead of relying on automatic legacy chat parsing for the main UI refresh paths. Roster, states, details, stats, inventory, spellbook, glyphs and related UI data are now refreshed through `MBOT GET~...` bridge requests when the bridge is available.

Manual playerbot commands are still intentionally preserved for diagnostics and gameplay actions. Outfits are also intentionally kept on the legacy path for now because they do not create heavy spam and will be migrated later.

Legacy automatic chat fallback is disabled by default:

```lua
MultiBot.allowLegacyChatFallback = false
```

Set it to `true` only for temporary debugging if you need to test the old chat-based fallback behavior.

# MultiBot
MultiBot is a user interface addon for the AzerothCore `mod-playerbots` module by the Playerbots team: https://github.com/mod-playerbots/mod-playerbots.<br>
Tested with American, German, French and Spanish 3.3.5 WotLK clients.

This version also expects the companion server module `mod-multibot-bridge` to be installed if you want the new bridge-first/chatless UI behavior.

# Installation

## 1. Server-side bridge module
1. Copy the `mod-multibot-bridge` folder into your AzerothCore modules directory, next to `mod-playerbots`:

```text
azerothcore-wotlk/modules/mod-multibot-bridge
```

2. Re-run CMake for your AzerothCore build if your workflow requires it.
3. Rebuild the server.
4. Start the server and check that `mod-multibot-bridge` is loaded.

When the addon connects successfully, the server console should show messages similar to:

```text
MBOT HELLO
MBOT HELLO_ACK
MBOT PING
MBOT PONG
GET~ROSTER
GET~STATES
GET~DETAILS
```

## 2. Client-side addon
1. Copy the addon files into a folder named `MultiBot` inside your World of Warcraft AddOns directory:

```text
World of Warcraft/Interface/AddOns/MultiBot
```

Example on Windows:

```text
DRIVE:\WowClientFolder\Interface\AddOns\MultiBot
```

2. Make sure the `.toc` file is directly inside the `MultiBot` folder. The addon should not be nested inside an extra directory such as `MultiBot/MultiBot/...`.
3. Start World of Warcraft.
4. Enable the addon on the character selection screen if needed.
5. Log in and use `/multibot`, `/mbot`, `/mb`, or the minimap button.

## 3. Recommended configuration
For normal bridge-first usage, keep legacy automatic chat fallback disabled:

```lua
MultiBot.allowLegacyChatFallback = false
```

Only enable it temporarily for debugging:

```lua
MultiBot.allowLegacyChatFallback = true
```

# Use
Start World of Warcraft and enter `/multibot`, `/mbot`, or `/mb` in the chat, or use the minimap button.

The addon will automatically try to connect to `mod-multibot-bridge`. If the bridge is connected, the main UI refresh paths use structured bridge messages instead of legacy chat replies.

# Current Status
Implemented bridge-first/chatless areas:

- Bridge handshake: `HELLO`, `HELLO_ACK`, `PING`, `PONG`.
- Roster refresh.
- Bot states refresh.
- Bot details refresh.
- Stats refresh.
- Inventory refresh with icons and item tooltips.
- Spellbook refresh.
- Glyph refresh with icons and glyph tooltips.
- Custom Glyphs socket mapping and apply order.
- Talent tab navigation stability after switching between tabs.
- Automatic bot reconnect on login/reload for bots already present in the group or raid.
- Units bar refresh after adding a bot through AddClass.

Kept intentionally:

- Manual whisper/playerbot commands for diagnostics.
- Outfits through the legacy path for now.
- Gameplay write actions that still rely on existing playerbot commands.

# Remaining Work
The next logical step is now: **migrate Outfits to bridge-first**.

Planned follow-up work:

- Add a structured `GET~OUTFITS~<bot>~<token>` flow in `mod-multibot-bridge`.
- Replace the current Outfits chat parsing path in the addon with bridge responses.
- Keep the old Outfits parser only as an optional legacy fallback while testing.
- Continue regression testing login, `/reload`, large raid groups, Units, EveryBars, Stats, Inventory, Spellbook, Talents and Glyphs.
- Remove obsolete debug prints and dead legacy parser paths once the bridge-first behavior is fully stable.
- Update screenshots and user documentation after the Outfits migration.

---

# Detailed Notice
