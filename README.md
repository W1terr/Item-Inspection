# Item Inspection

SKSE plugin for Skyrim SE / AE, inspired by the item pickups of Crimson Desert.

When you take an item from the world it doesn't go straight to your inventory:

1. Your right hand brings the item up. In 1st person you see it from your eyes; in 3rd person the camera flies in over
   your right shoulder and your character holds it (or, if you turn that off, the camera switches to 1st person with a
   short fade). In 3rd person your character goes back to standing and holds still while looking at the item.
2. Move the mouse (or the right stick) to turn the item and look at it from every side. The mouse wheel brings it closer
   or moves it farther. Hold the right mouse button (gamepad RT) to look around; the hand and the item stay put.
3. Press **E** (gamepad **A**) to put it in your backpack: the item comes down onto your palm, the hand goes down and
   behind your back, and the item is added to the inventory.
   Press **R** (gamepad **B**) to put it back where it was instead.
   Press **F** (gamepad right stick click) to switch between 3rd and 1st person while you hold it (with a short fade).

Afterwards the camera goes back to where it was (or stays in the view you switched to with F).

- Small items float over your open hand with the Telekinesis spell's hand effect; your fingers move while you turn them.
- Weapons are held by the grip, bows like an archer holds them (string towards you). The mouse turns the hand.
- When the item reaches your hand you hear its own pickup sound.
- While you hold an item the world waits: nobody talks to you, attacks you or can hurt you. The crosshair, compass and
  bars are hidden, and the keys to put the item away or back stay on screen.
- By default you look at each kind of item only the first time you take it in a playthrough (remembered in the save);
  after that it goes straight into the inventory. Turn on "Show it every time" to always hold items first.
- In the inventory, scrolling up on an item (the vanilla zoom) closes the inventory and puts the item in your hand.
  Putting it away opens the inventory again (Skyrim AE only).

The arm is animated procedurally in the plugin: no animation files, no behavior patches, no plugin (ESP).
Works with skeleton replacers such as XPMSSE, and with Improved Camera SE and SmoothCam. With
[Immersive Interactions](https://www.nexusmods.com/skyrimspecialedition/mods/47670) its pickup animation plays first
in 3rd person, then the item comes to your hand.

## Requirements

- Skyrim SE / AE with [SKSE](https://skse.silverlock.org/) (built against CommonLibSSE-NG, tested on 1.7.104)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
- Optional: [SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/120352) for the in-game settings
  page (Item Inspection > Settings); without it the settings are in the INI

## Credits

- Inspired by the item pickups of Crimson Desert by Pearl Abyss.
- [CommonLibSSE-NG](https://github.com/alandtse/CommonLibVR/tree/ng) (MIT) and the
  [SKSE Menu Framework](https://github.com/QTR-Modding/SKSE-Menu-Framework-3-API) API (LGPL-2.1).
- The telekinesis hand effect, light and grab sound are the vanilla Telekinesis spell's (Skyrim.esm); the fist that
  holds weapons starts from the game's 1st person one-handed idle. Both are used from the game while it runs.

## License

Copyright © 2007 Free Software Foundation, Inc. <https://fsf.org/>

Everyone is permitted to copy and distribute verbatim copies of this license document, but changing it is not allowed.
