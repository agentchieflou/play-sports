"""Step field_look: the field's turf and paint materials and the skies (lane V2).

What APSFieldSurface and UPSStadiumLightingSubsystem name in Data/field_markings.json and
Data/stadium_lighting.json, built from the CC0 sources in RawAssets/field/
(tools/assets/field/fetch_field_assets.py made them; each folder's LICENSE names its source):

  /Game/Field/Textures   T_Turf_Color (sRGB), T_Turf_Normal (DirectX normal map), T_Turf_ORM
                         (linear: R occlusion, G roughness) and T_Turf_Noise (linear: R macro,
                         G breakup, B medium), all wrapping.
  /Game/Field/Skies      HDR_Sky_Day and HDR_Sky_Night: cubemaps for the sky light on tiers without
                         a real-time sky capture.
  /Game/Field/Materials  MPC_Field (the Dew scalar the lighting preset sets), M_Turf (opaque, every
                         detail), M_TurfLite (one texture sample and the macro variation: the low
                         mobile tier) and M_FieldPaint (masked painted grass: lines, numerals,
                         arrows, borders, end zones).

The materials are world-aligned: they tile in world centimetres, so the field's scaled basic-shape
planes need no UVs. Every parameter has a default here and is set per piece by APSFieldSurface from
Data/field_markings.json (tuning lives in data). Mowing stripes run across the field, a band every
StripeWidthCm along X, and brighten or darken with the view direction as real ones do.

Idempotent without diffing graphs: every asset carries a metadata tag with the digest of what built
it (a texture: its source file and import settings; a material: this script and turf.json). An asset
whose tag matches is left alone, so an unchanged run saves nothing and stores no new LFS version.
"""

import hashlib
import json
import os

import unreal

import ue_content as content

STEP_DIR = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(STEP_DIR)))
RAW = os.path.join(REPO, "RawAssets", "field")

TEXTURES_PATH = "/Game/Field/Textures"
SKIES_PATH = "/Game/Field/Skies"
MATERIALS_PATH = "/Game/Field/Materials"
DIGEST_TAG = "PSContentDigest"
# Bump to rebuild every asset of a kind when the way it is made changes in a way the digests miss.
TEXTURE_RECIPE = "1"
MATERIAL_RECIPE = "1"

MEL = unreal.MaterialEditingLibrary

# (asset name, source file under RawAssets/field, settings)
TEXTURES = [
    ("T_Turf_Color", "turf/T_Turf_Color.jpg",
     {"srgb": True, "compression_settings": unreal.TextureCompressionSettings.TC_DEFAULT,
      "lod_group": unreal.TextureGroup.TEXTUREGROUP_WORLD}),
    ("T_Turf_Normal", "turf/T_Turf_Normal.jpg",
     {"srgb": False, "compression_settings": unreal.TextureCompressionSettings.TC_NORMALMAP,
      "lod_group": unreal.TextureGroup.TEXTUREGROUP_WORLD_NORMAL_MAP, "flip_green_channel": False}),
    ("T_Turf_ORM", "turf/T_Turf_ORM.png",
     {"srgb": False, "compression_settings": unreal.TextureCompressionSettings.TC_MASKS,
      "lod_group": unreal.TextureGroup.TEXTUREGROUP_WORLD_SPECULAR}),
    ("T_Turf_Noise", "turf/T_Turf_Noise.png",
     {"srgb": False, "compression_settings": unreal.TextureCompressionSettings.TC_MASKS,
      "lod_group": unreal.TextureGroup.TEXTUREGROUP_WORLD}),
]

SKIES = [
    ("HDR_Sky_Day", "sky/HDR_Sky_Day.hdr"),
    ("HDR_Sky_Night", "sky/HDR_Sky_Night.hdr"),
]

LUMINANCE = unreal.LinearColor(0.2126, 0.7152, 0.0722, 0.0)


# ---------------------------------------------------------------------------------------------
# Bookkeeping
# ---------------------------------------------------------------------------------------------

def _sha(*parts):
    digest = hashlib.sha256()
    for part in parts:
        digest.update(part if isinstance(part, bytes) else str(part).encode("utf-8"))
        digest.update(b"\0")
    return digest.hexdigest()


def _file_bytes(path):
    with open(path, "rb") as handle:
        data = handle.read()
    # Text with CRLF read as LF, as the pipeline's digests do, so Windows and Linux agree.
    return data.replace(b"\r\n", b"\n") if b"\0" not in data[:8000] else data


def _asset_file(package):
    return content.package_file(package, ".uasset")


def _load(package):
    if not os.path.isfile(_asset_file(package)):
        return None
    name = package.rsplit("/", 1)[1]
    return unreal.load_asset(f"{package}.{name}")


def _current_digest(asset):
    if asset is None:
        return ""
    return unreal.EditorAssetLibrary.get_metadata_tag(asset, DIGEST_TAG) or ""


def _needs_build(ctx, package, digest):
    """True when package is missing or was built from something else; records the change."""
    asset = _load(package)
    current = _current_digest(asset)
    if asset is not None and current == digest:
        return False
    ctx.change(f"{'build' if asset is None else 'rebuild'} {package}")
    return not ctx.dry_run


def _save(ctx, asset, package, digest):
    unreal.EditorAssetLibrary.set_metadata_tag(asset, DIGEST_TAG, digest)
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, False):
        raise RuntimeError(f"Couldn't save {package}.")
    if package not in ctx.saved:
        ctx.saved.append(package)


# ---------------------------------------------------------------------------------------------
# Textures and skies
# ---------------------------------------------------------------------------------------------

def _import(source, destination, name):
    if not os.path.isfile(source):
        raise RuntimeError(f"{source} is missing: run tools/assets/field/fetch_field_assets.py (Git LFS checkout?).")
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", source)
    task.set_editor_property("destination_path", destination)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    # The engine's texture factory, explicitly: it makes a cubemap of a long-lat .hdr.
    task.set_editor_property("factory", unreal.TextureFactory())
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    asset = unreal.load_asset(f"{destination}/{name}.{name}")
    if asset is None:
        raise RuntimeError(f"Importing {source} made no {destination}/{name}.")
    return asset


def build_textures(ctx):
    for name, relative, settings in TEXTURES:
        package = f"{TEXTURES_PATH}/{name}"
        source = os.path.join(RAW, *relative.split("/"))
        digest = _sha(TEXTURE_RECIPE, _sha(_file_bytes(source)), sorted((k, str(v)) for k, v in settings.items()))
        if not _needs_build(ctx, package, digest):
            continue
        texture = _import(source, TEXTURES_PATH, name)
        if not isinstance(texture, unreal.Texture2D):
            raise RuntimeError(f"{package} imported as {texture.get_class().get_name()}, not a Texture2D.")
        for key, value in settings.items():
            texture.set_editor_property(key, value)
        texture.set_editor_property("address_x", unreal.TextureAddress.TA_WRAP)
        texture.set_editor_property("address_y", unreal.TextureAddress.TA_WRAP)
        _save(ctx, texture, package, digest)


def build_skies(ctx):
    for name, relative in SKIES:
        package = f"{SKIES_PATH}/{name}"
        source = os.path.join(RAW, *relative.split("/"))
        digest = _sha(TEXTURE_RECIPE, "cube", _sha(_file_bytes(source)))
        if not _needs_build(ctx, package, digest):
            continue
        cube = _import(source, SKIES_PATH, name)
        if not isinstance(cube, unreal.TextureCube):
            raise RuntimeError(f"{package} imported as {cube.get_class().get_name()}, not a TextureCube.")
        _save(ctx, cube, package, digest)


# ---------------------------------------------------------------------------------------------
# Materials
# ---------------------------------------------------------------------------------------------

class Graph:
    """Adds nodes to a material and wires them, failing loudly on a connection the engine refuses."""

    def __init__(self, material):
        self.material = material
        self.row = 0

    def node(self, cls, **properties):
        self.row += 1
        expression = MEL.create_material_expression(self.material, cls, -1600 + (self.row % 8) * 200, (self.row // 8) * 160)
        if expression is None:
            raise RuntimeError(f"Couldn't add a {cls.__name__} to {self.material.get_name()}.")
        for key, value in properties.items():
            expression.set_editor_property(key, value)
        return expression

    def link(self, source, output, target, target_input):
        if not MEL.connect_material_expressions(source, output, target, target_input):
            raise RuntimeError(f"{self.material.get_name()}: couldn't connect {source.get_class().get_name()}.{output or '<out>'} "
                               f"to {target.get_class().get_name()}.{target_input}")

    def to_property(self, source, output, prop):
        if not MEL.connect_material_property(source, output, prop):
            raise RuntimeError(f"{self.material.get_name()}: couldn't connect {source.get_class().get_name()}.{output or '<out>'} to {prop}")

    # Small builders --------------------------------------------------------------------------

    def scalar(self, name, default, group="Field"):
        return self.node(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=float(default), group=group)

    def vector(self, name, color, group="Field"):
        return self.node(unreal.MaterialExpressionVectorParameter, parameter_name=name,
                         default_value=unreal.LinearColor(*color), group=group)

    def const(self, value):
        return self.node(unreal.MaterialExpressionConstant, r=float(value))

    def const3(self, color):
        return self.node(unreal.MaterialExpressionConstant3Vector, constant=color)

    def const2(self, x, y):
        return self.node(unreal.MaterialExpressionConstant2Vector, r=float(x), g=float(y))

    def binary(self, cls, a, a_out, b, b_out):
        expression = self.node(cls)
        self.link(a, a_out, expression, "A")
        self.link(b, b_out, expression, "B")
        return expression

    def mul(self, a, b, a_out="", b_out=""):
        return self.binary(unreal.MaterialExpressionMultiply, a, a_out, b, b_out)

    def div(self, a, b, a_out="", b_out=""):
        return self.binary(unreal.MaterialExpressionDivide, a, a_out, b, b_out)

    def add(self, a, b, a_out="", b_out=""):
        return self.binary(unreal.MaterialExpressionAdd, a, a_out, b, b_out)

    def sub(self, a, b, a_out="", b_out=""):
        return self.binary(unreal.MaterialExpressionSubtract, a, a_out, b, b_out)

    def lerp(self, a, b, alpha, a_out="", b_out="", alpha_out=""):
        expression = self.node(unreal.MaterialExpressionLinearInterpolate)
        self.link(a, a_out, expression, "A")
        self.link(b, b_out, expression, "B")
        self.link(alpha, alpha_out, expression, "Alpha")
        return expression

    def unary(self, cls, a, a_out=""):
        expression = self.node(cls)
        self.link(a, a_out, expression, "")
        return expression

    def mask(self, a, r=False, g=False, b=False, a_out=""):
        expression = self.node(unreal.MaterialExpressionComponentMask, r=r, g=g, b=b, a=False)
        self.link(a, a_out, expression, "")
        return expression

    def dot(self, a, b, a_out="", b_out=""):
        return self.binary(unreal.MaterialExpressionDotProduct, a, a_out, b, b_out)

    def sample(self, name, texture, uvs, sampler):
        expression = self.node(unreal.MaterialExpressionTextureSampleParameter2D, parameter_name=name,
                               texture=texture, sampler_type=sampler, group="Field|Textures")
        self.link(uvs, "", expression, "UVs")
        return expression


class TurfInputs:
    """The nodes the turf and the paint share: world-aligned UVs, the grass samples, the stripes."""

    def __init__(self, graph, textures, mean_color, detailed):
        g = graph
        self.world = g.node(unreal.MaterialExpressionWorldPosition)
        self.xy = g.mask(self.world, r=True, g=True)
        self.tile = g.scalar("TileSizeCm", 150.0)
        self.uv = g.div(self.xy, self.tile)
        self.color = g.sample("TurfColor", textures["T_Turf_Color"], self.uv, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
        grass = self.color
        grass_out = "RGB"
        if detailed:
            # A second, larger and offset sample of the same grass hides the tiling at a distance.
            second_tile = g.mul(self.tile, g.scalar("SecondTileScale", 3.3))
            uv2 = g.add(g.div(self.xy, second_tile), g.const2(0.37, 0.71))
            color2 = g.sample("TurfColor", textures["T_Turf_Color"], uv2, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
            grass = g.lerp(self.color, color2, g.scalar("SecondTileMix", 0.35), "RGB", "RGB")
            grass_out = ""
        self.grass = grass
        self.grass_out = grass_out
        self.mean = g.vector("TextureMeanColor", mean_color)
        self.orm = g.sample("TurfORM", textures["T_Turf_ORM"], self.uv, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
        self.normal = g.sample("TurfNormal", textures["T_Turf_Normal"], self.uv, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        self.noise_texture = textures["T_Turf_Noise"]

        # Mowing stripes: a band every StripeWidthCm along X, lighter and darker by turns, more so
        # as the camera looks along the field (the blades lean toward or away from it).
        x = g.mask(self.world, r=True)
        along = g.div(g.sub(x, g.scalar("StripeOriginCm", 0.0)), g.scalar("StripeWidthCm", 500.0))
        phase = g.unary(unreal.MaterialExpressionFrac, along)
        band = g.sub(g.mul(g.unary(unreal.MaterialExpressionFloor, g.mul(phase, g.const(2.0))), g.const(2.0)), g.const(1.0))
        camera = g.mask(g.node(unreal.MaterialExpressionCameraVectorWS), r=True)
        strength = g.add(g.scalar("StripeContrast", 0.06), g.mul(g.scalar("StripeViewContrast", 0.08), camera))
        self.stripe = g.add(g.const(1.0), g.mul(band, strength))


def _dew(graph, collection):
    """MPC_Field.Dew, which the lighting preset sets: at night the grass is glossier."""
    return graph.node(unreal.MaterialExpressionCollectionParameter, collection=collection, parameter_name="Dew")


def build_turf(graph, textures, mean_color, collection, detailed):
    g = graph
    t = TurfInputs(g, textures, mean_color, detailed)
    # Colour: the grass divided by its own mean colour, times Color: so Color is the field's average.
    albedo = g.mul(g.div(t.grass, t.mean, t.grass_out, ""), g.vector("Color", (0.044, 0.155, 0.023)))

    macro_uv = g.div(t.xy, g.scalar("MacroScaleCm", 4000.0))
    macro = g.sample("TurfNoise", t.noise_texture, macro_uv, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    macro_strength = g.scalar("MacroStrength", 0.12)
    variation = g.lerp(g.sub(g.const(1.0), macro_strength), g.add(g.const(1.0), macro_strength), macro, "", "", "R")
    albedo = g.mul(albedo, variation)
    if detailed:
        # Drier, yellower patches at a medium scale.
        medium_uv = g.div(t.xy, g.scalar("DryScaleCm", 1500.0))
        medium = g.sample("TurfNoise", t.noise_texture, medium_uv, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
        dry = g.mul(albedo, g.vector("DryTint", (1.25, 1.1, 0.7)))
        albedo = g.lerp(albedo, dry, g.mul(medium, g.scalar("DryAmount", 0.25), "B"))
    albedo = g.mul(albedo, t.stripe)
    g.to_property(albedo, "", unreal.MaterialProperty.MP_BASE_COLOR)

    flat = g.const3(unreal.LinearColor(0.0, 0.0, 1.0, 0.0))
    g.to_property(g.lerp(flat, t.normal, g.scalar("NormalStrength", 0.8), "", "RGB"), "", unreal.MaterialProperty.MP_NORMAL)

    dew = _dew(g, collection)
    rough = g.mul(t.orm, g.scalar("RoughnessScale", 1.0), "G")
    rough = g.mul(rough, g.lerp(g.const(1.0), g.scalar("DewRoughnessScale", 0.55), dew))
    g.to_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    g.to_property(g.scalar("Specular", 0.35), "", unreal.MaterialProperty.MP_SPECULAR)
    g.to_property(g.lerp(g.const(1.0), t.orm, g.scalar("OcclusionStrength", 0.6), "", "R"), "", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)


def build_paint(graph, textures, mean_color, collection):
    g = graph
    t = TurfInputs(g, textures, mean_color, detailed=False)
    # Paint on grass: the paint's colour, with the blades' light and dark showing through it.
    luma = g.dot(t.color, g.const3(LUMINANCE), "RGB")
    mean_luma = g.dot(t.mean, g.const3(LUMINANCE))
    detail = g.lerp(g.const(1.0), g.div(luma, mean_luma), g.scalar("PaintTextureStrength", 0.6))
    paint = g.mul(g.vector("Color", (1.0, 1.0, 1.0)), detail)
    paint = g.mul(paint, g.lerp(g.const(1.0), t.stripe, g.scalar("PaintStripeAmount", 0.5)))
    g.to_property(paint, "", unreal.MaterialProperty.MP_BASE_COLOR)

    flat = g.const3(unreal.LinearColor(0.0, 0.0, 1.0, 0.0))
    g.to_property(g.lerp(flat, t.normal, g.scalar("PaintNormalStrength", 0.45), "", "RGB"), "", unreal.MaterialProperty.MP_NORMAL)
    dew = _dew(g, collection)
    rough = g.mul(g.scalar("PaintRoughness", 0.75), g.lerp(g.const(1.0), g.scalar("DewRoughnessScale", 0.55), dew))
    g.to_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    g.to_property(g.scalar("Specular", 0.4), "", unreal.MaterialProperty.MP_SPECULAR)

    # Coverage: the breakup noise (an even histogram) keeps Coverage of the pixels; the rest show
    # the grass below. Clipped at 0.5: visible where breakup + Coverage - 0.5 > 0.5.
    breakup_uv = g.div(t.xy, g.scalar("BreakupScaleCm", 60.0))
    breakup = g.sample("TurfNoise", t.noise_texture, breakup_uv, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    coverage = g.add(breakup, g.sub(g.scalar("Coverage", 0.93), g.const(0.5)), "G")

    # Arrows: a triangle cut from the quad, in world space from the instance's custom data
    # (APSFieldSurface): 0 direction (+1 or -1 along X), 1-2 centre X and Y, 3 length, 4 base.
    def custom(index, default):
        return g.node(unreal.MaterialExpressionPerInstanceCustomData, data_index=index, const_default_value=float(default))

    direction, center_x, center_y, length, base = custom(0, 1.0), custom(1, 0.0), custom(2, 0.0), custom(3, 1.0), custom(4, 1.0)
    x = g.mask(t.world, r=True)
    y = g.mask(t.world, g=True)
    along = g.mul(g.sub(x, center_x), direction)
    across = g.unary(unreal.MaterialExpressionAbs, g.sub(y, center_y))
    half_width = g.mul(g.mul(base, g.const(0.5)), g.sub(g.const(0.5), g.div(along, length)))
    inside = g.unary(unreal.MaterialExpressionSaturate, g.mul(g.sub(half_width, across), g.const(10000.0)))
    shape = g.lerp(g.const(1.0), inside, g.scalar("ShapeTriangle", 0.0))
    g.to_property(g.mul(coverage, shape), "", unreal.MaterialProperty.MP_OPACITY_MASK)


def _material(ctx, name, digest, blend_mode, builder):
    package = f"{MATERIALS_PATH}/{name}"
    if not _needs_build(ctx, package, digest):
        return
    material = _load(package)
    if material is None:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, MATERIALS_PATH, unreal.Material, unreal.MaterialFactoryNew())
        if material is None:
            raise RuntimeError(f"Couldn't create {package}.")
    else:
        MEL.delete_all_material_expressions(material)
    material.set_editor_property("blend_mode", blend_mode)
    if blend_mode == unreal.BlendMode.BLEND_MASKED:
        material.set_editor_property("opacity_mask_clip_value", 0.5)
    # The lines, numerals and arrows are instanced meshes: a cooked game draws a material on one
    # only when the material says so.
    material.set_editor_property("used_with_instanced_static_meshes", True)
    builder(Graph(material))
    MEL.layout_material_expressions(material)
    MEL.recompile_material(material)
    _save(ctx, material, package, digest)


def build_collection(ctx, digest):
    package = f"{MATERIALS_PATH}/MPC_Field"
    if _needs_build(ctx, package, digest):
        collection = _load(package)
        if collection is None:
            collection = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                "MPC_Field", MATERIALS_PATH, unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew())
            if collection is None:
                raise RuntimeError(f"Couldn't create {package}.")
        dew = unreal.CollectionScalarParameter()
        dew.set_editor_property("parameter_name", "Dew")
        dew.set_editor_property("default_value", 0.0)
        collection.set_editor_property("scalar_parameters", [dew])
        _save(ctx, collection, package, digest)
    return _load(package)


def build_materials(ctx):
    with open(os.path.join(RAW, "turf", "turf.json"), encoding="utf-8") as handle:
        turf = json.load(handle)
    mean = tuple(turf["MeanLinearColor"]) + (1.0,)
    with open(os.path.abspath(__file__), "rb") as handle:
        script = handle.read().replace(b"\r\n", b"\n")
    collection_digest = _sha(MATERIAL_RECIPE, "MPC_Field", "Dew=0")
    # The collection's parameter id is part of every material that reads it: a rebuilt collection
    # rebuilds the materials too.
    digest = _sha(MATERIAL_RECIPE, _sha(script), json.dumps(turf, sort_keys=True), collection_digest)

    collection = build_collection(ctx, collection_digest)
    textures = {name: _load(f"{TEXTURES_PATH}/{name}") for name, _, _ in TEXTURES}
    if ctx.dry_run and (collection is None or any(texture is None for texture in textures.values())):
        # A dry run can't build the materials' inputs; the changes above already say so.
        for name in ("M_Turf", "M_TurfLite", "M_FieldPaint"):
            _needs_build(ctx, f"{MATERIALS_PATH}/{name}", digest)
        return
    missing = [name for name, texture in textures.items() if texture is None]
    if missing or collection is None:
        raise RuntimeError(f"The materials' inputs are missing: {missing or 'MPC_Field'}")

    _material(ctx, "M_Turf", digest, unreal.BlendMode.BLEND_OPAQUE,
              lambda g: build_turf(g, textures, mean, collection, detailed=True))
    _material(ctx, "M_TurfLite", digest, unreal.BlendMode.BLEND_OPAQUE,
              lambda g: build_turf(g, textures, mean, collection, detailed=False))
    _material(ctx, "M_FieldPaint", digest, unreal.BlendMode.BLEND_MASKED,
              lambda g: build_paint(g, textures, mean, collection))


def build(ctx):
    build_textures(ctx)
    build_skies(ctx)
    build_materials(ctx)
