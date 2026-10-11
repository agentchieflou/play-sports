"""Step stadium_look: the stadium bowl's materials (lane V3, Epics 48 and 52).

What APSStadiumSet and UPSCrowdRenderComponent name in Data/stadium_set.json's KindMaterials and
Data/crowd_look.json, built from the CC0 sources in RawAssets/stadium/
(tools/assets/stadium/fetch_stadium_assets.py made them; each folder's LICENSE names its source):

  /Game/Stadium/Textures   T_Concrete_Color (sRGB), T_Concrete_Normal (DirectX normal map) and
                           T_Concrete_ARM (linear: R occlusion, G roughness, B metalness), wrapping.
  /Game/Stadium/Materials  M_StadiumConcrete (world-aligned cast concrete: risers, stairs, walls,
                           the back wall), M_StadiumPaint (a painted or moulded surface: seats,
                           padding, rails, fasciae, the fans' clothes), M_StadiumGlass (the suites
                           and the press box) and M_StadiumScreen (unlit: the video and ribbon
                           boards).

Every material has a vector parameter Color, which the set and the crowd set per kind of piece
(Data/stadium_set.json's colours are each piece's average colour), and a scalar Roughness the data
can override per kind. The concrete is world-aligned (three planar projections blended by the
surface's normal): it tiles in world centimetres, so the bowl's scaled basic-shape boxes need no
UVs. Every material is used with instanced static meshes: the bowl and the crowd are instances.

Idempotent without diffing graphs: every asset carries a metadata tag with the digest of what built
it (a texture: its source file and import settings; a material: this script and concrete.json). An
asset whose tag matches is left alone, so an unchanged run saves nothing and stores no new LFS
version.
"""

import hashlib
import json
import os

import unreal

import ue_content as content

STEP_DIR = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(STEP_DIR)))
RAW = os.path.join(REPO, "RawAssets", "stadium")

TEXTURES_PATH = "/Game/Stadium/Textures"
MATERIALS_PATH = "/Game/Stadium/Materials"
DIGEST_TAG = "PSContentDigest"
# Bump to rebuild every asset of a kind when the way it is made changes in a way the digests miss.
TEXTURE_RECIPE = "1"
MATERIAL_RECIPE = "1"

MEL = unreal.MaterialEditingLibrary

# (asset name, source file under RawAssets/stadium, settings)
TEXTURES = [
    ("T_Concrete_Color", "concrete/T_Concrete_Color.jpg",
     {"srgb": True, "compression_settings": unreal.TextureCompressionSettings.TC_DEFAULT,
      "lod_group": unreal.TextureGroup.TEXTUREGROUP_WORLD}),
    ("T_Concrete_Normal", "concrete/T_Concrete_Normal.jpg",
     {"srgb": False, "compression_settings": unreal.TextureCompressionSettings.TC_NORMALMAP,
      "lod_group": unreal.TextureGroup.TEXTUREGROUP_WORLD_NORMAL_MAP, "flip_green_channel": False}),
    ("T_Concrete_ARM", "concrete/T_Concrete_ARM.jpg",
     {"srgb": False, "compression_settings": unreal.TextureCompressionSettings.TC_MASKS,
      "lod_group": unreal.TextureGroup.TEXTUREGROUP_WORLD_SPECULAR}),
]

MATERIALS = ("M_StadiumConcrete", "M_StadiumPaint", "M_StadiumGlass", "M_StadiumScreen")


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


def _load(package):
    if not os.path.isfile(content.package_file(package, ".uasset")):
        return None
    name = package.rsplit("/", 1)[1]
    return unreal.load_asset(f"{package}.{name}")


def _needs_build(ctx, package, digest):
    """True when package is missing or was built from something else; records the change."""
    asset = _load(package)
    current = unreal.EditorAssetLibrary.get_metadata_tag(asset, DIGEST_TAG) if asset is not None else ""
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
# Textures
# ---------------------------------------------------------------------------------------------

def _import(source, destination, name):
    if not os.path.isfile(source):
        raise RuntimeError(f"{source} is missing: run tools/assets/stadium/fetch_stadium_assets.py (Git LFS checkout?).")
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", source)
    task.set_editor_property("destination_path", destination)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
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

    def scalar(self, name, default):
        return self.node(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=float(default), group="Stadium")

    def vector(self, name, color):
        return self.node(unreal.MaterialExpressionVectorParameter, parameter_name=name,
                         default_value=unreal.LinearColor(*color), group="Stadium")

    def const(self, value):
        return self.node(unreal.MaterialExpressionConstant, r=float(value))

    def const3(self, color):
        return self.node(unreal.MaterialExpressionConstant3Vector, constant=color)

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

    def dot(self, a, b, a_out="", b_out=""):
        return self.binary(unreal.MaterialExpressionDotProduct, a, a_out, b, b_out)

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

    def sample(self, name, texture, uvs, sampler):
        expression = self.node(unreal.MaterialExpressionTextureSampleParameter2D, parameter_name=name,
                               texture=texture, sampler_type=sampler, group="Stadium|Textures")
        self.link(uvs, "", expression, "UVs")
        return expression


def _triplanar(g, name, texture, sampler, uv_x, uv_y, uv_z, weights, output):
    """The texture projected along X, Y and Z in world space, blended by the surface's normal."""
    blended = None
    for uv, weight in ((uv_x, g.mask(weights, r=True)), (uv_y, g.mask(weights, g=True)), (uv_z, g.mask(weights, b=True))):
        sample = g.sample(name, texture, uv, sampler)
        term = g.mul(sample, weight, output, "")
        blended = term if blended is None else g.add(blended, term)
    return blended


def build_concrete(g, textures, mean_color):
    world = g.node(unreal.MaterialExpressionWorldPosition)
    tile = g.scalar("TileSizeCm", 300.0)
    scaled = g.div(world, tile)
    uv_x = g.mask(scaled, g=True, b=True)
    uv_y = g.mask(scaled, r=True, b=True)
    uv_z = g.mask(scaled, r=True, g=True)
    # Blend weights: the normal's components to the fourth power, normalised, so each face takes
    # the projection it faces and the edges blend over a short distance.
    normal = g.unary(unreal.MaterialExpressionAbs, g.node(unreal.MaterialExpressionVertexNormalWS))
    squared = g.mul(normal, normal)
    fourth = g.mul(squared, squared)
    weights = g.div(fourth, g.dot(fourth, g.const3(unreal.LinearColor(1.0, 1.0, 1.0, 0.0))))

    color = _triplanar(g, "ConcreteColor", textures["T_Concrete_Color"], unreal.MaterialSamplerType.SAMPLERTYPE_COLOR,
                       uv_x, uv_y, uv_z, weights, "RGB")
    arm = _triplanar(g, "ConcreteARM", textures["T_Concrete_ARM"], unreal.MaterialSamplerType.SAMPLERTYPE_MASKS,
                     uv_x, uv_y, uv_z, weights, "RGB")
    # Colour: the concrete divided by its own mean colour, times Color, so Color is the average.
    albedo = g.mul(g.div(color, g.vector("TextureMeanColor", mean_color)), g.vector("Color", (0.26, 0.25, 0.23)))
    g.to_property(albedo, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = g.mul(g.mask(arm, g=True), g.scalar("Roughness", 1.0))
    g.to_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    g.to_property(g.scalar("Specular", 0.4), "", unreal.MaterialProperty.MP_SPECULAR)
    occlusion = g.lerp(g.const(1.0), g.mask(arm, r=True), g.scalar("OcclusionStrength", 0.7))
    g.to_property(occlusion, "", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)


def build_paint(g):
    g.to_property(g.vector("Color", (0.5, 0.5, 0.5)), "", unreal.MaterialProperty.MP_BASE_COLOR)
    g.to_property(g.scalar("Roughness", 0.55), "", unreal.MaterialProperty.MP_ROUGHNESS)
    g.to_property(g.scalar("Metallic", 0.0), "", unreal.MaterialProperty.MP_METALLIC)
    g.to_property(g.scalar("Specular", 0.5), "", unreal.MaterialProperty.MP_SPECULAR)


def build_glass(g):
    g.to_property(g.vector("Color", (0.02, 0.03, 0.04)), "", unreal.MaterialProperty.MP_BASE_COLOR)
    g.to_property(g.scalar("Roughness", 0.06), "", unreal.MaterialProperty.MP_ROUGHNESS)
    g.to_property(g.scalar("Metallic", 0.4), "", unreal.MaterialProperty.MP_METALLIC)
    g.to_property(g.scalar("Specular", 1.0), "", unreal.MaterialProperty.MP_SPECULAR)


def build_screen(g):
    emissive = g.mul(g.vector("Color", (0.1, 0.25, 0.6)), g.scalar("EmissiveStrength", 3.0))
    g.to_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)


def _material(ctx, name, digest, builder, unlit=False):
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
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    if unlit:
        material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    # The bowl and the crowd are instanced meshes: a cooked game draws a material on one only when
    # the material says so.
    material.set_editor_property("used_with_instanced_static_meshes", True)
    builder(Graph(material))
    MEL.layout_material_expressions(material)
    MEL.recompile_material(material)
    _save(ctx, material, package, digest)


def build_materials(ctx):
    with open(os.path.join(RAW, "concrete", "concrete.json"), encoding="utf-8") as handle:
        concrete = json.load(handle)
    mean = tuple(concrete["MeanLinearColor"]) + (1.0,)
    with open(os.path.abspath(__file__), "rb") as handle:
        script = handle.read().replace(b"\r\n", b"\n")
    digest = _sha(MATERIAL_RECIPE, _sha(script), json.dumps(concrete, sort_keys=True))

    textures = {name: _load(f"{TEXTURES_PATH}/{name}") for name, _, _ in TEXTURES}
    if ctx.dry_run and any(texture is None for texture in textures.values()):
        # A dry run can't build the materials' inputs; the changes above already say so.
        for name in MATERIALS:
            _needs_build(ctx, f"{MATERIALS_PATH}/{name}", digest)
        return
    missing = [name for name, texture in textures.items() if texture is None]
    if missing:
        raise RuntimeError(f"The materials' textures are missing: {missing}")

    _material(ctx, "M_StadiumConcrete", digest, lambda g: build_concrete(g, textures, mean))
    _material(ctx, "M_StadiumPaint", digest, build_paint)
    _material(ctx, "M_StadiumGlass", digest, build_glass)
    _material(ctx, "M_StadiumScreen", digest, build_screen, unlit=True)


def build(ctx):
    build_textures(ctx)
    build_materials(ctx)
