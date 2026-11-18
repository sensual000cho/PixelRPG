# PixelRPG

[한국어](README.ko.md)

A 3D pixel-art action RPG prototype in Unreal Engine 5.8: low-poly, vertex-coloured models rendered as pixel art, melee combat built on the Gameplay Ability System, a mage companion, gathering, six quests in the village of Brookhollow and save games.

## Layout

- `Source/PixelRPG`: C++ gameplay (combat, camera rig and pixel post process, HUD, audio, quest, inventory).
- `Tools/make_assets_blender.py`: builds every model with Blender and exports `SourceArt/*.fbx`.
- `Tools/make_textures.py`: generates the detail, foliage and terrain colour textures (needs Pillow).
- `Tools/setup_pixel_assets.py`: imports everything into Unreal, builds the materials and the `PixelTest` level, including the landscape.
- `Tools/rebuild_and_shot.sh`: builds the module, runs the asset setup, plays the game and saves a screenshot to `Saved/PixelShot.png`.

## The game

- Quests: First Steps -> Steel and Straw -> Copper for the Forge (unlocks the spear), Magic Spark (the companion), the Trampled Garden, and finally King of the Glade (the Slime King boss). They are all written in `Source/PixelRPG/World/PixelQuestCatalog.cpp`.
- Monsters: slimes (body tackle), mushrooms (a charge that gets faster the longer the run-up, stunned when it hits a wall) and rock monsters (roll at you from afar or body-slam up close; tough and hard to push). They live in camps around the valley, and at most five fight at once.
- Characters and monsters are flip-books of seven posed static meshes (`SM_<Name>_00` to `_06`), not skeletal meshes.

## Controls

- Move `WASD`, attack `left mouse` / `J`, dodge `Space`, swap weapon `Tab` / `R`
- Companion: mount / deploy `G`, follow / hold / focus `1` / `2` / `3`, auto mode `T`, bolt `F`, empower `C`
- Interact (gather, talk) `E`, save `F5`, load `F9`
- Camera: mouse to turn, zoom `Z` / `X`, third person / top down `V` (`Left` / `Right` turn the top-down view), pixel effect on / off `F1`
- Mouse sensitivity `[` / `]`, hide or show the key help `H` (both are remembered)

Chaining sword attacks gives a five-hit combo: chop, slash, backhand, rising cut and stab.

## Automated runs

- `-PixelQuestTest` plays all six quests through on its own; `-PixelCombatTest` just fights the nearest enemies.
- `-PixelNewGame` replaces the existing save, `-PixelSaveSlot=<name>` uses another slot.

## Sound

The sound effects come from purchased marketplace packs that are not included here (`Content/Dev` is ignored). Without them the game runs silently; `Source/PixelRPG/PixelAudio.cpp` lists the files it looks for.

## Large files

`.uasset`, `.umap`, `.fbx` and `.png` files are stored with Git LFS. Run `git lfs install` before cloning.
