"""Creates the UE assets of the prototype from SourceArt/ (see make_assets_blender.py, make_textures.py).

  /Game/Pixel/M_PixelPost      post process: soft dithered shading, fog, sky, character outline
  /Game/Pixel/M_VertexColor    vertex colours x pixel detail texture (props; cut away when they hide the player)
  /Game/Pixel/M_Character      the same without the cut-away (characters, weapons)
  /Game/Pixel/M_Terrain        vertex colours x ground texture (the valley floor)
  /Game/Pixel/T_*              detail textures
  /Game/Meshes/SM_*            meshes
  /Game/Maps/PixelTest         test level (overwritten on every run)

Run from the editor:  Tools > Execute Python Script...
or headless:          UnrealEditor-Cmd.exe PixelRPG.uproject -ExecutePythonScript=Tools/setup_pixel_assets.py
"""
import math
import os
import random
import sys

import unreal

PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
sys.path.insert(0, os.path.join(PROJECT_DIR, "Tools"))
import terrain  # noqa: E402  (shared with the Blender script)

mel = unreal.MaterialEditingLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary

PIXEL_DIR = "/Game/Pixel"
MESH_DIR = "/Game/Meshes"
MAP_PATH = "/Game/Maps/PixelTest"
COLLECTION = None   # /Game/Pixel/MPC_Pixel, made first by main()
SOURCE_DIR = os.path.join(PROJECT_DIR, "SourceArt")

# Characters are animated like a flip book: one model per pose (see the *_POSES lists in make_assets_blender.py).
POSE_SETS = {"SM_Hero": 34, "SM_Slime": 7, "SM_Companion": 7, "SM_Villager": 4}
POSE_MESHES = [f"{prefix}_{i:02d}" for prefix, count in POSE_SETS.items() for i in range(count)]
TREES = ["SM_TreePine", "SM_TreeRound", "SM_TreeRed", "SM_TreeOrange"]
# Every tree is two models on the same spot: the trunk blocks movement, the leaves collide with nothing.
TRUNK_MESHES = {tree + "_Trunk" for tree in TREES}
LEAF_MESHES = {tree + "_Leaves" for tree in TREES}
MESHES = POSE_MESHES + sorted(TRUNK_MESHES) + sorted(LEAF_MESHES) + ["SM_Rock", "SM_Bush",
          "SM_Grass", "SM_Flowers", "SM_Crystal", "SM_House", "SM_Sword", "SM_Spear",
          "SM_SlashArc", "SM_Ring", "SM_Bolt", "SM_Herb", "SM_Ore", "SM_Mountain"]
# Meshes with leaf cards: their second material slot shows the alpha cut-out foliage atlas.
FOLIAGE_MESHES = LEAF_MESHES | {"SM_Bush", "SM_Grass"}
# Placed as foliage instances (the same thing the editor's Foliage mode paints): no collision, cheap to draw
# in thousands, and editable afterwards with the Foliage tool. Value = casts a shadow.
INSTANCED_MESHES = {**{leaves: True for leaves in LEAF_MESHES}, "SM_Bush": True, "SM_Grass": False, "SM_Flowers": False}
# Meshes the characters must not walk through. The triangles are used directly as collision.
SOLID_MESHES = {"SM_Rock", "SM_Crystal", "SM_House"} | TRUNK_MESHES
# Characters and what they carry: never cut away by the see-through effect.
CHARACTER_MESHES = set(POSE_MESHES) | {"SM_Sword", "SM_Spear", "SM_Bolt", "SM_SlashArc"}
# Things to pick up: part of the world (full resolution, real depth), but never cut away.
PROP_MESHES = {"SM_Herb", "SM_Ore"}

POST_CODE = r"""
// Scene texture ids: 14 = PostProcessInput0, 1 = SceneDepth, 5 = BaseColor, 13 = CustomDepth, 25 = CustomStencil
const float3 LumaW = float3(0.3, 0.59, 0.11);
float2 P = Parameters.SvPosition.xy;

// --- Characters are redrawn in big screen-aligned pixels; the world stays full resolution ---
// Each character has a grid (anchor in pixels, cell size) and a stencil id. A pixel shows the scene
// at the centre of its cell if that centre lies on the character, so the 3D model reads as a sprite.
float2 Off = float2(0, 0);   // where this pixel takes its colour from, relative to itself
float IsChar = 0;
float CellEdge = 0;
{
    float3 Chars[16] = { Char0, Char1, Char2, Char3, Char4, Char5, Char6, Char7, Char8, Char9, Char10, Char11, Char12, Char13, Char14, Char15 };
    float SelfS = SceneTextureFetchFunc(Parameters, 25, float2(0, 0)).r;
    float SelfD = SceneTextureFetchFunc(Parameters, 1, float2(0, 0)).r;
    float SelfCD = SceneTextureFetchFunc(Parameters, 13, float2(0, 0)).r;
    float Best = 1e12;
    float BestId = 0;
    float BestSize = 0;
    float2 BestOff = float2(0, 0);
    float2 Fall = float2(0, 0);
    for (int i = 0; i < 16; i++)
    {
        float3 A = Chars[i];
        float Id = i + 1;
        if (A.z >= 1.5)
        {
            float2 O = A.xy + (floor((P - A.xy) / A.z) + 0.5) * A.z - P;
            float S = SceneTextureFetchFunc(Parameters, 25, O).r;
            if (abs(S - Id) < 0.5)
            {
                float CDc = SceneTextureFetchFunc(Parameters, 13, O).r;
                float Dc = SceneTextureFetchFunc(Parameters, 1, O).r;
                // Visible at the cell centre, nothing nearer on this pixel, and the nearest character wins.
                if (CDc <= Dc + 2.0 && SelfD > CDc - 40.0 && CDc < Best)
                {
                    Best = CDc;
                    BestId = Id;
                    BestSize = A.z;
                    BestOff = O;
                }
            }
            else if (abs(SelfS - Id) < 0.5 && SelfCD <= SelfD + 2.0)
            {
                // A sliver of the character in a cell whose centre misses it: show what the centre shows.
                Fall = O;
            }
        }
    }
    if (BestId > 0.5)
    {
        Off = BestOff;
        IsChar = 1;
        // One-pixel lines, in character pixels: around the silhouette, and inside it where this
        // part is clearly in front of its neighbour (an arm across the body, the head over the scarf).
        float2 Steps[4] = { float2(0, -1), float2(1, 0), float2(0, 1), float2(-1, 0) };
        for (int k = 0; k < 4; k++)
        {
            float2 N = Off + Steps[k] * BestSize;
            float S = SceneTextureFetchFunc(Parameters, 25, N).r;
            if (abs(S - BestId) > 0.5)
            {
                CellEdge = 1;
            }
            else if (SceneTextureFetchFunc(Parameters, 13, N).r - Best > DepthEdge)
            {
                CellEdge = max(CellEdge, InnerLine);
            }
        }
    }
    else
    {
        Off = Fall;
    }
}

float3 C = SceneTextureFetchFunc(Parameters, 14, Off).rgb;
float  D = SceneTextureFetchFunc(Parameters, 1, Off).r;
float3 B = SceneTextureFetchFunc(Parameters, 5, Off).rgb;

// View ray of this pixel (camera -> scene).
float3 Ray = -Parameters.CameraVector;
float3 Haze = pow(float3(0.72, 0.80, 0.86), 2.2);
float3 SkyZenith = pow(float3(0.44, 0.60, 0.80), 2.2);

// Sky: a hazy gradient instead of the physical sky.
if (D > SkyDepth)
{
    return lerp(Haze, SkyZenith, saturate(Ray.z * 2.2)) * View.PreExposure;
}

// --- Shading: the amount of light re-applied to the albedo, shadows tinted cool and lit areas warm ---
// Smooth on the world; in a few flat steps on characters, like hand-shaded sprites.
float BaseLuma = max(dot(B, LumaW), 0.02);
float Light = saturate(dot(C * View.OneOverPreExposure, LumaW) / BaseLuma / LightMax);
float Band = IsChar > 0.5 ? saturate(floor(Light * CharBands + 0.5) / CharBands) : Light;
float3 ShadowTint = float3(0.40, 0.45, 0.62) * ShadowBrightness;
float3 LitTint = float3(1.00, 0.96, 0.88);
float3 Out = B * lerp(ShadowTint, LitTint, Band) * View.PreExposure;

// Darker pixels along the silhouette of a character.
Out = lerp(Out, Out * OutlineDarken, CellEdge);   // CellEdge is 1 on the silhouette, less on inner lines

// --- Fog: thick haze that swallows the distance ---
float Fog = saturate((D - FogStart) / max(FogEnd - FogStart, 1)) * FogMax;
Out = lerp(Out, Haze * View.PreExposure, saturate(Fog));
return Out;
"""

VERTEX_COLOR_CODE = r"""
// Detail texture projected along the dominant local axis, so it sticks to moving and rotated objects.
float3 A = abs(LN);
float2 UV = A.z >= max(A.x, A.y) ? LP.xy : (A.x >= A.y ? LP.yz : LP.xz);
// Explicit mip level from distance: the projection switch would otherwise leave seams.
float Lod = log2(max(Depth * LodScale, 1.0));
float3 Detail = Texture2DSampleLevel(Tex, TexSampler, UV / TileSize, Lod).rgb * 2.0;
return pow(VC, 2.2) * lerp(float3(1, 1, 1), Detail, DetailStrength);
"""

SEE_THROUGH_CODE = r"""
// Props between the camera and the character are cut away with a dither pattern around the
// screen centre (where the character is), so canopies and roofs never hide the player.
// Enabled is 0 until the game's camera turns it on, so nothing is cut away in the editor viewport.
#if SHADOW_DEPTH_SHADER
return 1;
#endif
static const float Bayer[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
float2 UV = Parameters.SvPosition.xy * View.ViewSizeAndInvSize.zw;
float2 P = (UV - float2(0.5, 0.55)) * float2(View.ViewSizeAndInvSize.x * View.ViewSizeAndInvSize.w, 1);
float Radial = saturate((1 - length(P) / ClipRadius) * 2.5);
float Near = saturate((ClipDepth - Depth) / 80);
// Whatever the camera itself is standing in (a canopy, a wall) goes completely, all over the screen:
// the boom is long, so the camera often ends up inside a tree.
float Close = saturate((ClipDepth * 0.5 - Depth) / 80);
int2 Cell = int2(Parameters.SvPosition.xy) & 3;
float Dither = (Bayer[Cell.y * 4 + Cell.x] + 0.5) / 16.0;
return Dither < 1 - max(Radial * Near * 0.94, Close) * Enabled ? 1 : 0;
"""

FLATTEN_CODE = r"""
// Squashes the model along the line from its pivot to the camera until it is almost a card.
// On screen it looks the same, but its depth becomes that of one plane: a character is then
// wholly in front of or wholly behind a wall, a tree or another character, the way a sprite is,
// and a spear or a rider no longer pokes through things. Shadows keep the full shape.
#if SHADOW_DEPTH_SHADER
return float3(0, 0, 0);
#endif
float3 ToCam = normalize(CamPos - ObjPos);
return -ToCam * dot(WP - ObjPos, ToCam) * (1.0 - Thickness);
"""

TERRAIN_CODE = r"""
float Lod = log2(max(Depth * LodScale, 1.0));
float3 Detail = Texture2DSampleLevel(Tex, TexSampler, WP.xy / TileSize, Lod).rgb * 2.0;
// The landscape has no vertex colours: its colour comes from a map laid over the whole ground,
// one texel per ColStep world units with the first texel centred on -ColHalf.
float2 ColUV = (WP.xy + ColHalf + ColStep * 0.5) / (ColHalf * 2 + ColStep);
float3 Colour = Texture2DSampleLevel(ColTex, ColTexSampler, ColUV, 0).rgb;
return pow(Colour, 2.2) * lerp(float3(1, 1, 1), Detail, DetailStrength);
"""


def delete_if_exists(path):
    if eal.does_asset_exist(path):
        eal.delete_asset(path)


def build_parameter_collection():
    """Values the running game sets for every material at once (see APixelCameraRig::BeginPlay).
    SeeThrough: 1 while playing, 0 otherwise."""
    # Kept from run to run: replacing it while materials that use the old one are still loaded crashes the renderer.
    collection = unreal.load_asset(f"{PIXEL_DIR}/MPC_Pixel.MPC_Pixel")
    if collection:
        return collection
    collection = asset_tools.create_asset("MPC_Pixel", PIXEL_DIR, unreal.MaterialParameterCollection,
                                          unreal.MaterialParameterCollectionFactoryNew())
    switch = unreal.CollectionScalarParameter()
    switch.set_editor_property("parameter_name", "SeeThrough")
    switch.set_editor_property("default_value", 0.0)
    collection.set_editor_property("scalar_parameters", [switch])
    eal.save_loaded_asset(collection)
    return collection


def connect_see_through_switch(mat, mask, x, y):
    switch = mel.create_material_expression(mat, unreal.MaterialExpressionCollectionParameter, x, y)
    switch.set_editor_property("collection", COLLECTION)
    switch.set_editor_property("parameter_name", "SeeThrough")
    mel.connect_material_expressions(switch, "", mask, "Enabled")


def create_material(name):
    delete_if_exists(f"{PIXEL_DIR}/{name}")
    return asset_tools.create_asset(name, PIXEL_DIR, unreal.Material, unreal.MaterialFactoryNew())


def add_custom(mat, description, code, input_names, x=-450, y=0):
    custom = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, x, y)
    custom.set_editor_property("description", description)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("code", code)
    inputs = []
    for name in input_names:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", name)
        inputs.append(ci)
    custom.set_editor_property("inputs", inputs)
    return custom


def add_scalar(mat, name, value, x, y):
    node = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    return node


def add_matte(mat):
    rough = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -200, 300)
    rough.set_editor_property("r", 1.0)
    spec = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -200, 400)
    spec.set_editor_property("r", 0.0)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)


def import_files(names, extension, destination):
    tasks = []
    for name in names:
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", os.path.join(SOURCE_DIR, name + extension))
        task.set_editor_property("destination_path", destination)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", True)
        tasks.append(task)
    asset_tools.import_asset_tasks(tasks)


def import_textures():
    names = ["T_Detail", "T_Ground", "T_Foliage", "T_TerrainColor"]
    import_files(names, ".png", PIXEL_DIR)
    textures = {}
    for name in names:
        tex = unreal.load_asset(f"{PIXEL_DIR}/{name}.{name}")
        if not tex:
            raise RuntimeError(f"texture import failed: {name}")
        # Stored as plain multipliers (128 = no change) and shown as crisp texels.
        tex.set_editor_property("srgb", False)
        # The colour map is blended between its texels, like the vertex colours it replaces.
        tex.set_editor_property("filter", unreal.TextureFilter.TF_BILINEAR if name == "T_TerrainColor" else unreal.TextureFilter.TF_NEAREST)
        if name == "T_TerrainColor":
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
            tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        if name == "T_Foliage":
            # Uncompressed, so the cut-out edges stay exactly on the texel grid.
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
        eal.save_loaded_asset(tex)
        textures[name] = tex
    return textures


def build_post_material():
    mat = create_material("M_PixelPost")
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    mat.set_editor_property("blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_DOF)

    # The scene texture nodes are only wired in so the material declares that it
    # uses those textures; the custom node samples them itself with pixel offsets.
    scene_inputs = [
        ("InColor", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0),
        ("InDepth", unreal.SceneTextureId.PPI_SCENE_DEPTH),
        ("InBaseColor", unreal.SceneTextureId.PPI_BASE_COLOR),
        ("InCustomDepth", unreal.SceneTextureId.PPI_CUSTOM_DEPTH),
        ("InCustomStencil", unreal.SceneTextureId.PPI_CUSTOM_STENCIL),
    ]
    params = [
        ("CharBands", 3.0),
        ("DepthEdge", 0.3),     # world units; character depth is squashed to 3 %, so 0.3 is about 10 units of model
        ("InnerLine", 0.6),
        ("LightMax", 0.85),
        ("ShadowBrightness", 1.0),
        ("OutlineDarken", 0.36),
        ("FogStart", 1300.0),
        ("FogEnd", 5600.0),
        ("FogMax", 0.86),
        ("SkyDepth", 100000.0),
    ]
    # One per character on screen: x, y = grid anchor in pixels, z = cell size in pixels (set by APixelCameraRig).
    char_params = [f"Char{i}" for i in range(16)]
    custom = add_custom(mat, "PixelCharacters", POST_CODE, [n for n, _ in scene_inputs + params] + char_params, -200, 0)

    y = -500
    for name, tex_id in scene_inputs:
        node = mel.create_material_expression(mat, unreal.MaterialExpressionSceneTexture, -700, y)
        node.set_editor_property("scene_texture_id", tex_id)
        mel.connect_material_expressions(node, "Color", custom, name)
        y += 220
    for name, value in params:
        mel.connect_material_expressions(add_scalar(mat, name, value, -700, y), "", custom, name)
        y += 100
    for name in char_params:
        node = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -1100, y)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", unreal.LinearColor(0.0, 0.0, 0.0, 0.0))
        mel.connect_material_expressions(node, "", custom, name)
        y += 100

    mel.connect_material_property(custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    return mat


def build_surface_material(name, code, texture, tile_size, strength, local, see_through=False, flatten=False, colour_map=None):
    """Vertex colour multiplied by a pixel detail texture. `local` projects in object space.
    With `colour_map` (the landscape) the colour comes from that texture, looked up by world position."""
    mat = create_material(name)
    mat.set_editor_property("used_with_instanced_static_meshes", True)   # foliage instances
    inputs = ["VC", "Depth", "Tex", "TileSize", "DetailStrength", "LodScale"] + (["LP", "LN"] if local else ["WP"])
    if colour_map:
        inputs += ["ColTex", "ColHalf", "ColStep"]
    custom = add_custom(mat, name[2:] + "Detail", code, inputs, -500, 0)

    vc = mel.create_material_expression(mat, unreal.MaterialExpressionVertexColor, -1000, -200)
    depth = mel.create_material_expression(mat, unreal.MaterialExpressionPixelDepth, -1000, 0)
    tex = mel.create_material_expression(mat, unreal.MaterialExpressionTextureObject, -1000, 120)
    tex.set_editor_property("texture", texture)
    mel.connect_material_expressions(vc, "", custom, "VC")
    mel.connect_material_expressions(depth, "", custom, "Depth")
    mel.connect_material_expressions(tex, "", custom, "Tex")
    if colour_map:
        col = mel.create_material_expression(mat, unreal.MaterialExpressionTextureObject, -1000, 1100)
        col.set_editor_property("texture", colour_map)
        mel.connect_material_expressions(col, "", custom, "ColTex")
        mel.connect_material_expressions(add_scalar(mat, "ColHalf", terrain.SIZE / 2, -1000, 1200), "", custom, "ColHalf")
        mel.connect_material_expressions(add_scalar(mat, "ColStep", terrain.STEP, -1000, 1300), "", custom, "ColStep")
    mel.connect_material_expressions(add_scalar(mat, "TileSize", tile_size, -1000, 300), "", custom, "TileSize")
    mel.connect_material_expressions(add_scalar(mat, "DetailStrength", strength, -1000, 400), "", custom, "DetailStrength")
    # Texels per world unit times the angle of one screen pixel (~0.0008 rad at 1280 px, 55 degrees).
    mel.connect_material_expressions(add_scalar(mat, "LodScale", 64.0 / tile_size * 0.0008, -1000, 500), "", custom, "LodScale")

    wp = mel.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -1400, 620)
    if local:
        to_local = mel.create_material_expression(mat, unreal.MaterialExpressionTransformPosition, -1150, 620)
        to_local.set_editor_property("transform_source_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
        to_local.set_editor_property("transform_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
        mel.connect_material_expressions(wp, "", to_local, "")
        mel.connect_material_expressions(to_local, "", custom, "LP")

        normal = mel.create_material_expression(mat, unreal.MaterialExpressionVertexNormalWS, -1400, 760)
        normal_local = mel.create_material_expression(mat, unreal.MaterialExpressionTransform, -1150, 760)
        normal_local.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD)
        normal_local.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_LOCAL)
        mel.connect_material_expressions(normal, "", normal_local, "")
        mel.connect_material_expressions(normal_local, "", custom, "LN")
    else:
        mel.connect_material_expressions(wp, "", custom, "WP")

    # "Flash" blends to white: driven from C++ when a character is hit.
    white = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -500, 250)
    white.set_editor_property("constant", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    lerp = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -200, 0)
    mel.connect_material_expressions(custom, "", lerp, "A")
    mel.connect_material_expressions(white, "", lerp, "B")
    mel.connect_material_expressions(add_scalar(mat, "Flash", 0.0, -500, 350), "", lerp, "Alpha")
    mel.connect_material_property(lerp, "", unreal.MaterialProperty.MP_BASE_COLOR)
    if see_through:
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        mat.set_editor_property("opacity_mask_clip_value", 0.5)
        mask = add_custom(mat, "SeeThrough", SEE_THROUGH_CODE, ["Depth", "ClipDepth", "ClipRadius", "Enabled"], -500, 600)
        mask.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
        mel.connect_material_expressions(depth, "", mask, "Depth")
        # ClipDepth: a little less than the camera boom, so the character itself is never cut.
        mel.connect_material_expressions(add_scalar(mat, "ClipDepth", 1100.0, -1000, 900), "", mask, "ClipDepth")
        mel.connect_material_expressions(add_scalar(mat, "ClipRadius", 0.34, -1000, 1000), "", mask, "ClipRadius")
        connect_see_through_switch(mat, mask, -1000, 1400)
        mel.connect_material_property(mask, "", unreal.MaterialProperty.MP_OPACITY_MASK)
    if flatten:
        offset = add_custom(mat, "FlattenToCamera", FLATTEN_CODE, ["WP", "ObjPos", "CamPos", "Thickness"], -500, 1200)
        rest = mel.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -1000, 1200)
        rest.set_editor_property("world_position_shader_offset", unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
        mel.connect_material_expressions(rest, "", offset, "WP")
        mel.connect_material_expressions(mel.create_material_expression(mat, unreal.MaterialExpressionObjectPositionWS, -1000, 1320), "", offset, "ObjPos")
        mel.connect_material_expressions(mel.create_material_expression(mat, unreal.MaterialExpressionCameraPositionWS, -1000, 1440), "", offset, "CamPos")
        mel.connect_material_expressions(add_scalar(mat, "Thickness", 0.03, -1000, 1560), "", offset, "Thickness")
        mel.connect_material_property(offset, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    add_matte(mat)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    return mat


def build_zone_material():
    """Red that glows by itself, so the hoops that mark where an enemy is about to charge
    (APixelEnemyCharacter) look the same in sun and in shade."""
    mat = create_material("M_TackleZone")
    red = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -500, 0)
    red.set_editor_property("constant", unreal.LinearColor(0.90, 0.07, 0.05, 1.0))
    # The glow is brighter than the surface colour, so the hoops stand out a little from the scene.
    glow = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -500, 150)
    glow.set_editor_property("constant", unreal.LinearColor(2.0, 0.24, 0.17, 1.0))
    mel.connect_material_property(red, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    add_matte(mat)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    return mat


def build_foliage_material(texture):
    """Leaf cards: the atlas tile multiplies the card's vertex colour, its alpha cuts the shape out."""
    mat = create_material("M_Foliage")
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property("opacity_mask_clip_value", 0.5)
    mat.set_editor_property("two_sided", True)

    tex = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -900, 0)
    tex.set_editor_property("texture", texture)
    tex.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    vc = mel.create_material_expression(mat, unreal.MaterialExpressionVertexColor, -900, -250)
    color = add_custom(mat, "FoliageColor", "return pow(VC, 2.2) * T * 2.0;", ["VC", "T"], -500, -100)
    mel.connect_material_expressions(vc, "", color, "VC")
    mel.connect_material_expressions(tex, "RGB", color, "T")
    mel.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)

    depth = mel.create_material_expression(mat, unreal.MaterialExpressionPixelDepth, -900, 400)
    mask = add_custom(mat, "SeeThrough", SEE_THROUGH_CODE, ["Depth", "ClipDepth", "ClipRadius", "Enabled"], -500, 400)
    mask.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    mel.connect_material_expressions(depth, "", mask, "Depth")
    mel.connect_material_expressions(add_scalar(mat, "ClipDepth", 1100.0, -900, 520), "", mask, "ClipDepth")
    mel.connect_material_expressions(add_scalar(mat, "ClipRadius", 0.34, -900, 620), "", mask, "ClipRadius")
    connect_see_through_switch(mat, mask, -900, 720)
    both = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -200, 300)
    mel.connect_material_expressions(tex, "A", both, "A")
    mel.connect_material_expressions(mask, "", both, "B")
    mel.connect_material_property(both, "", unreal.MaterialProperty.MP_OPACITY_MASK)

    add_matte(mat)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    return mat


def import_meshes(material, character_material, foliage_material, prop_material):
    import_files(MESHES, ".fbx", MESH_DIR)
    meshes = {}
    for name in MESHES:
        mesh = unreal.load_asset(f"{MESH_DIR}/{name}.{name}")
        if not mesh:
            raise RuntimeError(f"import failed: {name}")
        mesh.set_material(0, character_material if name in CHARACTER_MESHES else prop_material if name in PROP_MESHES else material)
        if name in FOLIAGE_MESHES:
            slots = [str(m.get_editor_property("material_slot_name")) for m in mesh.get_editor_property("static_materials")]
            if "Foliage" not in slots:
                raise RuntimeError(f"{name}: no Foliage material slot, got {slots}")
            for index, slot in enumerate(slots):   # a mesh made only of cards has just the one slot
                mesh.set_material(index, foliage_material if slot == "Foliage" else material)
        # The importer wraps every mesh in a convex hull. Nothing should collide with that hull: solid
        # meshes use their triangles, and everything else must not block at all.
        unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).remove_collisions(mesh)
        body_setup = mesh.get_editor_property("body_setup")
        if body_setup:
            body_setup.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE
                                           if name in SOLID_MESHES else unreal.CollisionTraceFlag.CTF_USE_DEFAULT)
        eal.save_loaded_asset(mesh)
        box = mesh.get_bounding_box()
        unreal.log(f"PixelRPG: {name} size {box.max.x - box.min.x:.0f} x {box.max.y - box.min.y:.0f} x {box.max.z - box.min.z:.0f}")
        meshes[name] = mesh
    return meshes


def build_level(meshes, ground_material):
    level_sub = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    # Work in a fresh level so an already loaded PixelTest map can be replaced.
    level_sub.new_level("/Game/Maps/_PixelTestTmp")
    delete_if_exists(MAP_PATH)
    level_sub.new_level(MAP_PATH)
    delete_if_exists("/Game/Maps/_PixelTestTmp")

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    solid_actors = []                                   # skipped when looking for the ground
    instances = {name: [] for name in INSTANCED_MESHES} # transforms of the foliage instances
    stats = {"traced": 0, "estimated": 0}

    def ground(x, y):
        """Height of the ground under a point: traced against the real landscape, because the
        formula in terrain.py and the triangles built from it differ a little on slopes."""
        hit = unreal.SystemLibrary.line_trace_single(
            world, unreal.Vector(x, y, 9000.0), unreal.Vector(x, y, -5000.0), unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,
            True, solid_actors, unreal.DrawDebugTrace.NONE, False)
        points = [value for value in hit.to_tuple() if isinstance(value, unreal.Vector)] if hit else []
        if points:
            stats["traced"] += 1
            return points[0].z
        stats["estimated"] += 1
        return terrain.height(x, y)

    def spawn(name, x, y, yaw=0.0, scale=1.0, label=None, shadow=True, z=None):
        z = ground(x, y) if z is None else z
        if name in TREES:
            spawn(name + "_Trunk", x, y, yaw, scale, label, shadow, z)
            name += "_Leaves"
        if name in INSTANCED_MESHES:
            # Sunk a little, so the foot of a tuft is never above the ground on a slope.
            sink = 0.0 if name in LEAF_MESHES else 4.0 * scale
            instances[name].append(unreal.Transform(unreal.Vector(x, y, z - sink), unreal.Rotator(0, 0, yaw), unreal.Vector(scale, scale, scale)))
            return None
        mesh = meshes[name]
        actor = actor_sub.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(x, y, z), unreal.Rotator(0, 0, yaw))
        comp = actor.static_mesh_component
        comp.set_static_mesh(mesh)
        comp.set_cast_shadow(shadow)
        if name in TRUNK_MESHES:
            # Trunks stop characters but not the camera boom: in a forest it would be pulled in constantly.
            comp.set_editor_property("component_tags", ["NoCameraBlock"])
        if name in SOLID_MESHES:
            solid_actors.append(actor)
        else:
            comp.set_collision_profile_name("NoCollision")
        actor.set_actor_scale3d(unreal.Vector(scale, scale, scale))
        if label:
            actor.set_actor_label(label)
        return actor

    # Lighting: a low, warm sun for long shadows, plus sky light.
    sun = actor_sub.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 1000), unreal.Rotator(0, -36, 35))
    sun_comp = sun.get_component_by_class(unreal.DirectionalLightComponent)
    sun_comp.set_mobility(unreal.ComponentMobility.MOVABLE)
    sun_comp.set_intensity(3.14)
    sun_comp.set_editor_property("atmosphere_sun_light", True)
    sun_comp.set_editor_property("light_source_angle", 0.0)
    sun_comp.set_editor_property("dynamic_shadow_distance_movable_light", 6000.0)
    sun_comp.set_editor_property("dynamic_shadow_cascades", 4)
    sun.set_actor_label("Sun")

    sky = actor_sub.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 1200))
    sky_comp = sky.get_component_by_class(unreal.SkyLightComponent)
    sky_comp.set_mobility(unreal.ComponentMobility.MOVABLE)
    sky_comp.set_editor_property("real_time_capture", True)
    sky_comp.set_intensity(1.0)
    actor_sub.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0))
    actor_sub.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0, 0, terrain.height(0, 0) + 120))

    # The ground: a landscape with the heights of Tools/terrain.py. A height value of 32768 is the Z of the
    # actor, and with a Z scale of 100 one step is 100 / 128 world units.
    half = terrain.SIZE / 2
    heights = [max(0, min(65535, int(round(32768 + terrain.height(-half + i * terrain.SPACING, -half + j * terrain.SPACING) * 1.28))))
               for j in range(terrain.SAMPLES) for i in range(terrain.SAMPLES)]
    landscape = unreal.PixelEditorLibrary.create_landscape(
        world, heights, terrain.COMPONENTS, terrain.QUADS, unreal.Vector(-half, -half, 0.0),
        unreal.Vector(terrain.SPACING, terrain.SPACING, 100.0), ground_material)
    if not landscape:
        raise RuntimeError("the landscape could not be created")
    landscape.set_actor_label("Terrain")

    # Distant peaks beyond the rim of the valley.
    rng = random.Random(7)
    for i in range(9):
        angle = i / 9 * math.tau + rng.uniform(-0.2, 0.2)
        radius = rng.uniform(11000, 13500)
        spawn("SM_Mountain", radius * math.cos(angle), radius * math.sin(angle),
              yaw=rng.uniform(0, 360), scale=rng.uniform(1.6, 2.6), label=f"Mountain{i}", shadow=False, z=600.0)

    # Hand-placed set piece in the clearing at the start.
    spawn("SM_House", 620, -520, yaw=200, label="House")
    spawn("SM_Crystal", -460, 430, yaw=20, scale=1.3, label="CrystalA")
    spawn("SM_Crystal", -570, 350, yaw=140, scale=0.8, label="CrystalB")
    spawn("SM_Rock", -520, 520, yaw=60, scale=1.4, label="CrystalRock")

    occupied = [(620, -520, 480), (0, 0, 260), (-500, 430, 200)]
    # Keep trees and rocks off the spots where the game mode spawns the quest giver and the
    # gathering nodes (same coordinates as APixelGameMode::SpawnWorldObjects).
    occupied += [(x, y, 110) for x, y in [
        (300, -300),
        (-260, -320), (220, 400), (-720, -160), (480, 260), (-160, 720), (920, 220), (-900, 500), (300, 1100),
        (-380, 250), (-700, 560), (1150, -250), (-1100, -500)]]

    def is_free(x, y, radius):
        return (terrain.path_weight(x, y) < 0.05 and math.hypot(x, y) < 7200
                and all((x - ox) ** 2 + (y - oy) ** 2 > (radius + r) ** 2 for ox, oy, r in occupied))

    # Forest: denser in clumps, mostly conifers with patches of autumn colour.
    count = 0
    for i in range(2600):
        x, y = rng.uniform(-6800, 6800), rng.uniform(-6800, 6800)
        clump = terrain.noise(int(x // 900), int(y // 900))
        if rng.random() > 0.25 + clump * 0.75 or not is_free(x, y, 130):
            continue
        if clump > 0.72:
            kind = rng.choices(["SM_TreeRed", "SM_TreeOrange", "SM_TreePine"], [5, 4, 2])[0]
        else:
            kind = rng.choices(["SM_TreePine", "SM_TreeRound", "SM_TreeOrange"], [8, 3, 1])[0]
        occupied.append((x, y, 130))
        spawn(kind, x, y, yaw=rng.uniform(0, 360), scale=rng.uniform(0.85, 1.5), label=f"{kind[3:]}{i}")
        count += 1
        if count >= 700:
            break

    for i in range(160):
        x, y = rng.uniform(-6000, 6000), rng.uniform(-6000, 6000)
        kind = "SM_Rock" if rng.random() < 0.45 else "SM_Bush"
        if is_free(x, y, 80):
            occupied.append((x, y, 80))
            spawn(kind, x, y, yaw=rng.uniform(0, 360), scale=rng.uniform(0.8, 1.6), label=f"{kind[3:]}{i}")

    # Ground cover near the start: no shadows, it only breaks up the ground.
    for i in range(1300):
        x, y = rng.uniform(-3600, 3600), rng.uniform(-3600, 3600)
        if terrain.path_weight(x, y) > 0.3:
            continue
        kind = "SM_Flowers" if rng.random() < 0.08 else "SM_Grass"
        spawn(kind, x, y, yaw=rng.uniform(0, 360), scale=rng.uniform(0.9, 1.7), label=f"{kind[3:]}{i}", shadow=False)

    # Foliage: one foliage type per mesh, all instances added in one go.
    for name, transforms in instances.items():
        type_name = "FT_" + name[3:]
        delete_if_exists(f"{PIXEL_DIR}/{type_name}")
        foliage_type = asset_tools.create_asset(type_name, PIXEL_DIR, unreal.FoliageType_InstancedStaticMesh,
                                                unreal.FoliageType_InstancedStaticMeshFactory())
        foliage_type.set_editor_property("mesh", meshes[name])
        foliage_type.set_editor_property("cast_shadow", INSTANCED_MESHES[name])
        eal.save_loaded_asset(foliage_type)
        unreal.InstancedFoliageActor.add_instances(world, foliage_type, transforms)
        unreal.log(f"PixelRPG: foliage {type_name}: {len(transforms)} instances")
    unreal.log(f"PixelRPG: ground heights traced {stats['traced']}, estimated {stats['estimated']}")

    level_sub.save_current_level()


def main():
    global COLLECTION
    COLLECTION = build_parameter_collection()
    textures = import_textures()
    build_post_material()
    surface = build_surface_material("M_VertexColor", VERTEX_COLOR_CODE, textures["T_Detail"], 218.0, 1.0, local=True, see_through=True)
    character = build_surface_material("M_Character", VERTEX_COLOR_CODE, textures["T_Detail"], 218.0, 0.2, local=True, flatten=True)
    prop = build_surface_material("M_Prop", VERTEX_COLOR_CODE, textures["T_Detail"], 218.0, 0.6, local=True)
    build_zone_material()
    ground = build_surface_material("M_Terrain", TERRAIN_CODE, textures["T_Ground"], 256.0, 1.0, local=False, colour_map=textures["T_TerrainColor"])
    delete_if_exists(f"{PIXEL_DIR}/M_Ground")
    # Left over from the billboard sprite experiment.
    for path in eal.list_assets(PIXEL_DIR, recursive=False):
        name = path.split("/")[-1].split(".")[0]
        if name.startswith(("MI_", "T_SM_", "M_Sprite")):
            eal.delete_asset(path.split(".")[0])
    for old in ["SM_Player", "SM_Slime", "SM_Companion", "SM_Villager", "SM_Terrain"] + TREES:   # replaced by pose sets / trunk + leaves / the landscape
        delete_if_exists(f"{MESH_DIR}/{old}")
    meshes = import_meshes(surface, character, build_foliage_material(textures["T_Foliage"]), prop)
    build_level(meshes, ground)
    unreal.log("PixelRPG: asset setup finished")


main()
