# PixelRPG

A 3D pixel-art action RPG prototype in Unreal Engine 5.8: low-poly, vertex-coloured models rendered as pixel art, melee combat built on the Gameplay Ability System, a mage companion, gathering, one quest and save games.

## Layout

- `Source/PixelRPG`: C++ gameplay (combat, camera rig and pixel post process, HUD, audio, quest, inventory).
- `Tools/make_assets_blender.py`: builds every model with Blender and exports `SourceArt/*.fbx`.
- `Tools/make_textures.py`: generates the detail, foliage and terrain colour textures (needs Pillow).
- `Tools/setup_pixel_assets.py`: imports everything into Unreal, builds the materials and the `PixelTest` level, including the landscape.
- `Tools/rebuild_and_shot.sh`: builds the module, runs the asset setup, plays the game and saves a screenshot to `Saved/PixelShot.png`.

## Sound

The sound effects come from purchased marketplace packs that are not included here (`Content/Dev` is ignored). Without them the game runs silently; `Source/PixelRPG/PixelAudio.cpp` lists the files it looks for.

## Large files

`.uasset`, `.umap`, `.fbx` and `.png` files are stored with Git LFS. Run `git lfs install` before cloning.
